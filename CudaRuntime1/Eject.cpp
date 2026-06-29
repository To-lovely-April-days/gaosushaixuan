// ============================================================================
// 文件名：Eject.cpp
// 作用  ：阀控通信模块的实现（串口 + 按帧动态延迟补偿）
//
// v3.7 改动：
//   - UDP_receive_thread 加新协议解析（包头 0xAA 0xBB）
//   - 支持 SET / QUERY / ACK
//   - 共 10 个参数可远程配置
//
// v3.8 改动：
//   - ★ 串口写入异步化, 解决 ProcessOneFrame 内部 WriteFile 偶发卡顿
//   - ProcessOneFrame 推命令到队列后立即返回 (~5us)
//   - 后台线程异步执行 WriteFile, 不阻塞主流程
//   - process max 从 ~44ms 降到 < 1ms
//
// v4.6 改动（参数日志）：
//   - 新增 LogAllParams(): 把当前全部运行参数快照写入 perf.log
//   - ApplyParam 拆为 ApplyParamImpl + 带日志外壳: 每次 UDP 改参自动留痕
//     [PARAM-CHG] 行带时间戳, 便于把"误吹出现时间"对上"哪次改了哪个参数"
// ============================================================================
#include "Eject.h"
#include "EventLog.h"
#include "UnifyProcessor.h"   // ★ v3.7 引用统一像素全局参数

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <vector>
#include <string>
#include <queue>                  // ★ v3.8
#include <mutex>                  // ★ v3.8
#include <condition_variable>     // ★ v3.8
#include <thread>                 // ★ v3.8
#include <iostream>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "PerfLog.h"              // ★ v4.6 参数日志

#pragma comment(lib, "ws2_32.lib")

// ============================================================================
// 外部全局变量
// ============================================================================
extern int dealCount;
extern int g_Samples;
extern int frameq;

// ============================================================================
// 模块全局变量
// ============================================================================
char firstByte = 0;

int sendok = 0;
int sel_type[12] = { 0 };
int percent[12] = { 0 };
int fa_ctl = 0;

std::atomic<bool> running{ true };

const int VALVE_COUNT = 192;

ValveRange g_valveMap[VALVE_COUNT];
bool       g_valveMapReady = false;

// ============================================================================
// ★ 阀控通信方式（默认 UDP）
// ============================================================================
std::atomic<int> g_valveCommMode{ VALVE_COMM_UDP };

// ===== UDP 阀控目标（千兆网接口板）=====
static std::string    g_valveUdpIp = "192.168.1.1";
static unsigned short g_valveUdpPort = 10100;
static SOCKET         g_valveUdpSocket = INVALID_SOCKET;
static sockaddr_in    g_valveUdpAddr{};
static std::atomic<bool> g_valveUdpReady{ false };

// 新 UDP 协议固定包头（14 字节），其后接 6 字节负载
static const unsigned char UDP_VALVE_HEADER[14] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0xFF
};
#define UDP_VALVE_PACKET_LEN  20   // 14 头 + 6 负载

void SetValveCommMode(int mode) {
    g_valveCommMode.store(mode);
}
void SetValveUdpTarget(const std::string& ip, unsigned short port) {
    g_valveUdpIp = ip;
    g_valveUdpPort = port;
}

// ===== 串口配置 =====
static std::string  g_comPort = "COM8";
static int          g_baudRate = 961200;
static unsigned short g_duration = 30;       // ★ 可远程配置
static unsigned short g_delay = 1;
static int          g_frontOffset = 0;
static int          g_backOffset = 0;

// ===== 按帧补偿 =====
unsigned short g_totalDelay = 45;            // ★ 可远程配置
int            g_valveThreshold = 1;
int g_valveThresholdRatio = 60;              // ★ 可远程配置
std::atomic<unsigned long long> g_currentFrameTick{ 0 };
int g_centerValveInflate = 2;                // ★ 可远程配置

int g_frameActivateThreshold = 5;            // ★ 可远程配置

// ===== QPC =====
static LARGE_INTEGER g_qpcFreq;
static bool g_qpcInited = false;

// ===== 串口句柄 =====
static HANDLE g_hSerial = INVALID_HANDLE_VALUE;

// ============================================================================
// ★ v3.8 异步串口发送
// ============================================================================
struct SerialCmd {
    // 单帧最多 96 段(交替开/关)。串口 96×7=672, UDP 96×20=1920, 4096 足够
    unsigned char buf[4096];
    int  len;
    bool isUdp;   // ★ true=UDP sendto(20字节/段), false=串口 WriteFile(7字节/段)
};

static std::queue<SerialCmd*>    g_serialQueue;
static std::mutex                g_serialMutex;
static std::condition_variable   g_serialCv;
static std::atomic<bool>         g_serialThreadRunning{ false };
static std::thread               g_serialThread;
static std::atomic<int>          g_serialQueueDropCount{ 0 };
static std::atomic<int>          g_serialQueueMaxSize{ 0 };

#define SERIAL_QUEUE_MAX  200    // 队列上限

