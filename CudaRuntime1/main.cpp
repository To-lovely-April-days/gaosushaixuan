// ============================================================================
// 文件名：main.cpp
// 版本  ：v4.6
// 作用  ：工业分选程序的主入口
//
// v4.5 改动 (相对 v3.6):
//   1. threadFunction 加分段计时 (H2D / calc_GPU / D2H 各段独立计时)
//      → 任一段 > 5ms 立即打印慢路径日志, 用于排查 GPU 突刺
//   2. threadFunction 和 threadFunction_dis 提为 TIME_CRITICAL 优先级
//      → 减少被 Windows 调度抢占的概率
//   3. 引入 PerfLog 异步日志模块, 输出到 perf.log
//      → 热路径上日志开销 < 1us, 缓冲满直接丢弃, 不阻塞
//   4. dis 线程每 100 批打印一次队列水位 (改为走 PerfLog)
//
// v4.6 改动:
//   5. ★ 启动时调 LogAllParams("run start") 把全部运行参数快照写入 perf.log
//      配合 Eject.cpp 的 [PARAM-CHG] 改参留痕, 排查"误吹/参数变化"更直接
// ============================================================================

#include <stdio.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <queue>
#include <algorithm>
#include <vector>
#include "windows.h"
#include <ShlObj.h>
#include <fstream>

// ===== 相机 SDK 头文件 =====
#include "GrabData.h"
#include "Camera.h"
#include "CameraEnumerate.h"
#include "CameraInfo.h"

// ===== 项目自有模块 =====
#include "kernel.h"
#include "PlsAI.h"
#include "CalibrationInfo.h"
#include "LockFreeQueue.h"
#include "AIModel.h"
#include "Eject.h"
#include "EventLog.h"
#include "UnifyProcessor.h"
#include "PerfLog.h"             // ★ v4.5 新增

// 来自 Eject.cpp 的全局参数
extern int sel_type[12];
extern int percent[12];
extern int fa_ctl;

using namespace std;
using namespace BaseCamera;
using namespace FactoryCameras;

// ============================================================================
// 全局状态变量
// ============================================================================
string currentStatus = "1";
bool   startFlag = false;

Camera* g_camera = nullptr;
vector<CameraInfo*>* g_cameraInfos = nullptr;

int g_Samples = 640;
int g_bands = 182;

std::atomic<int> g_batchNo{ 0 };

AIModel aiModel;
CalibrationInfo calibrationInfo;

int dealCount = 24;
int frameq;

// ============================================================================
// 共享队列
// ============================================================================
LockFreeQueue<GrabData*>          grabDatas;
LockFreeQueue<unsigned long long> frameTickQueue;

struct FrameTicks { unsigned long long t[64]; };

LockFreeQueue<char*>        sortResultDatas;
LockFreeQueue<FrameTicks*>  batchFrameTicksQueue;
LockFreeQueue<char> firstByteList;

// ============================================================================
// 同步事件句柄
// ============================================================================
HANDLE hEvent = NULL;
HANDLE hEventSort = NULL;

// ============================================================================
// 函数前置声明
// ============================================================================
bool Connect();
bool Configure();
bool Start();
bool Stop();
bool Disconnect();
void UserGrabedDataEvent(GrabData* grabData);

// ============================================================================
// 控制台关闭事件处理
// ============================================================================
BOOL CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
    case CTRL_CLOSE_EVENT:
        std::cout << "控制台窗口正在关闭...\n";
        if (startFlag) {
            Stop();
            Disconnect();
        }
        StopEventLog();
        UnifyShutdown();
        PerfLogShutdown();   // ★ v4.5
        return TRUE;
    default:
        return FALSE;
    }
}

string GetDocumentPath()
{
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path) == S_OK)
        return string(path);
    else
        return "";
}

// ============================================================================
// 主函数
// ============================================================================
int main()
{
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    // ★ v3.8 禁用 CMD 控制台的"快速编辑模式"
    {
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        if (hStdin != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(hStdin, &mode)) {
                mode &= ~(ENABLE_QUICK_EDIT_MODE);
                mode &= ~(ENABLE_INSERT_MODE);
                mode |= ENABLE_EXTENDED_FLAGS;
                SetConsoleMode(hStdin, mode);
                std::cout << "[Console] 已禁用快速编辑/插入模式" << std::endl;
            }
        }
    }
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    StartEventLog("event.log");

    // ★ v4.5 启动异步性能日志
    PerfLogInit("perf.log");

    hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
    hEventSort = CreateEvent(NULL, FALSE, TRUE, NULL);

    UnifyInit(dealCount, g_Samples);

    cout << "正在启动分选程序... " << endl;

