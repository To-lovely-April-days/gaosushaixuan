// ============================================================================
// 文件名：main.cpp
// 作用  ：工业分选程序的主入口（v2 - 按帧处理 + 按帧补偿）
//
// v2 重大改动（相对旧版）：
//   1. GPU 线程：保留全部 50 帧时刻；用 pinned memory + 单次 cudaMemcpy
//      tag 用 3 缓冲环形复用（不每批 malloc）
//   2. 分发线程：出队 1 次后逐帧处理（for i = 0..49 调 ProcessOneFrame）
//   3. 删除：批中点延迟模式、5x5/膨胀相关参数、sort_stats.log（IO 浪费）
// ============================================================================

#include <stdio.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <queue>
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

// 来自 Eject.cpp 的全局参数
extern int sel_type[12];
extern int percent[12];   // v2 已不用
extern int fa_ctl;        // v2 已不用

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
// 共享队列（生产者-消费者）
// ============================================================================
LockFreeQueue<GrabData*>          grabDatas;          // 相机 → GPU 线程
LockFreeQueue<unsigned long long> frameTickQueue;     // 每帧时刻，与 grabDatas 一一对应

// ===== v2 新设计：tag 缓冲 + 帧时刻打包 =====
//
// FrameTicks: 一批 50 帧的时刻打包传递（用指针，避免 vector 堆分配）
struct FrameTicks { unsigned long long t[64]; };   // 64 大于 dealCount=50 上限，留富余

LockFreeQueue<char*>        sortResultDatas;            // GPU → 分发：tag 缓冲指针（指向环形缓冲）
LockFreeQueue<FrameTicks*>  batchFrameTicksQueue;       // GPU → 分发：50 个帧时刻指针

// ===== 心跳字节队列（保留）=====
LockFreeQueue<char> firstByteList;