// ============================================================================
// QPC 工具
// ============================================================================
unsigned long long NowMs()
{
    if (!g_qpcInited) {
        QueryPerformanceFrequency(&g_qpcFreq);
        g_qpcInited = true;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (unsigned long long)((t.QuadPart * 1000ULL) / g_qpcFreq.QuadPart);
}

// ============================================================================
// 配置函数
// ============================================================================
void SetSerialPort(const std::string& comPort, int baudRate) {
    g_comPort = comPort;
    g_baudRate = baudRate;
}

void SetEjectParams(unsigned short durationMs, unsigned short delayMs) {
    g_duration = durationMs;
    g_delay = delayMs;
}

void SetPixelOffset(int frontOffset, int backOffset) {
    g_frontOffset = frontOffset;
    g_backOffset = backOffset;
}

// ============================================================================
// ★ v4.6 参数快照：把当前全部运行参数写入 perf.log
//   放在这里（Eject.cpp）的原因：g_duration 是本文件的 static 变量,
//   只有同一翻译单元能读到。
//   注意：PerfLog 单条上限 220 字节, 下面每行都远小于此, 不会被截断。
// ============================================================================
void LogAllParams(const char* tag)
{
    PERF_LOG("==== PARAMS (%s) ====", tag ? tag : "");
    PERF_LOG("[PARAM] totalDelay=%dms duration=%dms valveRatio=%d%% valveThreshold=%d frameActivate=%d centerInflate=%d",
        (int)g_totalDelay, (int)g_duration, g_valveThresholdRatio,
        g_valveThreshold, g_frameActivateThreshold, g_centerValveInflate);
    PERF_LOG("[PARAM] unify(majority-vote) enable=%d useGpu=%d tailFrames=%d minArea=%d",
        (int)g_unifyEnable, (int)g_unifyUseGpu, g_unifyTailFrames, g_unifyMinArea);
    PERF_LOG("[PARAM] unify(deprecated) forceClassId=%d threshold=%.3f fillBg=%d",
        g_unifyForceClassId, g_unifyThreshold, (int)g_unifyFillBackground);

    // 哪些 class 会被吹（sel_type=1）
    std::string ej;
    for (int i = 0; i < 12; i++)
        if (sel_type[i]) ej += std::to_string(i + 1) + " ";
    PERF_LOG("[PARAM] eject classes (sel_type=1): %s",
        ej.empty() ? "(none)" : ej.c_str());
}

void BuildValveMap()
{
    int x_start = g_frontOffset;
    int x_end = g_Samples - g_backOffset;
    int x_range = x_end - x_start;

    if (x_range <= 0) {
        std::cerr << "[Eject] BuildValveMap 失败：像素范围无效" << std::endl;
        g_valveMapReady = false;
        return;
    }

    for (int fa = 0; fa < VALVE_COUNT; fa++) {
        int seg = VALVE_COUNT - 1 - fa;
        int x1 = x_start + (seg * x_range) / VALVE_COUNT;
        int x2 = x_start + ((seg + 1) * x_range) / VALVE_COUNT - 1;
        if (x2 >= g_Samples) x2 = g_Samples - 1;
        g_valveMap[fa].x1 = x1;
        g_valveMap[fa].x2 = x2;
    }
    g_valveMapReady = true;

    std::cout << "[Eject] 阀号映射表已生成（按比例均匀分布，"
        << "VALVE_COUNT=" << VALVE_COUNT
        << "，像素范围 [" << x_start << "~" << x_end
        << "] 共 " << x_range << "px）" << std::endl;
    std::cout << "  每阀平均 = " << (double)x_range / VALVE_COUNT << " 像素" << std::endl;

    for (int i = 0; i < 5 && i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "px)" << std::endl;
    }
    std::cout << "  ..." << std::endl;
    for (int i = 95; i <= 97; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "px)" << std::endl;
    }
    std::cout << "  ..." << std::endl;
    for (int i = VALVE_COUNT - 5; i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "px)" << std::endl;
    }
}

// ============================================================================
// Start_send / Stop_send
// ============================================================================
// ★ v3.8 前置声明 (函数实现在文件下方)
static void StartSerialThread();
static void StopSerialThread();

// ============================================================================
// ★ 当前阀控通信链路是否就绪（按通信方式判断）
// ============================================================================
static bool ValveTransportReady()
{
    if (g_valveCommMode.load() == VALVE_COMM_UDP)
        return g_valveUdpReady.load();
    return g_hSerial != INVALID_HANDLE_VALUE;
}

// ============================================================================
// ★ UDP 阀控链路 开/关
//   WSAStartup/WSACleanup 带引用计数, 与 UDP_receive_thread 并存无碍
// ============================================================================
static bool StartValveUdp()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Eject] WSAStartup 失败(UDP阀控)" << std::endl;
        return false;
    }

    g_valveUdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_valveUdpSocket == INVALID_SOCKET) {
        std::cerr << "[Eject] UDP 阀控套接字创建失败" << std::endl;
        WSACleanup();
        return false;
    }

    g_valveUdpAddr.sin_family = AF_INET;
    g_valveUdpAddr.sin_port = htons(g_valveUdpPort);
    if (inet_pton(AF_INET, g_valveUdpIp.c_str(), &g_valveUdpAddr.sin_addr) != 1) {
        std::cerr << "[Eject] UDP 阀控目标 IP 非法: " << g_valveUdpIp << std::endl;
        closesocket(g_valveUdpSocket);
        g_valveUdpSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    g_valveUdpReady.store(true);
    return true;
}