Restart:
    bool result;

    // 1) 连接相机
    result = Connect();

    // 2) 加载 AI 模型
    string pathDoc = GetDocumentPath();
    string pathMode = pathDoc + "\\SortingExpert\\param\\modeCpp.models";
    std::ifstream inputFile(pathMode);
    json jRead;
    inputFile >> jRead;
    aiModel.from_json(jRead);
    inputFile.close();

    // 3) 加载校准
    string pathCali = pathDoc + "\\SortingExpert\\param\\modelCpp.calibrationInfo";
    calibrationInfo.loadData(pathCali);

    // 4) 配置相机参数
    result = Configure();
    g_Samples = g_camera->GetSamples()->getValue();
    g_bands = g_camera->GetBands()->getValue();

    // 5) 初始化 GPU
    result = initGpu();
    if (!result) {
        std::cout << "[ERROR] GPU 初始化失败！请检查上方日志" << std::endl;
        std::cout << "按任意键退出..." << std::endl;
        std::string num;
        std::cin >> num;
        return -1;
    }

    // ============================================================
    // ★★★ 关键参数 ★★★
    // ============================================================
    g_totalDelay = 38;
    g_valveThreshold = 3;
    g_centerValveInflate = 0;
    g_valveThresholdRatio = 75;
    g_frameActivateThreshold = 7;

    g_unifyEnable = true;
    g_unifyTailFrames = 24;
    g_unifyForceClassId = 1;
    g_unifyThreshold = 0.30f;
    g_unifyFillBackground = false;
    g_unifyUseGpu = true;

    std::cout << "[Mode] v4.5 固定迭代 + 分段计时 + 优先级提升"
        << "  g_totalDelay=" << g_totalDelay << "ms"
        << "  g_valveThresholdRatio=" << g_valveThresholdRatio << "%"
        << "  g_frameActivateThreshold=" << g_frameActivateThreshold
        << std::endl;
    std::cout << "[Unify] enable=" << (g_unifyEnable ? "ON" : "OFF")
        << "  K=" << g_unifyTailFrames
        << "  forceClassId=" << g_unifyForceClassId
        << "  threshold=" << g_unifyThreshold
        << std::endl;

    // ★ v4.5 启动横幅写到 perf.log
    PERF_LOG("==== Run start: dealCount=%d Samples=%d bands=%d totalDelay=%d ====",
        dealCount, g_Samples, g_bands, (int)g_totalDelay);

    // 6) 初始化阀控
    // ★ 阀控通信方式开关: 默认 UDP(千兆网接口板 192.168.1.1:10100)
    //    如需切回串口, 改成 SetValveCommMode(VALVE_COMM_SERIAL);
    SetValveCommMode(VALVE_COMM_UDP);
    Start_send();
    UnifyReset();

    // ===== 设置 sel_type: 优先读配置工具下发/持久化的 eject_select.json, 否则回退默认 =====
    //   文件格式: {"eject":[1,4]}  表示吹 classid 1 和 4
    //   只对模型 CoreList 里真实存在的 classid 生效 (classid 可能不连续, 如 1/2/4)
    {
        for (int i = 0; i < 12; i++) sel_type[i] = 0;

        string pathEject = pathDoc + "\\SortingExpert\\param\\eject_select.json";
        std::vector<int> ejectIds;
        bool loaded = false;
        try {
            std::ifstream ef(pathEject);
            if (ef.good()) {
                json je;
                ef >> je;
                if (je.contains("eject")) {
                    for (auto& v : je["eject"]) ejectIds.push_back(v.get<int>());
                    loaded = true;
                }
            }
        }
        catch (...) { loaded = false; }

        for (size_t i = 0; i < aiModel.CoreList.size(); i++) {
            int classid = aiModel.CoreList[i].classid;
            if (classid < 1 || classid > 12) continue;

            bool eject;
            if (loaded)
                eject = (std::find(ejectIds.begin(), ejectIds.end(), classid) != ejectIds.end());
            else
                eject = (classid == 1);   // 无配置文件时默认: 只吹 classid 1 (兼容旧行为)

            sel_type[classid - 1] = eject ? 1 : 0;
            std::cout << "[SelType] classid=" << classid
                << " (" << aiModel.CoreList[i].TargetName << ") => "
                << (eject ? "吹" : "不吹") << std::endl;
        }
        std::cout << "[SelType] 吹气来源: "
            << (loaded ? pathEject : std::string("默认(classid==1, 未找到 eject_select.json)"))
            << std::endl;
    }

    // ★ v4.6 sel_type 已确定, 打一份完整参数快照到 perf.log
    LogAllParams("run start");

    std::thread udpReceiveThread(UDP_receive_thread);
    udpReceiveThread.detach();

    // 7) 启动相机采集
    startFlag = true;
    result = Start();
    cout << "分选算法运行中 " << endl;
    goto Menu;