// ============================================================================
// 同步事件句柄
// ============================================================================
HANDLE hEvent = NULL;   // 通知分发线程有新结果
HANDLE hEventSort = NULL;   // 通知 GPU 线程数据足够

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
    // ★ 提升进程优先级（之前测过非常有效，把 postGpu 抖动从 50ms 压到 < 1ms）★
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    StartEventLog("event.log");

    hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
    hEventSort = CreateEvent(NULL, FALSE, TRUE, NULL);

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
    // ★★★ 关键参数（v2）★★★
    //
    //   g_totalDelay     : 总飞行时间（ms）。按帧补偿后 elapsed 范围扩大，
    //                      建议从 45ms 起步，根据实测调整。
    //                      调试方法：跑一段看 FIREF 日志的 elapsed 字段，
    //                      g_totalDelay 至少要 ≥ elapsed 的 95 分位值才能基本不 LATE。
    //
    //   g_valveThreshold : 阀触发阈值。1 = 任意 1 个像素命中就吹（最敏感，等同旧逻辑）
    //                      2 = 至少 2 个像素命中才吹（中等抗噪）
    //                      3 = 全部 3 个像素都命中（最严格）
    // ============================================================
    g_totalDelay = 38;
    g_valveThreshold = 3;
    g_centerValveInflate = 0;
    g_valveThresholdRatio = 75;          // 75% 比例阈值
    g_frameActivateThreshold = 5;        // ★ v3.3 新增：帧激活阈值（< 5 像素的帧整帧不吹）

    std::cout << "[Mode] v3.3 按帧 + 比例阈值 + 帧激活"
        << "  g_totalDelay=" << g_totalDelay << "ms"
        << "  g_valveThresholdRatio=" << g_valveThresholdRatio << "%"
        << "  g_frameActivateThreshold=" << g_frameActivateThreshold
        << "  g_centerValveInflate=" << g_centerValveInflate
        << std::endl;

    // 6) 初始化阀控（打开串口 + 启动时建阀号映射表）
    Start_send();

    // ===== 自动设置 sel_type：模型里有几个 classid，就剔除几个 =====
    //for (size_t i = 0; i < aiModel.CoreList.size(); i++) {
    //    int classid = aiModel.CoreList[i].classid;
    //    if (classid == 1) {                  // ← 只剔除 classid=1
    //        sel_type[classid - 1] = 1;
    //        std::cout << "[Auto] sel_type[" << (classid - 1)
    //            << "] = 1  (classid=" << classid
    //            << " " << aiModel.CoreList[i].TargetName << " 将被剔除)" << std::endl;
    //    }
    //    else {
    //        std::cout << "[Auto] 跳过 classid=" << classid
    //            << " (" << aiModel.CoreList[i].TargetName << " 不剔除)" << std::endl;
    //    }
    //}

    /*for (size_t i = 0; i < aiModel.CoreList.size(); i++) {
        int classid = aiModel.CoreList[i].classid;
        if (classid >= 1 && classid <= 12) {
            sel_type[classid - 1] = 1;
            std::cout << "[Auto] sel_type[" << (classid - 1)
                << "] = 1  (classid=" << classid
                << " " << aiModel.CoreList[i].TargetName << " 将被剔除)" << std::endl;
        }
    }*/
    for (size_t i = 0; i < aiModel.CoreList.size(); i++) {
        int classid = aiModel.CoreList[i].classid;
        if (classid == 1) {                          // ← 只剔除 classid=1
            sel_type[classid - 1] = 1;
            std::cout << "[Auto] sel_type[" << (classid - 1)
                << "] = 1  (classid=" << classid
                << " " << aiModel.CoreList[i].TargetName << " 将被剔除)" << std::endl;
        }
        else {
            std::cout << "[Auto] 跳过 classid=" << classid
                << " (" << aiModel.CoreList[i].TargetName << " 不剔除)" << std::endl;
        }
    }
    //// ===== 强制写死（保证 sel_type 是这个值）=====
    //sel_type[0] = 1;   // classid=1 剔除
    //sel_type[1] = 0;   // classid=2 不剔除
    // v2 已不再使用 percent[] 和 fa_ctl，留空即可
    // ============================================================

    // 启动 UDP 接收线程
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
// GPU 处理线程（v2 优化版）
//
// 优化点：
//   ② 主机端 pinned memory + 单次大 cudaMemcpy（替代 50 次小 cudaMemcpy）
//   ③ tag 用 3 缓冲环形复用（不每批 malloc/free）
//   ④ FrameTicks 用固定数组指针 + 3 缓冲环形复用
//
// 流程：
//   1. 等够 50 帧
//   2. 出队 50 帧 → 拷到主机 pinned 大缓冲（同时记录 50 个时刻）
//   3. 一次 cudaMemcpy 把 50 帧整批送到 GPU
//   4. calc_GPU
//   5. cudaMemcpy 拷回 tags 到环形缓冲槽
//   6. 把 tag 槽指针 + frameTicks 槽指针入队
//   7. SetEvent 唤醒分发线程
// ============================================================================
void threadFunction()
{
    cudaError_t cudaStatus;

    // ===== 主机端 pinned memory（一次性分配，永久持有）=====
    // GPU 拷贝从 pinned 内存比 pageable 快 30~50%
    char* hostPinnedFrameBuf = nullptr;
    size_t frameBufSize = (size_t)dealCount * g_Samples * g_bands * 2;
    cudaStatus = cudaMallocHost((void**)&hostPinnedFrameBuf, frameBufSize);
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "[ERROR] cudaMallocHost 失败: %s\n", cudaGetErrorString(cudaStatus));
        return;
    }
    fprintf(stderr, "[GPU] pinned frame buffer 分配成功: %zu bytes\n", frameBufSize);

    // ===== tag 环形缓冲（8 槽，避免每批 malloc）=====
    // 8 槽足以吸收分发线程偶发慢一下的抖动（比如串口卡 50ms 也只是积压 4 槽左右）
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

    // ===== FrameTicks 环形缓冲（8 槽，跟 tag 缓冲对齐）=====
    static FrameTicks tickBufs[TAG_BUF_COUNT];
    int tickBufIdx = 0;

    while (startFlag) {
        WaitForSingleObject(hEventSort, INFINITE);

        int count = grabDatas.size();
        if (count >= dealCount) {
#ifdef GPU_MODE
            GrabData* data;
            bool copyError = false;

            // 当前批用哪一个 tick 槽
            FrameTicks* fts = &tickBufs[tickBufIdx];
            tickBufIdx = (tickBufIdx + 1) % TAG_BUF_COUNT;

            // ===== Step A: 出队 50 帧，主机端 memcpy 到 pinned 大缓冲 =====
            const int frameSize = g_Samples * g_bands * 2;
            for (size_t i = 0; i < (size_t)dealCount; i++) {
                grabDatas.dequeue(data);

                // 同步出队帧时间戳
                unsigned long long frameTick = 0;
                frameTickQueue.dequeue(frameTick);
                fts->t[i] = frameTick;

                if (data == nullptr || data->Values == nullptr) {
                    fprintf(stderr, "[ERROR] 第 %zu 帧数据指针为空\n", i);
                    copyError = true;
                    break;
                }

                // 主机端 memcpy（极快，几 us）
                memcpy(hostPinnedFrameBuf + i * frameSize,
                    data->Values, frameSize);

                if (i == 0) {
                    firstByteList.enqueue(data->Values[0]);
                }
                delete data;
            }

            if (copyError) goto Error;

            // ===== Step B: 一次大 cudaMemcpy（替代 50 次小拷贝）=====
            cudaStatus = cudaMemcpy(cuda_frameData,
                hostPinnedFrameBuf,
                frameBufSize,
                cudaMemcpyHostToDevice);
            if (cudaStatus != cudaSuccess) {
                static bool firstError = true;
                if (firstError) {
                    fprintf(stderr, "\n[ERROR] cudaMemcpy(整批) 失败！\n");
                    fprintf(stderr, "  错误码: %d  描述: %s\n",
                        cudaStatus, cudaGetErrorString(cudaStatus));
                    fprintf(stderr, "  期望大小: %zu bytes\n", frameBufSize);
                    firstError = false;
                }
                goto Error;
            }

            // ===== Step C: GPU 计算 =====
            calc_GPU(cuda_frameData, cuda_tempData, cuda_outputData,
                cuda_k_White, cuda_White_ReadBytes, cuda_Black_ReadBytes,
                cuda_otherParams, cuda_preprocessList, cuda_modelWaveList,
                cuda_Intercept, cuda_Coef, cuda_StdX, cuda_MeanX, cuda_tags);

            // ===== Step D: 拷回 tags 到当前环形槽 =====
            char* sortResult = tagBufs[tagBufIdx];
            tagBufIdx = (tagBufIdx + 1) % TAG_BUF_COUNT;

            cudaStatus = cudaMemcpy(sortResult, cuda_tags,
                (size_t)dealCount * g_Samples,
                cudaMemcpyDeviceToHost);
            if (cudaStatus != cudaSuccess) {
                fprintf(stderr, "[ERROR] cudaMemcpy(拷回) 失败\n");
                goto Error;
            }

#ifdef OUTPUT_SORTING_RESULT
            // ===== Step E: 入队（tag 缓冲指针 + tick 缓冲指针）=====
            sortResultDatas.enqueue(sortResult);
            batchFrameTicksQueue.enqueue(fts);
            SetEvent(hEvent);

            int batchId = g_batchNo.fetch_add(1);
            LogDataAcquired(batchId, dealCount, dealCount * g_Samples);
#endif
#endif
        }
    Error:
        ;
    }

    // 退出清理