static void StopValveUdp()
{
    if (!g_valveUdpReady.load() && g_valveUdpSocket == INVALID_SOCKET) return;
    g_valveUdpReady.store(false);
    if (g_valveUdpSocket != INVALID_SOCKET) {
        closesocket(g_valveUdpSocket);
        g_valveUdpSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

// ============================================================================
// ★ 打开串口（从 Start_send 抽出, 仅串口模式调用）
// ============================================================================
static bool OpenSerialPort()
{
    g_hSerial = CreateFileA(
        g_comPort.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (g_hSerial == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        std::cerr << "[Eject] 打开串口失败: " << g_comPort
            << " (错误码 " << err << ")" << std::endl;
        return false;
    }

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(g_hSerial, &dcb)) {
        std::cerr << "[Eject] GetCommState 失败" << std::endl;
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }
    dcb.BaudRate = g_baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(g_hSerial, &dcb)) {
        std::cerr << "[Eject] SetCommState 失败" << std::endl;
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(g_hSerial, &timeouts);

    PurgeComm(g_hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    std::cout << "[Eject] 串口打开成功: " << g_comPort
        << " @ " << g_baudRate << " bps" << std::endl;
    return true;
}

bool Start_send()
{
    bool useUdp = (g_valveCommMode.load() == VALVE_COMM_UDP);

    if (useUdp) {
        if (!StartValveUdp()) return false;
        std::cout << "[Eject] 阀控通信方式: UDP -> "
            << g_valveUdpIp << ":" << g_valveUdpPort
            << " (新协议, 20字节/段)" << std::endl;
    }
    else {
        if (!OpenSerialPort()) return false;
        std::cout << "[Eject] 阀控通信方式: 串口 (旧协议, 7字节/段)" << std::endl;
    }

    std::cout << "[Eject] 像素偏移: 前=" << g_frontOffset
        << " 后=" << g_backOffset
        << " 有效范围=[" << g_frontOffset << "," << (g_Samples - g_backOffset) << "]"
        << std::endl;
    std::cout << "[Eject] 阀总数=" << VALVE_COUNT
        << " 吹气=" << g_duration << "ms" << std::endl;
    std::cout << "[Eject] 模式: 按帧动态补偿  g_totalDelay=" << g_totalDelay
        << "ms  g_valveThreshold=" << g_valveThreshold << std::endl;

    BuildValveMap();

    // ★ v3.8 启动异步串口发送线程
    StartSerialThread();

    return true;
}

void Stop_send()
{
    // ★ v3.8 先停异步发送线程, 让队列里残留命令发完
    StopSerialThread();
    std::cout << "[Eject] 队列统计: maxSize=" << g_serialQueueMaxSize.load()
        << ", drops=" << g_serialQueueDropCount.load() << std::endl;

    if (g_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
        std::cout << "[Eject] 串口已关闭" << std::endl;
    }

    // ★ 关闭 UDP 阀控链路
    if (g_valveUdpSocket != INVALID_SOCKET) {
        StopValveUdp();
        std::cout << "[Eject] UDP 阀控链路已关闭" << std::endl;
    }
}

// ============================================================================
// ★ v3.8 异步串口发送线程实现
// ============================================================================
static void SerialSendThread()
{
    std::cout << "[Eject] 异步串口发送线程已启动" << std::endl;

    while (g_serialThreadRunning.load()) {
        SerialCmd* cmd = nullptr;
        {
            std::unique_lock<std::mutex> lock(g_serialMutex);
            g_serialCv.wait(lock, [] {
                return !g_serialQueue.empty() || !g_serialThreadRunning.load();
                });

            if (!g_serialThreadRunning.load() && g_serialQueue.empty()) break;

            if (!g_serialQueue.empty()) {
                cmd = g_serialQueue.front();
                g_serialQueue.pop();
            }
        }

        if (cmd && cmd->len > 0) {
            if (cmd->isUdp) {
                // ★ UDP: 每 20 字节作为一个独立数据报发往接口板
                if (g_valveUdpReady.load() && g_valveUdpSocket != INVALID_SOCKET) {
                    for (int off = 0;
                        off + UDP_VALVE_PACKET_LEN <= cmd->len;
                        off += UDP_VALVE_PACKET_LEN) {
                        int s = sendto(g_valveUdpSocket,
                            (const char*)(cmd->buf + off),
                            UDP_VALVE_PACKET_LEN, 0,
                            (const sockaddr*)&g_valveUdpAddr,
                            sizeof(g_valveUdpAddr));
                        if (s == SOCKET_ERROR) {
                            std::cerr << "[Eject] UDP 阀控发送失败, 错误码: "
                                << WSAGetLastError() << std::endl;
                            break;
                        }
                    }
                }
            }
            else if (g_hSerial != INVALID_HANDLE_VALUE) {
                DWORD bytesWritten = 0;
                BOOL ok = WriteFile(g_hSerial, cmd->buf, cmd->len, &bytesWritten, NULL);
                if (!ok) {
                    std::cerr << "[Eject] 异步串口写入失败, 错误码: "
                        << GetLastError() << std::endl;
                }
            }
        }

        delete cmd;
    }

    // 退出: 清空残留队列
    std::lock_guard<std::mutex> lock(g_serialMutex);
    while (!g_serialQueue.empty()) {
        delete g_serialQueue.front();
        g_serialQueue.pop();
    }

    std::cout << "[Eject] 异步串口发送线程已退出" << std::endl;
}

static void StartSerialThread()
{
    if (g_serialThreadRunning.load()) return;
    g_serialThreadRunning.store(true);
    g_serialThread = std::thread(SerialSendThread);
}

static void StopSerialThread()
{
    if (!g_serialThreadRunning.load()) return;
    g_serialThreadRunning.store(false);
    g_serialCv.notify_all();
    if (g_serialThread.joinable()) {
        g_serialThread.join();
    }
}

// 入队 (从 ProcessOneFrame 调用, 必须快)
static void EnqueueSerialCmd(const unsigned char* buf, int len, bool isUdp)
{
    if (len <= 0 || len > (int)sizeof(SerialCmd::buf)) return;
    if (!ValveTransportReady()) return;

    SerialCmd* cmd = new SerialCmd;
    memcpy(cmd->buf, buf, len);
    cmd->len = len;
    cmd->isUdp = isUdp;

    int qsize = 0;
    {
        std::lock_guard<std::mutex> lock(g_serialMutex);
        if ((int)g_serialQueue.size() >= SERIAL_QUEUE_MAX) {
            // 队列满: 丢弃最老命令 (生产环境抗压)
            delete g_serialQueue.front();
            g_serialQueue.pop();
            g_serialQueueDropCount.fetch_add(1);
        }
        g_serialQueue.push(cmd);
        qsize = (int)g_serialQueue.size();
    }
    if (qsize > g_serialQueueMaxSize.load()) {
        g_serialQueueMaxSize.store(qsize);
    }
    g_serialCv.notify_one();
}

// ============================================================================
// ProcessOneFrame（v3.8 - 串口写入改异步）
// ============================================================================
void ProcessOneFrame(const char* frameTags, unsigned long long frameTickMs)
{
    if (!ValveTransportReady()) return;
    if (!g_valveMapReady) return;

    bool useUdp = (g_valveCommMode.load() == VALVE_COMM_UDP);

    g_currentFrameTick.store(frameTickMs);

    bool valve_trigger[VALVE_COUNT] = { false };
    int rawClassified = 0;

    for (int fa = 0; fa < VALVE_COUNT; fa++) {
        int x1 = g_valveMap[fa].x1;
        int x2 = g_valveMap[fa].x2;
        int valveWidth = x2 - x1 + 1;

        int hitCount = 0;
        for (int x = x1; x <= x2; x++) {
            int cls = (unsigned char)frameTags[x];

            if (cls > 0 && cls <= 12 && sel_type[cls - 1] == 1) {
                hitCount++;
            }
        }

        rawClassified += hitCount;

        if (g_valveThresholdRatio > 0) {
            valve_trigger[fa] = (hitCount * 100 >= valveWidth * g_valveThresholdRatio);
        }
        else {
            valve_trigger[fa] = (hitCount >= g_valveThreshold);
        }
    }

    if (g_frameActivateThreshold > 0) {
        int maxRun = 0;
        int curRun = 0;
        for (int x = 0; x < g_Samples; x++) {
            int cls = (unsigned char)frameTags[x];
            bool isTarget = (cls > 0 && cls <= 12 && sel_type[cls - 1] == 1);

            if (isTarget) {
                curRun++;
                if (curRun > maxRun) maxRun = curRun;
            }
            else {
                curRun = 0;
            }
        }

        if (maxRun < g_frameActivateThreshold) {
            memset(valve_trigger, 0, sizeof(valve_trigger));

            if (rawClassified > 0) {
                static std::atomic<int> g_suppressCount{ 0 };
                g_suppressCount.fetch_add(1);
                // ★ v3.8 去掉热路径的 cout 输出
            }
        }
    }

    if (g_centerValveInflate > 0) {
        const int CENTER_VALVE_START = 64;
        const int CENTER_VALVE_END = 127;

        bool original_trigger[VALVE_COUNT];
        memcpy(original_trigger, valve_trigger, sizeof(valve_trigger));

        for (int fa = CENTER_VALVE_START; fa <= CENTER_VALVE_END; fa++) {
            if (!original_trigger[fa]) continue;

            for (int d = 1; d <= g_centerValveInflate; d++) {
                int left = fa - d;
                if (left >= 0) valve_trigger[left] = true;
                else break;
            }
            for (int d = 1; d <= g_centerValveInflate; d++) {
                int right = fa + d;
                if (right < VALVE_COUNT) valve_trigger[right] = true;
                else break;
            }
        }
    }

    // UDP 模式每段 20 字节, 串口模式每段 7 字节, 按更大者开缓冲
    unsigned char txBuf[VALVE_COUNT * UDP_VALVE_PACKET_LEN];
    int txLen = 0;
    int cmdCount = 0;

    unsigned long long now = NowMs();
    long long elapsed = (long long)(now - frameTickMs);
    long long actualDelay = (long long)g_totalDelay - elapsed;
    if (actualDelay < 0) { actualDelay = 0; }
    if (actualDelay > 65535) actualDelay = 65535;
    unsigned short delay = (unsigned short)actualDelay;

    // ★ 新 UDP 协议: 延时/时长各 1 字节, 范围 1~200ms, 需另行夹取
    long long udpDelayLL = actualDelay;
    if (udpDelayLL < 1)   udpDelayLL = 1;     // 不使用延迟时固定 0x01
    if (udpDelayLL > 200) udpDelayLL = 200;
    unsigned char udpDelay = (unsigned char)udpDelayLL;
    int udpDurInt = (int)g_duration;
    if (udpDurInt < 1)   udpDurInt = 1;
    if (udpDurInt > 200) udpDurInt = 200;
    unsigned char udpDuration = (unsigned char)udpDurInt;

    int i = 0;
    while (i < VALVE_COUNT) {
        if (!valve_trigger[i]) { i++; continue; }

        int segStart = i;
        int segEnd = i;
        while (segEnd + 1 < VALVE_COUNT && valve_trigger[segEnd + 1]) {
            segEnd++;
        }

        int valveStart = segStart + 1;
        int valveEnd = segEnd + 1;

        unsigned char* p = txBuf + txLen;
        if (useUdp) {
            // ★ 新 UDP 协议帧: 14 字节固定包头 + 阀起2 + 阀止2 + 延时1 + 时长1
            //   阀编号 0 起始(直接用 segStart/segEnd), 双字节大端
            memcpy(p, UDP_VALVE_HEADER, 14);
            p[14] = (unsigned char)((segStart >> 8) & 0xFF);
            p[15] = (unsigned char)(segStart & 0xFF);
            p[16] = (unsigned char)((segEnd >> 8) & 0xFF);
            p[17] = (unsigned char)(segEnd & 0xFF);
            p[18] = udpDelay;
            p[19] = udpDuration;
            txLen += UDP_VALVE_PACKET_LEN;   // 20
        }
        else {
            // 旧串口协议帧: 阀编号 1 起始(valveStart/valveEnd), 延时/时长各 2 字节
            p[0] = 0xFF;
            p[1] = (unsigned char)valveStart;
            p[2] = (unsigned char)valveEnd;
            p[3] = (unsigned char)(delay >> 8);
            p[4] = (unsigned char)(delay & 0xFF);
            p[5] = (unsigned char)(g_duration >> 8);
            p[6] = (unsigned char)(g_duration & 0xFF);
            txLen += 7;
        }
        cmdCount++;

        i = segEnd + 1;
    }

    // ★ v3.8 异步: 不直接 WriteFile, 推队列后立即返回
    if (txLen > 0) {
        EnqueueSerialCmd(txBuf, txLen, useUdp);
    }
}

// ============================================================================
// ★ v3.7 新增：参数协议处理
// ============================================================================

// 协议常量
#define PROTO_HEADER1   0xAA
#define PROTO_HEADER2   0xBB
#define PROTO_VERSION   0x01

#define CMD_SET         0x01
#define CMD_QUERY       0x02
#define CMD_SET_ACK     0x81
#define CMD_QUERY_ACK   0x82

// 参数 ID
#define PID_TOTAL_DELAY            0x01
#define PID_DURATION               0x02
#define PID_VALVE_RATIO            0x03
#define PID_FRAME_ACTIVATE         0x04
#define PID_CENTER_INFLATE         0x05
#define PID_UNIFY_ENABLE           0x06
#define PID_UNIFY_TAIL_FRAMES      0x07
#define PID_UNIFY_FORCE_CLASSID    0x08
#define PID_UNIFY_THRESHOLD        0x09
#define PID_UNIFY_FILL_BG          0x0A
#define PID_UNIFY_MIN_AREA         0x0B   // 多数投票版: 最小连通域面积(噪点过滤)
#define PID_EJECT_MASK             0x0C   // 吹气选择掩码: bit0=classid1 ... bit11=classid12

// 数据类型
#define TYPE_INT32   0
#define TYPE_FLOAT   1
#define TYPE_BOOL    2

// ACK 状态码
#define ACK_OK              0
#define ACK_UNKNOWN_PARAM   1
#define ACK_OUT_OF_RANGE    2
#define ACK_TYPE_MISMATCH   3
#define ACK_BAD_CHECKSUM    4

// 计算 XOR 校验和
static unsigned char CalcXor(const unsigned char* data, int len)
{
    unsigned char x = 0;
    for (int i = 0; i < len; i++) x ^= data[i];
    return x;
}

// 把 4 字节 little-endian 解码成 int / float
static int DecodeInt32(const unsigned char* p) {
    return (int)((unsigned int)p[0] | ((unsigned int)p[1] << 8)
        | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24));
}

static float DecodeFloat(const unsigned char* p) {
    float f;
    memcpy(&f, p, 4);
    return f;
}

// 把 int / float 编码到 4 字节 little-endian
static void EncodeInt32(unsigned char* p, int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void EncodeFloat(unsigned char* p, float f) {
    memcpy(p, &f, 4);
}

// 构造一个参数 6 字节
static void BuildParam(unsigned char* p, unsigned char id,
    unsigned char type, int intVal, float floatVal)
{
    p[0] = id;
    p[1] = type;
    if (type == TYPE_FLOAT) {
        EncodeFloat(p + 2, floatVal);
    }
    else {
        EncodeInt32(p + 2, intVal);
    }
}

// ============================================================================
// 应用一个参数到全局变量（内部实现）
//   返回：0=OK, 2=越界, 3=类型错误, 1=未知
//   ★ v4.6: 原 ApplyParam 改名为 ApplyParamImpl, switch 逻辑保持不变;
//            外层加一个带日志的 ApplyParam 外壳（见下方）。
// ============================================================================
static int ApplyParamImpl(unsigned char paramId, unsigned char dataType,
    int intVal, float floatVal)
{
    switch (paramId) {
    case PID_TOTAL_DELAY:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 65535) return ACK_OUT_OF_RANGE;
        g_totalDelay = (unsigned short)intVal;
        std::cout << "[UDP] g_totalDelay = " << intVal << " ms" << std::endl;
        return ACK_OK;

    case PID_DURATION:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 65535) return ACK_OUT_OF_RANGE;
        g_duration = (unsigned short)intVal;
        std::cout << "[UDP] g_duration = " << intVal << " ms" << std::endl;
        return ACK_OK;

    case PID_VALVE_RATIO:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 100) return ACK_OUT_OF_RANGE;
        g_valveThresholdRatio = intVal;
        std::cout << "[UDP] g_valveThresholdRatio = " << intVal << " %" << std::endl;
        return ACK_OK;

    case PID_FRAME_ACTIVATE:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 50) return ACK_OUT_OF_RANGE;
        g_frameActivateThreshold = intVal;
        std::cout << "[UDP] g_frameActivateThreshold = " << intVal << std::endl;
        return ACK_OK;

    case PID_CENTER_INFLATE:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 10) return ACK_OUT_OF_RANGE;
        g_centerValveInflate = intVal;
        std::cout << "[UDP] g_centerValveInflate = " << intVal << std::endl;
        return ACK_OK;

    case PID_UNIFY_ENABLE:
        if (dataType != TYPE_BOOL) return ACK_TYPE_MISMATCH;
        if (intVal != 0 && intVal != 1) return ACK_OUT_OF_RANGE;
        g_unifyEnable = (intVal != 0);
        std::cout << "[UDP] g_unifyEnable = " << (g_unifyEnable ? "ON" : "OFF") << std::endl;
        return ACK_OK;

    case PID_UNIFY_TAIL_FRAMES:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 64) return ACK_OUT_OF_RANGE;
        g_unifyTailFrames = intVal;
        std::cout << "[UDP] g_unifyTailFrames = " << intVal << std::endl;
        return ACK_OK;

    case PID_UNIFY_FORCE_CLASSID:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 1 || intVal > 12) return ACK_OUT_OF_RANGE;
        g_unifyForceClassId = intVal;
        std::cout << "[UDP] g_unifyForceClassId = " << intVal << std::endl;
        return ACK_OK;

    case PID_UNIFY_THRESHOLD:
        if (dataType != TYPE_FLOAT) return ACK_TYPE_MISMATCH;
        if (floatVal < 0.0f || floatVal > 1.0f) return ACK_OUT_OF_RANGE;
        g_unifyThreshold = floatVal;
        std::cout << "[UDP] g_unifyThreshold = " << floatVal << std::endl;
        return ACK_OK;

    case PID_UNIFY_FILL_BG:
        if (dataType != TYPE_BOOL) return ACK_TYPE_MISMATCH;
        if (intVal != 0 && intVal != 1) return ACK_OUT_OF_RANGE;
        g_unifyFillBackground = (intVal != 0);
        std::cout << "[UDP] g_unifyFillBackground = "
            << (g_unifyFillBackground ? "ON" : "OFF") << std::endl;
        return ACK_OK;

    case PID_UNIFY_MIN_AREA:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 100000) return ACK_OUT_OF_RANGE;
        g_unifyMinArea = intVal;
        std::cout << "[UDP] g_unifyMinArea = " << intVal
            << " px (连通域 < 此值整块抹成背景, 不吹)" << std::endl;
        return ACK_OK;

    case PID_EJECT_MASK:
        if (dataType != TYPE_INT32) return ACK_TYPE_MISMATCH;
        if (intVal < 0 || intVal > 0xFFF) return ACK_OUT_OF_RANGE;   // 12 位掩码
        for (int i = 0; i < 12; i++) sel_type[i] = (intVal >> i) & 1;
        {
            std::string ej;
            for (int i = 0; i < 12; i++) if (sel_type[i]) ej += std::to_string(i + 1) + " ";
            std::cout << "[UDP] eject mask = 0x" << std::hex << intVal << std::dec
                << "  吹 classid: " << (ej.empty() ? "(none)" : ej.c_str()) << std::endl;
        }
        return ACK_OK;

    default:
        std::cerr << "[UDP] 未知参数 ID: 0x" << std::hex << (int)paramId
            << std::dec << std::endl;
        return ACK_UNKNOWN_PARAM;
    }
}