Stop:
    result = Stop();
    startFlag = false;
    StopEventLog();
    result = Disconnect();
    UnifyReset();
    PERF_LOG("==== Run stopped ====");
    if (currentStatus == "0")
        return 0;
    goto Menu;

Menu:
    while (true)
    {
        string x;
        cout << "输入：0 关闭程序; 1 启动; 2 停止 " << endl;
        cin >> x;
        if (x == "1" && currentStatus != "1") {
            currentStatus = "2";
            goto Restart;
        }
        else if (x == "2" && currentStatus != "2") {
            currentStatus = "2";
            goto Stop;
        }
        else if (x == "0") {
            if (currentStatus == "1") {
                currentStatus = "0";
                goto Stop;
            }
            else if (currentStatus == "2") {
                UnifyShutdown();
                PerfLogShutdown();   // ★ v4.5
                return 0;
            }
        }
    }
}

// ============================================================================
// Connect: 枚举并打开相机
// ============================================================================
bool Connect()
{
    vector<Camera*>* cameras = nullptr;
    try {
        cameras = CameraEnumerate::GetCameras(nullptr);
        std::cout << "find " << cameras->size() << " camera" << std::endl;
        if (cameras->size() == 0) {
            std::cout << ("=================(Execution completed=================)") << std::endl;
            string num;
            std::cin >> num;
        }
        for (size_t i = 0; i < cameras->size(); i++) {
            std::cout << "Number" << (i + 1) << "The camera serial number is"
                << (*cameras)[i]->Info->InstrumentSN << std::endl;
        }
    }
    catch (const char* exception) {
        std::cout << exception << std::endl;
        string num;
        std::cin >> num;
        return false;
    }

    Camera::CameraGrabedData = UserGrabedDataEvent;
    std::cout << "Subscribe to receive hyperspectral image events" << std::endl;

    g_camera = (*cameras)[0];
    try {
        g_camera->OpenCamera(nullptr);
        std::cout << "Open the hyperspectral camera)" << std::endl;
    }
    catch (const char* exception) {
        std::cout << exception << std::endl;
        string num;
        std::cin >> num;
        return false;
    }
    return true;
}

// ============================================================================
// Configure: 配置相机参数
// ============================================================================
bool Configure()
{
    try {
        if (g_camera->SetPixelFormat(EnumPixelFormat::Mono14))
            std::cout << "设置像素格式为Mono14" << std::endl;
        else
            std::cout << "设置像素格式失败" << std::endl;

        if (g_camera->SetSampleBinning(EnumBinning::Binning1))
            std::cout << ("设置空间合并为1合并") << std::endl;
        else std::cout << ("设置空间合并失败") << std::endl;

        if (g_camera->SetBandBinning(EnumBinning::Binning1))
            std::cout << ("设置光谱合并为1合并") << std::endl;
        else std::cout << ("设置光谱合并失败") << std::endl;

        if (g_camera->SetGain(aiModel.Gain))
            std::cout << ("设置增益:") << aiModel.Gain << std::endl;
        else std::cout << ("设置增益失败") << std::endl;

        if (g_camera->SetExposureTime(aiModel.ExpTime))
            std::cout << ("设置曝光时间:") << aiModel.ExpTime << std::endl;
        else std::cout << ("设置曝光时间失败") << std::endl;

        if (g_camera->SetTriggerMode(EnumTriggerMode::Off))
            std::cout << ("设置触发模式为内触发") << std::endl;
        else std::cout << ("设置触发模式失败") << std::endl;

        auto bandz = g_camera->GetBandROI()->getBandZones();
        if (aiModel.EndBandIndex - aiModel.StartBandIndex > 2 &&
            aiModel.EndBandIndex - aiModel.StartBandIndex + 1 != g_camera->GetBands()->getValue() &&
            bandz->size() > 0)
        {
            (*bandz)[0]->setEnable(true);
            auto ofst = (*bandz)[0]->getOffset();
            int offsetInc = ofst->getInc();
            int offmod = aiModel.StartBandIndex % offsetInc;
            int offset = aiModel.StartBandIndex - offmod;
            ofst->setValue(offset);
            (*bandz)[0]->setOffset(ofst);

            auto sz = (*bandz)[0]->getSize();
            int size = aiModel.EndBandIndex - offset + 1;
            int szInc = sz->getInc();
            int szMod = size % szInc;
            size = szMod == 0 ? size : size + szInc - szMod;
            sz->setValue(size);
            (*bandz)[0]->setSize(sz);

            if (g_camera->SetBandROI((*bandz)[0]))
                std::cout << ("设置工作模式为bank模式") << std::endl;
            else std::cout << ("设置工作模式失败") << std::endl;
        }

        frameq = aiModel.FrameRate;
        if (g_camera->SetAcquisitionFrameRate(frameq))
            std::cout << ("设置帧频") << frameq << std::endl;
        else std::cout << ("设置帧频失败") << std::endl;
    }
    catch (const char* exception) {
        std::cout << exception << std::endl;
        string num;
        std::cin >> num;
        return false;
    }
    return true;
}

