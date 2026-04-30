// ============================================================================
// 文件名：main.cpp
// 作用  ：工业分选程序的主入口
//         流程：连接相机 → 加载模型/校准 → 配置相机参数 → 初始化 GPU →
//               启动采集 → 多线程并行处理（GPU 计算 + 结果分发）→ 用户控制启停
// ============================================================================

#include <stdio.h>
#include <iostream>
#include <thread>           // C++11 多线程
#include <thread>           // (重复 include，无害；可清理)
#include <mutex>            // 互斥锁（本文件未直接使用，预留）
#include <chrono>           // 时间测量
#include <queue>            // 标准队列（实际使用的是无锁队列 LockFreeQueue）
#include <chrono>           // (重复 include，无害；可清理)
#include "windows.h"        // Windows API：事件、句柄、控制台等
#include <ShlObj.h>         // SHGetFolderPathA：获取"我的文档"路径

// ===== 相机 SDK 头文件 =====
#include "GrabData.h"           // 相机采集数据帧结构
#include "Camera.h"             // 相机类
#include "CameraEnumerate.h"    // 相机枚举（查找连接的相机）
#include "CameraInfo.h"         // 相机信息

// ===== 项目自有模块 =====
#include "kernel.h"             // GPU 计算核函数与 GPU 内存管理
#include "PlsAI.h"              // PLS-DA 模型（单个分类核）
#include "CalibrationInfo.h"    // 黑白校准数据
#include "LockFreeQueue.h"      // 无锁队列（生产者-消费者模式）
#include "AIModel.h"            // 完整 AI 模型（含相机参数+多个 PLS 核）
#include "Eject.h"              // 阀控通信（UDP/串口）

using namespace std;
using namespace BaseCamera;     // 相机 SDK 命名空间
using namespace FactoryCameras; // 相机工厂命名空间

// ============================================================================
// 全局状态变量
// ============================================================================

// 当前程序状态："1"=运行中  "2"=已停止  "0"=准备退出
string currentStatus = "1";

// 分选状态标志：true=正在采集分选，false=已停止
// 工作线程靠它判断是否退出循环
bool startFlag = false;

// 相机对象指针（连接成功后赋值）
Camera* g_camera = nullptr;

// 相机列表指针（暂存枚举结果，本文件中未使用）
vector<CameraInfo*>* g_cameraInfos = nullptr;

// 相机分辨率参数
int g_Samples = 640;   // 每帧像素数（空间方向）
int g_bands = 102;   // 每帧波段数（光谱方向）

// AI 模型与校准数据
AIModel aiModel;                  // 从 modeCpp.models 文件加载
CalibrationInfo calibrationInfo;  // 从 modelCpp.calibrationInfo 文件加载

// 一次批量处理的帧数（GPU 一次算 dealCount 帧的数据）
int dealCount = 50;

// 相机帧率（从模型文件读取，传给 Eject 模块用于计算帧周期）
int frameq;

// ============================================================================
// 共享队列（生产者-消费者）
// ============================================================================

// 相机采集到的原始数据帧 → 由 GPU 处理线程消费
LockFreeQueue<GrabData*> grabDatas;

// 分选结果 → 由结果分发线程消费（写日志或发给 Eject）
LockFreeQueue<char*> sortResultDatas;

// 分选耗时记录（保留接口，本文件未消费）
LockFreeQueue<float> sortResultTime;

// 每批次首帧的第一个字节，用作给 FPGA 板的心跳/同步标识
LockFreeQueue<char> firstByteList;

// ============================================================================
// 函数前置声明（实现在文件下方）
// ============================================================================
bool Connect();      // 连接相机
bool Configure();    // 配置相机参数
bool Start();        // 启动采集
bool Stop();         // 停止采集
bool Disconnect();   // 关闭相机

// 相机数据回调（订阅事件后由 SDK 调用）
void UserGrabedDataEvent(GrabData* grabData);