// ============================================================================
// ★ v4.6 参数 ID → 可读名字（仅供日志使用）
// ============================================================================
static const char* ParamName(unsigned char id) {
    switch (id) {
    case PID_TOTAL_DELAY:         return "totalDelay";
    case PID_DURATION:            return "duration";
    case PID_VALVE_RATIO:         return "valveRatio%";
    case PID_FRAME_ACTIVATE:      return "frameActivate";
    case PID_CENTER_INFLATE:      return "centerInflate";
    case PID_UNIFY_ENABLE:        return "unifyEnable";
    case PID_UNIFY_TAIL_FRAMES:   return "unifyTailFrames";
    case PID_UNIFY_FORCE_CLASSID: return "unifyForceClassId";
    case PID_UNIFY_THRESHOLD:     return "unifyThreshold";
    case PID_UNIFY_FILL_BG:       return "unifyFillBg";
    case PID_UNIFY_MIN_AREA:      return "unifyMinArea";
    case PID_EJECT_MASK:          return "ejectMask";
    default:                      return "unknown";
    }
}

// ============================================================================
// ★ v4.6 ApplyParam 外壳：应用参数 + 写日志
//   每次 UDP 改参成功/失败都在 perf.log 留一行 [PARAM-CHG]（带时间戳）。
//   调用方（HandleParamPacket）无需改动。
// ============================================================================
static int ApplyParam(unsigned char paramId, unsigned char dataType,
    int intVal, float floatVal)
{
    int r = ApplyParamImpl(paramId, dataType, intVal, floatVal);
    if (r == ACK_OK) {
        if (dataType == TYPE_FLOAT)
            PERF_LOG("[PARAM-CHG] %s = %.3f  (UDP OK)", ParamName(paramId), floatVal);
        else
            PERF_LOG("[PARAM-CHG] %s = %d  (UDP OK)", ParamName(paramId), intVal);
    }
    else {
        PERF_LOG("[PARAM-CHG] %s set FAILED code=%d", ParamName(paramId), r);
    }
    return r;
}

