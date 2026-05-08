// ============================================================================
// 文件名：Eject.cpp
// 作用  ：阀控通信模块的实现（串口 + 按帧动态延迟补偿）
//
// v2 重大改动（相对旧版）：
//   1. 启动时一次性生成阀号映射表 g_valveMap[192]（含倒序映射）
//   2. 处理粒度：批 → 帧（ProcessOneFrame 处理 1 帧的 640 像素）
//   3. 新增 g_valveThreshold 参数（阀触发阈值）
//   4. 删除：5x5 过滤、阀位膨胀（EnhanceTags）、ControlValvesWithRows、批中点延迟
//   5. 优化：每帧的多条命令打包成一次 WriteFile（减少串口调用开销）
//   6. 删除 g_useDelayCompensation 开关、OpenAir 旧路径
// ============================================================================
#include "Eject.h"
#include "EventLog.h"

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <atomic>

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
int percent[12] = { 0 };   // v2 已不用，但 UDP 接收逻辑保留兼容
int fa_ctl = 0;            // v2 已不用，但 UDP 接收逻辑保留兼容

std::atomic<bool> running{ true };

const int VALVE_COUNT = 192;

// ===== 阀号映射表（启动时一次性生成，含倒序映射）=====
ValveRange g_valveMap[VALVE_COUNT];
bool       g_valveMapReady = false;

// ===== 串口配置参数 =====
static std::string  g_comPort = "COM8";
static int          g_baudRate = 961200;
static unsigned short g_duration = 30;       // 吹气时长（ms）
static unsigned short g_delay = 1;        // 兼容字段（v2 不再使用）
static int          g_frontOffset = 0;
static int          g_backOffset = 0;

// ===== 按帧延迟补偿相关 =====
unsigned short g_totalDelay = 45;             // 默认 45ms（按帧模式建议值）
int            g_valveThreshold = 1;          // 默认 1（等同旧逻辑）
// 阀触发比例阈值（0~100，0=不启用比例，仍用 g_valveThreshold）
//   60 = 60% 比例 → 3px阀需要≥2 (67%)，4px阀需要≥3 (75%)  ★ 推荐
//   75 = 75% 比例 → 3px阀需要≥3 (100%)，4px阀需要≥3 (75%)
//   90 = 90% 比例 → 3px阀需要≥3 (100%)，4px阀需要≥4 (100%) 最严格
//   0  = 不启用比例（用旧的 g_valveThreshold）
//
// 启用后会忽略 g_valveThreshold
int g_valveThresholdRatio = 60;
std::atomic<unsigned long long> g_currentFrameTick{ 0 };
// 中间 4 像素阀（阀 65~128）触发时的横向膨胀范围（阀数）
//   0 = 不膨胀
//   1 = ±1 阀（吹 3 个：左 1 + 自己 + 右 1）
//   2 = ±2 阀（吹 5 个：左 2 + 自己 + 右 2，默认）
//   3 = ±3 阀（吹 7 个）
// 注意：膨胀允许跨界（阀 65 可以扩到阀 64 即 3 像素区，反之亦然）
int g_centerValveInflate = 2;

// 帧激活阈值（详见 Eject.h 注释）
int g_frameActivateThreshold = 5;
// ===== QPC 时间戳工具 =====
static LARGE_INTEGER g_qpcFreq;
static bool g_qpcInited = false;

// ===== 串口句柄 =====
static HANDLE g_hSerial = INVALID_HANDLE_VALUE;

// ============================================================================
// QPC 工具：获取毫秒精度时间戳
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
// 配置函数（运行时可调）
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

// 192 阀均匀覆盖 640 像素
// 每阀 = 640 / 192 = 3.333... 像素