// ============================================================================
// 同步事件句柄
// ============================================================================
HANDLE hEvent = NULL;   // 通知"分发线程"有新结果可处理
HANDLE hEventSort = NULL;   // 通知"GPU 处理线程"有足够数据可处理

// ============================================================================
// 控制台窗口关闭事件处理
// 用户点关闭按钮时，先停止采集再退出，避免设备资源未释放
// ============================================================================
BOOL CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
    case CTRL_CLOSE_EVENT:
        std::cout << "控制台窗口正在关闭...\n";
        if (startFlag)
        {
            bool result = Stop();        // 停止采集
            result = Disconnect();        // 关闭相机
        }
        return TRUE;   // 返回 TRUE 表示已处理该事件
    default:
        return FALSE;
    }
}

// ============================================================================
// 获取"我的文档"路径
// 模型文件、校准文件保存在 "我的文档\SortingExpert\param\" 下
// ============================================================================
string GetDocumentPath()
{
    char path[MAX_PATH];
    // CSIDL_PERSONAL 即 "我的文档"
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
    // 注册控制台关闭回调（用户点 X 按钮时执行清理）
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    // 创建两个事件对象（手动复位 = FALSE，初始有信号 = TRUE）
    // 用于线程间同步：唤醒等待的工作线程
    hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
    hEventSort = CreateEvent(NULL, FALSE, TRUE, NULL);

    cout << "正在启动分选程序... " << endl;

Restart:   // 标签：重启时跳回这里
    bool result;

    // 1) 连接相机（枚举 + 打开）
    result = Connect();

    // 2) 加载 AI 模型文件
    string pathDoc = GetDocumentPath();
    string pathMode = pathDoc + "\\SortingExpert\\param\\modeCpp.models";
    std::ifstream inputFile(pathMode);
    json jRead;
    inputFile >> jRead;            // 解析 JSON
    aiModel.from_json(jRead);      // 反序列化到 AIModel 对象
    inputFile.close();

    // 3) 加载黑白校准数据
    string pathCali = pathDoc + "\\SortingExpert\\param\\modelCpp.calibrationInfo";
    calibrationInfo.loadData(pathCali);

    // 4) 配置相机参数（曝光、增益、ROI、帧率等）
    result = Configure();
    g_Samples = g_camera->GetSamples()->getValue();   // 实际像素数
    g_bands = g_camera->GetBands()->getValue();     // 实际波段数

    // 5) 初始化 GPU（分配显存 + 拷贝模型/校准数据到显存）
    result = initGpu();
    if (!result) {
        std::cout << "[ERROR] GPU 初始化失败！请检查上方日志" << std::endl;
        std::cout << "按任意键退出..." << std::endl;
        std::string num;
        std::cin >> num;
        return -1;
    }

    // 6) 初始化 Eject 模块（UDP 发送配置包 + 启动接收线程）
    Start_send();                                          // 发初始化包
    std::thread udpReceiveThread(UDP_receive_thread);
    udpReceiveThread.detach();                             // 分离线程

    // 7) 启动相机采集（数据通过回调进入 grabDatas 队列）
    startFlag = true;
    result = Start();
    cout << "分选算法运行中 " << endl;
    goto Menu;   // 跳到菜单等待用户输入

Stop:     // 标签：停止时跳到这里
    result = Stop();              // 停止采集
    startFlag = false;             // 通知工作线程退出
    result = Disconnect();         // 关闭相机
    if (currentStatus == "0")
    {
        return 0;                  // 完全退出
    }
    goto Menu;

Menu:     // 标签：菜单交互
    while (true)
    {
        string x;
        cout << "输入：0 关闭程序; 1 启动; 2 停止 " << endl;
        cin >> x;
        if (x == "1" && currentStatus != "1")
        {
            currentStatus = "2";   // 注意：这里赋值看起来有逻辑问题（应为"1"），保留原代码
            goto Restart;
        }
        else if (x == "2" && currentStatus != "2")
        {
            currentStatus = "2";
            goto Stop;
        }
        else if (x == "0")
        {
            if (currentStatus == "1")
            {
                currentStatus = "0";
                goto Stop;          // 先停止再退出
            }
            else if (currentStatus == "2")
            {
                return 0;           // 已停止状态直接退出
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
    try
    {
        // 枚举电脑上连接的所有相机（GigE/USB 等）
        cameras = CameraEnumerate::GetCameras(nullptr);
        std::cout << "find " << cameras->size() << " camera" << std::endl;
        if (cameras->size() == 0)
        {
            // 没找到相机：暂停等待用户查看错误
            std::cout << ("=================(Execution completed=================)") << std::endl;
            string num;
            std::cin >> num;
        }
        // 打印每个相机的序列号
        for (size_t i = 0; i < cameras->size(); i++)
        {
            std::cout << "Number" << (i + 1) << "The camera serial number is"
                << (*cameras)[i]->Info->InstrumentSN << std::endl;
        }
    }
    catch (const char* exception)
    {
        std::cout << exception << std::endl;
        string num;
        std::cin >> num;
        return false;
    }

    // 订阅"采集到一帧数据"事件，回调到 UserGrabedDataEvent
    Camera::CameraGrabedData = UserGrabedDataEvent;
    std::cout << "Subscribe to receive hyperspectral image events" << std::endl;

    // 默认使用第一台相机
    g_camera = (*cameras)[0];
    try
    {
        g_camera->OpenCamera(nullptr);   // 打开相机（建立通信）
        std::cout << "Open the hyperspectral camera)" << std::endl;
    }
    catch (const char* exception)
    {
        std::cout << exception << std::endl;
        string num;
        std::cin >> num;
        return false;
    }
    return true;
}

// ============================================================================
// Configure: 设置相机参数
// 参数全部从 aiModel 中读取（建模时保存的相机配置）
// ============================================================================
bool Configure()
{
    try
    {
        // 像素格式：Mono14（14 位灰度，每像素 2 字节）
        if (g_camera->SetPixelFormat(EnumPixelFormat::Mono14))
            std::cout << "设置像素格式为Mono14" << std::endl;
        else
            std::cout << "设置像素格式失败" << std::endl;

        // 空间合并：1 合并（不合并，保持原分辨率）
        if (g_camera->SetSampleBinning(EnumBinning::Binning1))
            std::cout << ("设置空间合并为1合并") << std::endl;
        else std::cout << ("设置空间合并失败") << std::endl;

        // 光谱合并：1 合并（不合并）
        if (g_camera->SetBandBinning(EnumBinning::Binning1))
            std::cout << ("设置光谱合并为1合并") << std::endl;
        else std::cout << ("设置光谱合并失败") << std::endl;

        // 增益：0=高 1=中 2=低
        if (g_camera->SetGain(aiModel.Gain))
            std::cout << ("设置增益:") << aiModel.Gain << std::endl;
        else std::cout << ("设置增益失败") << std::endl;

        // 曝光时间（微秒），来自模型文件
        if (g_camera->SetExposureTime(aiModel.ExpTime))
            std::cout << ("设置曝光时间:") << aiModel.ExpTime << std::endl;
        else std::cout << ("设置曝光时间失败") << std::endl;

        // 触发模式：Off = 内触发（自由运行）
        if (g_camera->SetTriggerMode(EnumTriggerMode::Off))
            std::cout << ("设置触发模式为内触发") << std::endl;
        else std::cout << ("设置触发模式失败") << std::endl;

        // 设置波段 ROI（只采集模型用到的波段范围，提高帧率）
        auto bandz = g_camera->GetBandROI()->getBandZones();
        if (aiModel.EndBandIndex - aiModel.StartBandIndex > 2 &&
            aiModel.EndBandIndex - aiModel.StartBandIndex + 1 != g_camera->GetBands()->getValue() &&
            bandz->size() > 0)
        {
            (*bandz)[0]->setEnable(true);   // 启用第一个 ROI 区域

            // 设置起始波段（必须是 inc 的整数倍，向下取整）
            auto ofst = (*bandz)[0]->getOffset();
            int offsetInc = ofst->getInc();
            int offmod = aiModel.StartBandIndex % offsetInc;
            int offset = aiModel.StartBandIndex - offmod;
            ofst->setValue(offset);
            (*bandz)[0]->setOffset(ofst);

            // 设置波段长度（必须是 inc 的整数倍，向上取整）
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

        // 设置帧率（影响传送带速度匹配）
        frameq = aiModel.FrameRate;
        if (g_camera->SetAcquisitionFrameRate(frameq))
            std::cout << ("设置帧频") << frameq << std::endl;
        else std::cout << ("设置帧频失败") << std::endl;
    }
    catch (const char* exception)
    {
        std::cout << exception << std::endl;
        string num;
        std::cin >> num;
        return false;
    }
    return true;
}

// ============================================================================
// 相机数据回调
// 相机 SDK 每采集到一帧就调用此函数（由 SDK 内部线程驱动）
// ============================================================================
void UserGrabedDataEvent(GrabData* grabData)
{
    // 入队（生产者）
    grabDatas.enqueue(grabData);

    // 凑够 dealCount 帧时，唤醒 GPU 处理线程
    if (grabDatas.size() >= dealCount)
    {
        SetEvent(hEventSort);
    }
}

// ============================================================================
// 编译开关
// ============================================================================
//#define TIME_RECORD             // 打印每段耗时
#define OUTPUT_SORTING_RESULT     // 启用结果分发
#define GPU_MODE                  // 走 GPU 路径（推荐）
//#define CPU_MODE                // 走 CPU 路径（仅测试用）

// ============================================================================
// GPU 处理线程：消费 grabDatas，调用 calc_GPU 分选，结果入 sortResultDatas
// ============================================================================
void threadFunction()
{
    try
    {
#ifdef CPU_MODE
        // CPU 模式：分配主机内存做相同计算
        char* cpu_frameData;
        cpu_frameData = (char*)malloc(dealCount * g_Samples * g_bands * 2 * sizeof(char));
        float* cpu_outputData;
        cpu_outputData = (float*)malloc(dealCount * g_Samples * g_bands * sizeof(float));
        float* cpu_tempData;
        cpu_tempData = (float*)malloc(dealCount * g_Samples * g_bands * sizeof(float));
        char* tags;
        tags = (char*)malloc(dealCount * g_Samples * sizeof(char));

        // CPU 模式参数（与 GPU 模式 OtherParams 类似）
        OtherParams* cpu_lOtherParams = new OtherParams;
        cpu_lOtherParams->waveLength = g_bands;
        cpu_lOtherParams->startBandIndex = aiModel.StartBandIndex;
        cpu_lOtherParams->endBandIndex = aiModel.EndBandIndex;
        cpu_lOtherParams->BandsCount = aiModel.CoreList[0].selectIndex.size();
        cpu_lOtherParams->Threshold = aiModel.CoreList[0].threshold;
        cpu_lOtherParams->RoiBandStartIndex = aiModel.RoiBandStartIndex;
        cpu_lOtherParams->LimitScopeFlag = aiModel.LimitScopeFlag;
        cpu_lOtherParams->LowestValue = aiModel.LowestValue;
        cpu_lOtherParams->HighestValue = aiModel.HighestValue;
        cpu_lOtherParams->PreprocessCount = aiModel.Preprocessings.size();
        cpu_lOtherParams->FilterStrength = aiModel.FilterStrength;
#endif
        cudaError_t cudaStatus;

        while (startFlag)
        {
            // 阻塞等待数据足够事件
            WaitForSingleObject(hEventSort, INFINITE);

            int count = grabDatas.size();
            if (count >= dealCount)
            {
#ifdef GPU_MODE
                if (true)
                {
                    GrabData* data;
                    bool copyError = false;
                    // 从队列出队 dealCount 帧，依次拷贝到 GPU 显存
                    for (size_t i = 0; i < dealCount; i++)
                    {
                        grabDatas.dequeue(data);

                        // 校验 data 指针不为空
                        if (data == nullptr || data->Values == nullptr) {
                            fprintf(stderr, "[ERROR] 第 %zu 帧数据指针为空\n", i);
                            copyError = true;
                            break;
                        }

                        cudaStatus = cudaMemcpy(
                            cuda_frameData + i * g_Samples * g_bands * 2,
                            data->Values,
                            g_Samples * g_bands * 2 * sizeof(char),
                            cudaMemcpyHostToDevice);

                        // 记录第一帧的首字节作心跳
                        if (i == 0)
                        {
                            firstByteList.enqueue(data->Values[0]);
                        }
                        delete data;   // 释放原始帧（内存所有权转移完成）
                        if (cudaStatus != cudaSuccess)
                        {
                            // 打印详细错误（只在第一次错误时打印，避免刷屏）
                            static bool firstError = true;
                            if (firstError) {
                                fprintf(stderr, "\n[ERROR] cudaMemcpy 失败！\n");
                                fprintf(stderr, "  错误码: %d\n", cudaStatus);
                                fprintf(stderr, "  错误描述: %s\n", cudaGetErrorString(cudaStatus));
                                fprintf(stderr, "  当前帧序号: %zu/%d\n", i, dealCount);
                                fprintf(stderr, "  期望拷贝大小: %d bytes\n", g_Samples * g_bands * 2);
                                fprintf(stderr, "  cuda_frameData 指针: %p\n", (void*)cuda_frameData);
                                fprintf(stderr, "  data->Values 指针: %p\n", (void*)data);
                                fprintf(stderr, "  g_Samples=%d, g_bands=%d\n", g_Samples, g_bands);
                                firstError = false;
                            }
                            copyError = true;
                            goto Error;
                        }
                    }

                    if (copyError) goto Error;

#ifdef TIME_RECORD
                    std::cout << "Data copy time: " << copyTime << " ms" << std::endl;
#endif

                    // 调用 GPU 核函数进行：校准 → 扣底 → 预处理 → PLS-DA 分选
                    calc_GPU(cuda_frameData, cuda_tempData, cuda_outputData,
                        cuda_k_White, cuda_White_ReadBytes, cuda_Black_ReadBytes,
                        cuda_otherParams, cuda_preprocessList, cuda_modelWaveList,
                        cuda_Intercept, cuda_Coef, cuda_StdX, cuda_MeanX, cuda_tags);

                    // 申请主机内存接收结果
                    char* sortResult = (char*)malloc(dealCount * g_Samples * sizeof(char));
                    if (sortResult == NULL) {
                        fprintf(stderr, "Host memory allocation failed!");
                        goto Error;
                    }
                    // 从 GPU 拷回结果（每像素一个 tag）
                    cudaStatus = cudaMemcpy(sortResult, cuda_tags,
                        dealCount * g_Samples * sizeof(char),
                        cudaMemcpyDeviceToHost);
                    if (cudaStatus != cudaSuccess) {
                        fprintf(stderr, "cudaMemcpy failed!");
                        goto Error;
                    }

#ifdef OUTPUT_SORTING_RESULT
                    sortResultDatas.enqueue(sortResult);   // 推送给分发线程
                    SetEvent(hEvent);                       // 唤醒分发线程
#endif
                }
#endif
#ifdef CPU_MODE
                if (false)   // CPU 路径默认关闭
                {
                    auto start = std::chrono::high_resolution_clock::now();
                    GrabData* data;
                    for (size_t i = 0; i < dealCount; i++)
                    {
                        grabDatas.dequeue(data);
                        memcpy(cpu_frameData + i * g_Samples * g_bands * 2 * sizeof(char),
                            data->Values, g_Samples * g_bands * 2 * sizeof(char));
                        delete data;
                    }
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double> duration = end - start;
                    std::cout << "程序运行时间: " << duration.count() << " 秒" << std::endl;

                    calc_CPU(cpu_frameData, cpu_tempData, cpu_outputData,
                        calibrationInfo.k_White.data(),
                        calibrationInfo.White_ReadBytes.data(),
                        calibrationInfo.Black_ReadBytes.data(),
                        cpu_lOtherParams, aiModel.Preprocessings.data(),
                        aiModel.CoreList[0].selectIndex.data(),
                        aiModel.CoreList[0].Intercept.data(),
                        aiModel.CoreList[0].Coef.data(),
                        aiModel.CoreList[0].StdX.data(),
                        aiModel.CoreList[0].MeanX.data(), tags);
                }
#endif
            }
        Error:
            ;
        }

        // 退出时清理
#ifdef GPU_MODE
        clearGpu();
#endif
#ifdef CPU_MODE
        delete cpu_frameData;
        delete cpu_outputData;
        delete cpu_tempData;
        delete tags;
        delete cpu_lOtherParams;
#endif
    }
    catch (const char* exception)
    {
        std::cout << exception << std::endl;
        string num;
        std::cin >> num;
        return;
    }
}

// ============================================================================
// 分发线程：消费 sortResultDatas
// 默认是打印调试模式（DIS_SORT_RESULT），实际部署时应改为调用 ControlValvesWithRows
// ============================================================================
#define DIS_SORT_RESULT      // 调试模式：打印分选标签到控制台
void threadFunction_dis()
{
    while (startFlag)
    {
        WaitForSingleObject(hEvent, INFINITE);
        if (sortResultDatas.size() > 0)
        {
            char* data2;
            sortResultDatas.dequeue(data2);
            firstByteList.dequeue(firstByte);  // 同步取出对应的心跳字节

#ifdef DIS_SORT_RESULT
            // === 调试模式：打印每帧非零标签 ===
            std::string dis = "";
            int couttt1 = 0;
            for (size_t i = 0; i < dealCount; i++)
            {
                couttt1 = 0;
                dis = "";
                for (size_t pair = 0; pair < g_Samples; pair++)
                {
                    if (data2[i * g_Samples + pair] != 0)
                    {
                        dis += std::to_string(data2[i * g_Samples + pair]) + " ";
                    }
                }
                if (dis != "")
                {
                    std::cout << dis << std::endl;
                }
            }
#elif    // 注意：原代码这里是 #elif 不是 #else，行为同 #else
            // === 生产模式：调用阀控发送 ===
            ControlValvesWithRows(data2, dealCount);
#endif
            delete data2;   // 释放结果缓冲
        }
    }
}

// ============================================================================
// Start: 启动相机采集 + 启动 GPU 处理线程 + 启动分发线程
// ============================================================================
bool Start()
{
    try
    {
        if (g_camera->getOpen())
        {
            g_camera->StartGrab();
            std::cout << ("开始采集高光谱数据") << std::endl;
        }
    }
    catch (const char* exception)
    {
        return false;
    }
    try
    {
        std::thread t1(threadFunction);
        t1.detach();    // 分离：主线程不等待
        std::thread t2(threadFunction_dis);
        t2.detach();
    }
    catch (const char* exception)
    {
        return false;
    }
    return true;
}

// ============================================================================
// Stop: 停止采集
// ============================================================================
bool Stop()
{
    try
    {
        g_camera->StopGrab();
        std::cout << ("停止采集高光谱数据") << std::endl;
    }
    catch (const char* exception)
    {
        return false;
    }
    return true;
}

// ============================================================================
// Disconnect: 关闭相机
// ============================================================================
bool Disconnect()
{
    try
    {
        g_camera->CloseCamera();
        std::cout << ("关闭高光谱相机") << std::endl;
    }
    catch (const char* exception)
    {
        return false;
    }
    return true;
}