// 发送 SET 响应
static void SendSetAck(SOCKET sock, const sockaddr_in& clientAddr,
    int clientAddrLen, unsigned char status,
    unsigned char errorParamId)
{
    unsigned char ack[10];
    ack[0] = PROTO_HEADER1;
    ack[1] = PROTO_HEADER2;
    ack[2] = PROTO_VERSION;
    ack[3] = CMD_SET_ACK;
    ack[4] = 0x02;          // payload_len = 2 (low byte)
    ack[5] = 0x00;          // payload_len = 2 (high byte)
    ack[6] = status;
    ack[7] = errorParamId;
    ack[8] = CalcXor(ack, 8);
    ack[9] = 0;             // 无意义，凑齐发包

    sendto(sock, (const char*)ack, 9, 0,
        (const sockaddr*)&clientAddr, clientAddrLen);
}

// 发送 QUERY 响应（包含所有 10 个参数当前值）
static void SendQueryAck(SOCKET sock, const sockaddr_in& clientAddr,
    int clientAddrLen)
{
    // 头 6 + count 1 + 10 个参数 × 6 + checksum 1 = 68 字节
    unsigned char buf[128];   // 12 参数需 80 字节, 留余量
    int idx = 0;
    buf[idx++] = PROTO_HEADER1;
    buf[idx++] = PROTO_HEADER2;
    buf[idx++] = PROTO_VERSION;
    buf[idx++] = CMD_QUERY_ACK;

    // payload_len 占位（稍后填）
    int lenIdx = idx;
    idx += 2;

    int payloadStart = idx;
    buf[idx++] = 12;        // 参数个数

    // 10 个参数
    BuildParam(buf + idx, PID_TOTAL_DELAY, TYPE_INT32, (int)g_totalDelay, 0);
    idx += 6;
    BuildParam(buf + idx, PID_DURATION, TYPE_INT32, (int)g_duration, 0);
    idx += 6;
    BuildParam(buf + idx, PID_VALVE_RATIO, TYPE_INT32, g_valveThresholdRatio, 0);
    idx += 6;
    BuildParam(buf + idx, PID_FRAME_ACTIVATE, TYPE_INT32, g_frameActivateThreshold, 0);
    idx += 6;
    BuildParam(buf + idx, PID_CENTER_INFLATE, TYPE_INT32, g_centerValveInflate, 0);
    idx += 6;
    BuildParam(buf + idx, PID_UNIFY_ENABLE, TYPE_BOOL, g_unifyEnable ? 1 : 0, 0);
    idx += 6;
    BuildParam(buf + idx, PID_UNIFY_TAIL_FRAMES, TYPE_INT32, g_unifyTailFrames, 0);
    idx += 6;
    BuildParam(buf + idx, PID_UNIFY_FORCE_CLASSID, TYPE_INT32, g_unifyForceClassId, 0);
    idx += 6;
    BuildParam(buf + idx, PID_UNIFY_THRESHOLD, TYPE_FLOAT, 0, g_unifyThreshold);
    idx += 6;
    BuildParam(buf + idx, PID_UNIFY_FILL_BG, TYPE_BOOL, g_unifyFillBackground ? 1 : 0, 0);
    idx += 6;
    BuildParam(buf + idx, PID_UNIFY_MIN_AREA, TYPE_INT32, g_unifyMinArea, 0);
    idx += 6;
    int ejectMaskQ = 0;
    for (int i = 0; i < 12; i++) if (sel_type[i]) ejectMaskQ |= (1 << i);
    BuildParam(buf + idx, PID_EJECT_MASK, TYPE_INT32, ejectMaskQ, 0);
    idx += 6;

    int payloadLen = idx - payloadStart;
    buf[lenIdx] = (unsigned char)(payloadLen & 0xFF);
    buf[lenIdx + 1] = (unsigned char)((payloadLen >> 8) & 0xFF);

    // 校验和
    buf[idx] = CalcXor(buf, idx);
    idx++;

    sendto(sock, (const char*)buf, idx, 0,
        (const sockaddr*)&clientAddr, clientAddrLen);

    std::cout << "[UDP] 已响应 QUERY (" << idx << " 字节)" << std::endl;
}