void BuildValveMap()
{
    int x_start = g_frontOffset;      // 默认 0
    int x_end = g_Samples - g_backOffset;  // 默认 640
    int x_range = x_end - x_start;    // 640

    if (x_range <= 0) {
        std::cerr << "[Eject] BuildValveMap 失败：像素范围无效" << std::endl;
        g_valveMapReady = false;
        return;
    }

    // ===== 按比例均匀分布 =====
    // 倒序：物理阀 fa=0（阀 1）对应图像最右段
    //      fa=191（阀 192）对应图像最左段
    //
    // 第 seg 段（从左数 0~191）的像素范围：
    //   x1 = x_start + floor(seg * x_range / 192)
    //   x2 = x_start + floor((seg+1) * x_range / 192) - 1
    // 这样每段宽度 3 或 4 像素自动分配，但分布是均匀的（不是连续 64 个 3、64 个 4、64 个 3）

    for (int fa = 0; fa < VALVE_COUNT; fa++) {
        int seg = VALVE_COUNT - 1 - fa;   // 倒序
        int x1 = x_start + (seg * x_range) / VALVE_COUNT;
        int x2 = x_start + ((seg + 1) * x_range) / VALVE_COUNT - 1;
        if (x2 >= g_Samples) x2 = g_Samples - 1;

        g_valveMap[fa].x1 = x1;
        g_valveMap[fa].x2 = x2;
    }
    g_valveMapReady = true;

    // 打印映射表
    std::cout << "[Eject] 阀号映射表已生成（按比例均匀分布，"
        << "VALVE_COUNT=" << VALVE_COUNT
        << "，像素范围 [" << x_start << "~" << x_end
        << "] 共 " << x_range << "px）" << std::endl;
    std::cout << "  每阀平均 = " << (double)x_range / VALVE_COUNT << " 像素" << std::endl;

    // 打印前 5 个阀
    for (int i = 0; i < 5 && i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "px)" << std::endl;
    }
    std::cout << "  ..." << std::endl;
    // 打印中间几个
    for (int i = 95; i <= 97; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "px)" << std::endl;
    }
    std::cout << "  ..." << std::endl;
    // 打印最后几个
    for (int i = VALVE_COUNT - 5; i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "px)" << std::endl;
    }
}

// ============================================================================
// Start_send: 打开串口
// ============================================================================
bool Start_send()
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
    std::cout << "[Eject] 像素偏移: 前=" << g_frontOffset
        << " 后=" << g_backOffset
        << " 有效范围=[" << g_frontOffset << "," << (g_Samples - g_backOffset) << "]"
        << std::endl;
    std::cout << "[Eject] 阀总数=" << VALVE_COUNT
        << " 吹气=" << g_duration << "ms" << std::endl;
    std::cout << "[Eject] 模式: 按帧动态补偿  g_totalDelay=" << g_totalDelay
        << "ms  g_valveThreshold=" << g_valveThreshold << std::endl;

    //★ 启动时一次性生成阀号映射表（含倒序映射）★
      BuildValveMap();
    return true;
}

void Stop_send()
{
    if (g_hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hSerial);
        g_hSerial = INVALID_HANDLE_VALUE;
        std::cout << "[Eject] 串口已关闭" << std::endl;
    }
}