// ============================================================================
// 相机数据回调
// ============================================================================
void UserGrabedDataEvent(GrabData* grabData)
{
    grabDatas.enqueue(grabData);
    frameTickQueue.enqueue(NowMs());

    if (grabDatas.size() >= dealCount) {
        SetEvent(hEventSort);
    }
}

// ============================================================================
// 编译开关
// ============================================================================
#define OUTPUT_SORTING_RESULT
#define GPU_MODE

// ============================================================================
// GPU 处理线程 (v4.5: 加分段计时 + 优先级提升)
// ============================================================================
void threadFunction()
{
    // ★ v4.5 提升优先级, 减少被 Windows 抢占的概率
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    PERF_LOG("[threadFunction] started, priority=TIME_CRITICAL");

    cudaError_t cudaStatus;

    char* hostPinnedFrameBuf = nullptr;
    size_t frameBufSize = (size_t)dealCount * g_Samples * g_bands * 2;
    cudaStatus = cudaMallocHost((void**)&hostPinnedFrameBuf, frameBufSize);
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "[ERROR] cudaMallocHost 失败: %s\n", cudaGetErrorString(cudaStatus));
        PERF_LOG("[threadFunction][ERROR] cudaMallocHost failed: %s",
            cudaGetErrorString(cudaStatus));
        return;
    }
    fprintf(stderr, "[GPU] pinned frame buffer 分配成功: %zu bytes\n", frameBufSize);

    const int TAG_BUF_COUNT = 8;
    static char* tagBufs[TAG_BUF_COUNT] = { nullptr };
    int tagBufIdx = 0;
    for (int i = 0; i < TAG_BUF_COUNT; i++) {
        tagBufs[i] = (char*)malloc(dealCount * g_Samples);
        if (!tagBufs[i]) {
            fprintf(stderr, "[ERROR] tag buffer 分配失败\n");
            return;
        }
    }

    static FrameTicks tickBufs[TAG_BUF_COUNT];
    int tickBufIdx = 0;

    while (startFlag) {
        WaitForSingleObject(hEventSort, INFINITE);

        int count = grabDatas.size();
        if (count >= dealCount) {
#ifdef GPU_MODE
            GrabData* data;
            bool copyError = false;

            FrameTicks* fts = &tickBufs[tickBufIdx];
            tickBufIdx = (tickBufIdx + 1) % TAG_BUF_COUNT;

            // ===== 段 1: 从队列取帧 + memcpy 到 pinned host buffer =====
            const int frameSize = g_Samples * g_bands * 2;
            for (size_t i = 0; i < (size_t)dealCount; i++) {
                grabDatas.dequeue(data);
                unsigned long long frameTick = 0;
                frameTickQueue.dequeue(frameTick);
                fts->t[i] = frameTick;

                if (data == nullptr || data->Values == nullptr) {
                    fprintf(stderr, "[ERROR] 第 %zu 帧数据指针为空\n", i);
                    PERF_LOG("[threadFunction][ERROR] frame ptr null at i=%zu", i);
                    copyError = true;
                    break;
                }
                memcpy(hostPinnedFrameBuf + i * frameSize,
                    data->Values, frameSize);
                if (i == 0) {
                    firstByteList.enqueue(data->Values[0]);
                }
                delete data;
            }
            if (copyError) goto Error;

            // ===== 段 2: H2D cudaMemcpy =====
            cudaStatus = cudaMemcpy(cuda_frameData, hostPinnedFrameBuf,
                frameBufSize, cudaMemcpyHostToDevice);

            if (cudaStatus != cudaSuccess) {
                static bool firstError = true;
                if (firstError) {
                    fprintf(stderr, "\n[ERROR] cudaMemcpy(H2D) 失败！%s\n",
                        cudaGetErrorString(cudaStatus));
                    firstError = false;
                }
                PERF_LOG("[threadFunction][ERROR] cudaMemcpy H2D failed: %s",
                    cudaGetErrorString(cudaStatus));
                goto Error;
            }

            // ===== 段 3: calc_GPU (内部有 cudaDeviceSynchronize) =====
            calc_GPU(cuda_frameData, cuda_tempData, cuda_outputData,
                cuda_k_White, cuda_White_ReadBytes, cuda_Black_ReadBytes,
                cuda_otherParams, cuda_preprocessList, cuda_modelWaveList,
                cuda_Intercept, cuda_Coef, cuda_StdX, cuda_MeanX, cuda_tags);

            // ===== 段 4: D2H cudaMemcpy =====
            char* sortResult = tagBufs[tagBufIdx];
            tagBufIdx = (tagBufIdx + 1) % TAG_BUF_COUNT;

            cudaStatus = cudaMemcpy(sortResult, cuda_tags,
                (size_t)dealCount * g_Samples,
                cudaMemcpyDeviceToHost);

            if (cudaStatus != cudaSuccess) {
                fprintf(stderr, "[ERROR] cudaMemcpy(D2H) 失败\n");
                PERF_LOG("[threadFunction][ERROR] cudaMemcpy D2H failed: %s",
                    cudaGetErrorString(cudaStatus));
                goto Error;
            }

#ifdef OUTPUT_SORTING_RESULT
            sortResultDatas.enqueue(sortResult);
            batchFrameTicksQueue.enqueue(fts);
            SetEvent(hEvent);

            g_batchNo.fetch_add(1);
#endif
#endif
        }
    Error:
        ;
    }