// 处理新协议包
static void HandleParamPacket(SOCKET sock, const unsigned char* buf, int len,
    const sockaddr_in& clientAddr, int clientAddrLen)
{
    // 最少 7 字节（头4 + len2 + checksum1）
    if (len < 7) return;

    // 校验包头
    if (buf[0] != PROTO_HEADER1 || buf[1] != PROTO_HEADER2) return;
    if (buf[2] != PROTO_VERSION) {
        std::cerr << "[UDP] 协议版本不匹配: " << (int)buf[2] << std::endl;
        return;
    }

    unsigned char cmd = buf[3];
    int payloadLen = (int)buf[4] | ((int)buf[5] << 8);

    // 校验包长
    int expectedLen = 6 + payloadLen + 1;   // 6 头 + payload + 1 校验
    if (len < expectedLen) {
        std::cerr << "[UDP] 包长度不足: 期望 " << expectedLen
            << " 实际 " << len << std::endl;
        return;
    }

    // 校验和
    unsigned char calcXor = CalcXor(buf, expectedLen - 1);
    unsigned char recvXor = buf[expectedLen - 1];
    if (calcXor != recvXor) {
        std::cerr << "[UDP] 校验和不匹配 calc=0x" << std::hex << (int)calcXor
            << " recv=0x" << (int)recvXor << std::dec << std::endl;
        SendSetAck(sock, clientAddr, clientAddrLen, ACK_BAD_CHECKSUM, 0);
        return;
    }

    if (cmd == CMD_SET) {
        // payload: [count:1, (param_id:1, type:1, value:4) × N]
        if (payloadLen < 1) {
            std::cerr << "[UDP] SET 包 payload 太短" << std::endl;
            return;
        }
        unsigned char count = buf[6];
        if (payloadLen != 1 + count * 6) {
            std::cerr << "[UDP] SET 包 payload 长度不对: 期望 "
                << (1 + count * 6) << " 实际 " << payloadLen << std::endl;
            return;
        }

        // 逐个应用参数；任一出错就回错误 ACK 并停
        const unsigned char* p = buf + 7;
        for (int i = 0; i < count; i++) {
            unsigned char pid = p[0];
            unsigned char type = p[1];
            int intVal = DecodeInt32(p + 2);
            float floatVal = DecodeFloat(p + 2);

            int result = ApplyParam(pid, type, intVal, floatVal);
            if (result != ACK_OK) {
                SendSetAck(sock, clientAddr, clientAddrLen,
                    (unsigned char)result, pid);
                return;
            }

            p += 6;
        }

        // 全部成功
        SendSetAck(sock, clientAddr, clientAddrLen, ACK_OK, 0);
    }
    else if (cmd == CMD_QUERY) {
        SendQueryAck(sock, clientAddr, clientAddrLen);
    }
    else {
        std::cerr << "[UDP] 未知命令字: 0x" << std::hex << (int)cmd
            << std::dec << std::endl;
    }
}