// ============================================================================
// ProcessOneFrame: 处理一帧的 640 像素分类结果（带日志）
//
// 流程：
//   Step 1:    算 192 阀状态
//   Step 1.5:  帧激活判定（连续目标像素 < 阈值则整帧抑制）★ v3.4 改连续版
//   Step 2:    中间 4 像素阀横向膨胀
//   Step 3:    合并连续阀号成区间
//   Step 4:    算 actualDelay
//   Step 5:    一次 WriteFile 发送
//   Step 6:    写日志
// ============================================================================
void ProcessOneFrame(const char* frameTags, unsigned long long frameTickMs)
{
    if (g_hSerial == INVALID_HANDLE_VALUE) return;
    if (!g_valveMapReady) return;

    g_currentFrameTick.store(frameTickMs);

    // ===== Step 1: 算 192 阀状态 + 诊断统计 =====
    bool valve_trigger[VALVE_COUNT] = { false };
    int rawClassified = 0;

    // 诊断统计（每 1000 帧采样一次）
    static int diag_frameCnt = 0;
    bool isDiagFrame = (++diag_frameCnt % 1000 == 0);

    int diag_pixCls0 = 0;
    int diag_pixCls1 = 0;
    int diag_pixCls2 = 0;
    int diag_pixOther = 0;
    int diag_valveHit1 = 0;
    int diag_valveHit2 = 0;
    int diag_valveHit3 = 0;

    for (int fa = 0; fa < VALVE_COUNT; fa++) {
        int x1 = g_valveMap[fa].x1;
        int x2 = g_valveMap[fa].x2;
        int valveWidth = x2 - x1 + 1;   // ★ 关键：本阀的实际像素宽度

        int hitCount = 0;
        for (int x = x1; x <= x2; x++) {
            int cls = (unsigned char)frameTags[x];

            // 诊断：统计像素分类（仅在采样帧）
            if (isDiagFrame) {
                if (cls == 0) diag_pixCls0++;
                else if (cls == 1) diag_pixCls1++;
                else if (cls == 2) diag_pixCls2++;
                else diag_pixOther++;
            }

            if (cls > 0 && cls <= 12 && sel_type[cls - 1] == 1) {
                hitCount++;
            }
        }

        rawClassified += hitCount;

        // ===== 阀触发判定 =====
        // 优先用比例阈值（按阀宽自适应）
        if (g_valveThresholdRatio > 0) {
            valve_trigger[fa] = (hitCount * 100 >= valveWidth * g_valveThresholdRatio);
        }
        else {
            valve_trigger[fa] = (hitCount >= g_valveThreshold);
        }

        // 诊断：统计阀命中分布（仅在采样帧）
        if (isDiagFrame) {
            if (hitCount >= 1) diag_valveHit1++;
            if (hitCount >= 2) diag_valveHit2++;
            if (hitCount >= 3) diag_valveHit3++;
        }
    }

    // ===== Step 1.5: 帧激活判定（连续像素版）★ v3.4 =====
    // 扫描整帧 640 像素，找"最长连续目标像素段"
    // 只要最长连续段 < 阈值，整帧抑制（屏蔽分散的斑点像素）
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

        // 最长连续段不够 → 整帧抑制
        if (maxRun < g_frameActivateThreshold) {
            memset(valve_trigger, 0, sizeof(valve_trigger));

            // 抑制计数（只对"有目标但被抑制"的帧计数，避免空帧刷屏）
            if (rawClassified > 0) {
                static int g_suppressCount = 0;
                g_suppressCount++;
                if (g_suppressCount % 100 == 1) {
                    std::cout << "[Frame] 已抑制 " << g_suppressCount
                        << " 帧（最长连续=" << maxRun
                        << " < " << g_frameActivateThreshold
                        << "，rawClassified=" << rawClassified << "）"
                        << std::endl;
                }
            }
        }
    }

    // ===== 写诊断到 event.log（每 1000 帧一次）=====
    if (isDiagFrame) {
        LogStats(diag_frameCnt,
            diag_pixCls0, diag_pixCls1, diag_pixCls2, diag_pixOther,
            diag_valveHit1, diag_valveHit2, diag_valveHit3,
            g_valveThreshold);
    }

    // ===== Step 2: 中间阀横向膨胀（不变）=====
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

    // ===== Step 3 + 4 + 5: 合并区间 + 算延迟 + 打包发送 =====
    unsigned char txBuf[VALVE_COUNT * 7];
    int txLen = 0;
    int cmdCount = 0;

    unsigned long long now = NowMs();
    long long elapsed = (long long)(now - frameTickMs);
    long long actualDelay = (long long)g_totalDelay - elapsed;
    bool wasNegative = false;
    if (actualDelay < 0) { actualDelay = 0; wasNegative = true; }
    if (actualDelay > 65535) actualDelay = 65535;
    unsigned short delay = (unsigned short)actualDelay;

    int currentBatch = g_batchNo.load();

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

        int hitPixels = 0;
        for (int fa = segStart; fa <= segEnd; fa++) {
            int x1 = g_valveMap[fa].x1;
            int x2 = g_valveMap[fa].x2;
            for (int x = x1; x <= x2; x++) {
                int cls = (unsigned char)frameTags[x];
                if (cls > 0 && cls <= 12 && sel_type[cls - 1] == 1) hitPixels++;
            }
        }
        LogValveDetected(currentBatch, valveStart, valveEnd, hitPixels);

        unsigned char* p = txBuf + txLen;
        p[0] = 0xFF;
        p[1] = (unsigned char)valveStart;
        p[2] = (unsigned char)valveEnd;
        p[3] = (unsigned char)(delay >> 8);
        p[4] = (unsigned char)(delay & 0xFF);
        p[5] = (unsigned char)(g_duration >> 8);
        p[6] = (unsigned char)(g_duration & 0xFF);
        txLen += 7;
        cmdCount++;

        i = segEnd + 1;
    }

    // ===== Step 5: 一次性 WriteFile =====
    int writeTime = 0;
    bool writeOk = true;
    if (txLen > 0) {
        unsigned long long t_before_write = NowMs();
        DWORD bytesWritten = 0;
        BOOL ok = WriteFile(g_hSerial, txBuf, txLen, &bytesWritten, NULL);
        unsigned long long t_after_write = NowMs();
        writeTime = (int)(t_after_write - t_before_write);
        writeOk = (ok && bytesWritten == (DWORD)txLen);

        if (!ok) {
            std::cerr << "[Eject] 串口写入失败, 错误码: " << GetLastError() << std::endl;
        }
    }

    // ===== Step 6: 写日志 =====
    {
        int j = 0;
        int idx = 0;
        while (j < VALVE_COUNT && idx < cmdCount) {
            if (!valve_trigger[j]) { j++; continue; }
            int segStart = j, segEnd = j;
            while (segEnd + 1 < VALVE_COUNT && valve_trigger[segEnd + 1]) segEnd++;

            LogValveFiredFrame(segStart + 1, segEnd + 1, g_duration, delay,
                (int)elapsed, writeTime, wasNegative, writeOk);

            j = segEnd + 1;
            idx++;
        }
    }

    LogBatchSummary(currentBatch, rawClassified, rawClassified, cmdCount);

    if (wasNegative) {
        static int g_lateCount = 0;
        g_lateCount++;
        if (g_lateCount % 100 == 1) {
            std::cerr << "[WARN] actualDelay 被截断为 0 已累计 " << g_lateCount
                << " 次，建议增大 g_totalDelay" << std::endl;
        }
    }

    if (writeTime > 5) {
        static int g_slowWriteCount = 0;
        g_slowWriteCount++;
        if (g_slowWriteCount % 50 == 1) {
            std::cerr << "[WARN] 串口 WriteFile 耗时 " << writeTime
                << "ms (累计 " << g_slowWriteCount << " 次>5ms)" << std::endl;
        }
    }
}
// ============================================================================
// UDP 参数接收线程
//   仍然接收操作屏的数据包（保持兼容），但 v2 中：
//     - sel_type[] 仍生效
//     - percent[]、fa_ctl 接收但不使用
// ============================================================================
typedef unsigned char byte;