#ifdef GPU_MODE
    clearGpu();
#endif
    if (hostPinnedFrameBuf) {
        cudaFreeHost(hostPinnedFrameBuf);
    }
    for (int i = 0; i < TAG_BUF_COUNT; i++) {
        if (tagBufs[i]) free(tagBufs[i]);
    }
    PERF_LOG("[threadFunction] exited");
}

// ============================================================================
// 分发线程 (v4.5: 优先级提升 + 分段计时走 PerfLog)
// ============================================================================
void threadFunction_dis()
{
    // ★ v4.5 优先级
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    PERF_LOG("[threadFunction_dis] started, priority=TIME_CRITICAL");

    const int UNIFY_BUF_COUNT = 4;
    static char* unifyBufs[UNIFY_BUF_COUNT] = { nullptr };
    int unifyBufIdx = 0;
    for (int i = 0; i < UNIFY_BUF_COUNT; i++) {
        unifyBufs[i] = (char*)malloc(dealCount * g_Samples);
        if (!unifyBufs[i]) {
            fprintf(stderr, "[ERROR] unify buffer 分配失败\n");
            return;
        }
    }

    while (startFlag)
    {
        WaitForSingleObject(hEvent, INFINITE);
        if (sortResultDatas.size() > 0)
        {
            char* tagBuf = nullptr;
            FrameTicks* fts = nullptr;

            sortResultDatas.dequeue(tagBuf);
            firstByteList.dequeue(firstByte);
            batchFrameTicksQueue.dequeue(fts);

            char* outBuf = unifyBufs[unifyBufIdx];
            unifyBufIdx = (unifyBufIdx + 1) % UNIFY_BUF_COUNT;

            UnifyProcess(tagBuf, outBuf, dealCount, g_Samples);

            for (int i = 0; i < dealCount; i++) {
                const char* frameTags = outBuf + i * g_Samples;
                ProcessOneFrame(frameTags, fts->t[i]);
            }
        }
    }

    for (int i = 0; i < UNIFY_BUF_COUNT; i++) {
        if (unifyBufs[i]) free(unifyBufs[i]);
    }
    PERF_LOG("[threadFunction_dis] exited");
}

// ============================================================================
// Start / Stop / Disconnect
// ============================================================================
bool Start()
{
    try {
        if (g_camera->getOpen()) {
            g_camera->StartGrab();
            std::cout << ("开始采集高光谱数据") << std::endl;
        }
    }
    catch (const char* exception) {
        return false;
    }
    try {
        std::thread t1(threadFunction);
        t1.detach();
        std::thread t2(threadFunction_dis);
        t2.detach();
    }
    catch (const char* exception) {
        return false;
    }
    return true;
}

bool Stop()
{
    try {
        g_camera->StopGrab();
        std::cout << ("停止采集高光谱数据") << std::endl;
    }
    catch (const char* exception) {
        return false;
    }
    return true;
}

bool Disconnect()
{
    try {
        g_camera->CloseCamera();
        std::cout << ("关闭高光谱相机") << std::endl;
    }
    catch (const char* exception) {
        return false;
    }
    return true;
}