#ifdef GPU_MODE
    clearGpu();
#endif
    if (hostPinnedFrameBuf) {
        cudaFreeHost(hostPinnedFrameBuf);
    }
    for (int i = 0; i < TAG_BUF_COUNT; i++) {
        if (tagBufs[i]) free(tagBufs[i]);
    }
}

// ============================================================================
// 分发线程（v2 按帧版本）
//
// 流程：
//   1. 等被唤醒
//   2. 出队 1 次：拿到 tag 缓冲指针 + 50 个帧时刻
//   3. for i = 0..49: ProcessOneFrame(第 i 帧的 640 像素 + 第 i 帧时刻)
//
// 注意：tag 缓冲是环形复用的，分发线程"用完"后不需要 delete
//      (下一轮 GPU 线程会覆盖这个槽，靠 3 槽轮转保证不冲突)
// ============================================================================
void threadFunction_dis()
{
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

            // 监控队列积压（每 100 批打印一次）
            static int monitorCnt = 0;
            if (++monitorCnt % 100 == 0) {
                std::cout << "[QUEUE] grabDatas=" << grabDatas.size()
                    << "  sortResultDatas=" << sortResultDatas.size()
                    << "  batchFrameTicksQueue=" << batchFrameTicksQueue.size()
                    << std::endl;
            }

            // ★ 逐帧处理：50 帧依次走 ProcessOneFrame ★
            // 每帧用自己的拍摄时刻算 actualDelay
            // 每帧的多条命令打包成一次 WriteFile（性能优化⑦）
            for (int i = 0; i < dealCount; i++) {
                const char* frameTags = tagBuf + i * g_Samples;
                ProcessOneFrame(frameTags, fts->t[i]);
            }

            // 不需要 delete tagBuf（环形缓冲，由 GPU 线程持有）
            // 不需要 delete fts（环形缓冲，由 GPU 线程持有）
        }
    }
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