void UDP_receive_thread()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Eject] WSAStartup 失败" << std::endl;
        return;
    }

    SOCKET udpReceive = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpReceive == INVALID_SOCKET) {
        std::cerr << "[Eject] 接收套接字创建失败" << std::endl;
        WSACleanup();
        return;
    }

    int reuseAddr = 1;
    setsockopt(udpReceive, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuseAddr), sizeof(reuseAddr));

    sockaddr_in serverAddr1{};
    serverAddr1.sin_family = AF_INET;
    serverAddr1.sin_port = htons(8080);
    serverAddr1.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(udpReceive, reinterpret_cast<sockaddr*>(&serverAddr1),
        sizeof(serverAddr1)) == SOCKET_ERROR) {
        std::cerr << "[Eject] UDP 绑定失败" << std::endl;
        closesocket(udpReceive);
        WSACleanup();
        return;
    }

    std::vector<byte> buf(1024);
    while (running) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        int recvLen = recvfrom(udpReceive,
            reinterpret_cast<char*>(buf.data()),
            (int)buf.size(), 0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen);
        if (recvLen > 0 && buf[0] == 2 && buf[1] == 0x2a) {
            sendok = buf[5];
            // for (int i = 0; i < 12; i++) {
            //     sel_type[i] = buf[12 + i * 2];   // ← 临时注释
            //     percent[i] = buf[13 + i * 2];
            // }
            // fa_ctl = buf[40];
        }
    }

    closesocket(udpReceive);
    WSACleanup();
}