// ============================================================================
// UDP 接收线程（v3.7 - 支持双协议）
// ============================================================================
typedef unsigned char byte;

void UDP_receive_thread()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Eject] WSAStartup 失败" << std::endl;
        return;
    }

    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "[Eject] 接收套接字创建失败" << std::endl;
        WSACleanup();
        return;
    }

    int reuseAddr = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

    sockaddr_in serverAddr1{};
    serverAddr1.sin_family = AF_INET;
    serverAddr1.sin_port = htons(8080);
    serverAddr1.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(udpSocket, reinterpret_cast<sockaddr*>(&serverAddr1),
        sizeof(serverAddr1)) == SOCKET_ERROR) {
        std::cerr << "[Eject] UDP 绑定失败 端口 8080" << std::endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }

    std::cout << "[UDP] 监听端口 8080，支持协议:" << std::endl;
    std::cout << "       - SortingExpert 协议（包头 0x02 0x2A）" << std::endl;
    std::cout << "       - 参数配置协议  （包头 0xAA 0xBB）" << std::endl;

    std::vector<byte> buf(1024);
    while (running) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        int recvLen = recvfrom(udpSocket,
            reinterpret_cast<char*>(buf.data()),
            (int)buf.size(), 0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen);

        if (recvLen <= 0) continue;

        // ===== SortingExpert 协议（保留兼容）=====
        if (recvLen >= 6 && buf[0] == 2 && buf[1] == 0x2a) {
            sendok = buf[5];
            // sel_type / percent / fa_ctl 保持注释，由 main.cpp 自动配置
            continue;
        }

        // ===== ★ v3.7 新增：参数配置协议 =====
        if (recvLen >= 7 && buf[0] == PROTO_HEADER1 && buf[1] == PROTO_HEADER2) {
            HandleParamPacket(udpSocket, buf.data(), recvLen,
                clientAddr, clientAddrLen);
            continue;
        }

        // 未知协议，忽略
    }

    closesocket(udpSocket);
    WSACleanup();
}