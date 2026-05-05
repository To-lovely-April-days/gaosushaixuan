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
std::atomic<unsigned long long> g_currentFrameTick{ 0 };
// 中间 4 像素阀（阀 65~128）触发时的横向膨胀范围（阀数）
//   0 = 不膨胀
//   1 = ±1 阀（吹 3 个：左 1 + 自己 + 右 1）
//   2 = ±2 阀（吹 5 个：左 2 + 自己 + 右 2，默认）
//   3 = ±3 阀（吹 7 个）
// 注意：膨胀允许跨界（阀 65 可以扩到阀 64 即 3 像素区，反之亦然）
int g_centerValveInflate = 2;

// ===== 阀冷却机制（v2.1 新增）=====
// 解决问题：一颗石头横跨多帧时，每帧都发命令导致 FPGA 接收 buffer 堆积，
//          最终吹气时机错乱漏吹。
//
// 工作原理：
//   - 记录每个阀"上次发命令的时刻"
//   - 记录每个阀"上一帧的触发状态"
//   - 本帧某阀想吹时：
//       a) 如果上一帧也吹了（持续触发）→ 跳过，不重复发
//       b) 如果上次发命令距今 < 冷却期 → 跳过，不重复发
//       c) 否则（新触发 + 不在冷却期）→ 允许发命令，记录时间
//
// 推荐 g_valveCooldownMs = g_duration（吹气时长），即"上次吹气结束就允许下次"
static unsigned long long g_lastFireTimeMs[VALVE_COUNT] = { 0 };
static bool               g_lastValveTrigger[VALVE_COUNT] = { false };
int g_valveCooldownMs = 30;   // 阀冷却期（ms），默认等于 g_duration

// 统计字段（仅用于运行时观察，不影响功能）
static std::atomic<unsigned long long> g_totalSuppressedByContinuous{ 0 };  // 因"持续触发"被跳过的次数
static std::atomic<unsigned long long> g_totalSuppressedByCooldown{ 0 };    // 因"冷却期"被跳过的次数
static std::atomic<unsigned long long> g_totalFired{ 0 };                    // 实际发出的命令数
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

// ============================================================================
// BuildValveMap: 启动时一次性生成阀号映射表（含倒序映射）
//
// v2.3：3/4/3 分布 + 中间阀膨胀（搭配 g_centerValveInflate 使用）
//   阀 1~64    : 每阀 3 像素（共 192 像素）— 图像右侧
//   阀 65~128  : 每阀 4 像素（共 256 像素）— 图像中间（膨胀目标区）
//   阀 129~192 : 每阀 3 像素（共 192 像素）— 图像左侧
//   合计 640 像素，无重叠无遗漏
//
// 倒序映射：物理阀号 fa（0~191）对应图像中第 (191-fa) 段像素区间
//   阀 1（fa=0）   → 图像最右侧
//   阀 192（fa=191）→ 图像最左侧
//
// 物理图像（像素 0 在左 / 639 在右）从左到右依次是：
//   阀 192~129（左侧 64 阀，每阀 3 像素）
//   阀 128~65  （中间 64 阀，每阀 4 像素）← 这部分膨胀
//   阀 64~1    （右侧 64 阀，每阀 3 像素）
// ============================================================================
void BuildValveMap()
{
    int x_start = g_frontOffset;
    int x_end = g_Samples - g_backOffset;
    int x_range = x_end - x_start;

    if (x_start < 0) x_start = 0;
    if (x_end > g_Samples) x_end = g_Samples;
    if (x_range != 640) {
        std::cerr << "[Eject] BuildValveMap 警告：当前 3/4/3 布局假设 x_range=640，"
            << "实际 x_range=" << x_range << "（g_Samples=" << g_Samples
            << " - g_frontOffset=" << g_frontOffset
            << " - g_backOffset=" << g_backOffset << "）" << std::endl;
        // 不致命，继续构建
    }
    if (x_range <= 0) {
        std::cerr << "[Eject] BuildValveMap 失败：像素范围无效" << std::endl;
        g_valveMapReady = false;
        return;
    }

    // ===== 构建图像从左到右的"段"列表 =====
    // 共 192 段，从像素 x_start 开始往右排
    // 段 0~63   : 每段 3 像素（图像左侧）→ 对应物理阀 192~129
    // 段 64~127 : 每段 4 像素（图像中间）→ 对应物理阀 128~65
    // 段 128~191: 每段 3 像素（图像右侧）→ 对应物理阀 64~1
    int seg_x1[VALVE_COUNT];
    int seg_x2[VALVE_COUNT];
    int cur = x_start;
    for (int seg = 0; seg < VALVE_COUNT; seg++) {
        int width;
        if (seg < 64)              width = 3;
        else if (seg < 128)        width = 4;
        else                       width = 3;

        seg_x1[seg] = cur;
        seg_x2[seg] = cur + width - 1;
        if (seg_x2[seg] >= g_Samples) seg_x2[seg] = g_Samples - 1;
        cur += width;
    }

    // ===== 倒序映射：物理阀 fa 对应图像段 (191 - fa) =====
    for (int fa = 0; fa < VALVE_COUNT; fa++) {
        int seg = VALVE_COUNT - 1 - fa;
        g_valveMap[fa].x1 = seg_x1[seg];
        g_valveMap[fa].x2 = seg_x2[seg];
    }
    g_valveMapReady = true;

    // ===== 打印映射表（验证用）=====
    std::cout << "[Eject] 阀号映射表已生成（v2.3 3/4/3 整数分布，"
        << "VALVE_COUNT=" << VALVE_COUNT
        << "，像素范围 [" << x_start << "~" << x_end << "]）：" << std::endl;
    std::cout << "  分布: 阀1~64=3px / 阀65~128=4px(中间，膨胀目标) / 阀129~192=3px"
        << std::endl;
    std::cout << "  中间膨胀: g_centerValveInflate=" << g_centerValveInflate
        << " (即阀 65~128 触发时向两侧各扩 " << g_centerValveInflate << " 个阀)"
        << std::endl;

    // 打印前 5 个阀
    for (int i = 0; i < 5 && i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "像素)" << std::endl;
    }
    std::cout << "  ..." << std::endl;
    // 打印阀 64~66（边界处，看 3→4 切换）
    for (int i = 63; i <= 65 && i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "像素)" << std::endl;
    }
    std::cout << "  ..." << std::endl;
    // 打印阀 128~130（边界处，看 4→3 切换）
    for (int i = 127; i <= 129 && i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "像素)" << std::endl;
    }
    std::cout << "  ..." << std::endl;
    // 打印最后 5 个阀
    for (int i = VALVE_COUNT - 5; i < VALVE_COUNT; i++) {
        int width = g_valveMap[i].x2 - g_valveMap[i].x1 + 1;
        std::cout << "  阀 " << (i + 1) << ": 像素 ["
            << g_valveMap[i].x1 << " ~ " << g_valveMap[i].x2
            << "] (" << width << "像素)" << std::endl;
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

     std::cout << "[Eject] 阀冷却机制: g_valveCooldownMs=" << g_valveCooldownMs
         << "ms (= g_duration=" << g_duration << "ms 时即吹气一结束就允许下次)"
        << std::endl;

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
// ProcessOneFrame: 处理一帧的 640 像素分类结果（v2.4 膨胀+冷却合并版）
//
// 流程：
//   Step 1: 算 192 阀状态
//   Step 2: ★ 中间 4 像素阀横向膨胀 ★
//   Step 3: ★ 阀冷却过滤 ★ （新增，关键改动）
//   Step 4: 合并连续阀号成区间
//   Step 5: 算 actualDelay
//   Step 6: 一次 WriteFile 发送
//   Step 7: 写日志
//
// 冷却语义（关键）：
//   - 对"经过膨胀后"的 valve_trigger 做冷却过滤
//   - 每个阀如果在 g_valveCooldownMs 内已经发过命令，本帧跳过
//   - 用 g_lastFireTimeMs[fa] 记录每个阀的最近发命令时刻
//   - 冷却到期后允许下次发
//
// 注意：g_lastValveTrigger 不再用了（旧版的"持续触发"判断），
//      改用纯时间冷却 —— 更稳定
// ============================================================================
void ProcessOneFrame(const char* frameTags, unsigned long long frameTickMs)
{
    if (g_hSerial == INVALID_HANDLE_VALUE) return;
    if (!g_valveMapReady) return;

    g_currentFrameTick.store(frameTickMs);

    // ===== Step 1: 算 192 阀状态 =====
    bool valve_trigger[VALVE_COUNT] = { false };
    int rawClassified = 0;

    for (int fa = 0; fa < VALVE_COUNT; fa++) {
        int x1 = g_valveMap[fa].x1;
        int x2 = g_valveMap[fa].x2;

        int hitCount = 0;
        for (int x = x1; x <= x2; x++) {
            int cls = (unsigned char)frameTags[x];
            if (cls > 0 && cls <= 12 && sel_type[cls - 1] == 1) {
                hitCount++;
            }
        }

        rawClassified += hitCount;
        valve_trigger[fa] = (hitCount >= g_valveThreshold);
    }

    // ===== Step 2: 中间阀横向膨胀 =====
    // 中间区域：fa = [64, 127]（即阀号 65~128，4 像素阀）
    if (g_centerValveInflate > 0) {
        const int CENTER_VALVE_START = 64;
        const int CENTER_VALVE_END = 127;

        // 复制原始触发状态（避免膨胀产生的位置又被当作"原始"再扩散）
        bool original_trigger[VALVE_COUNT];
        memcpy(original_trigger, valve_trigger, sizeof(valve_trigger));

        for (int fa = CENTER_VALVE_START; fa <= CENTER_VALVE_END; fa++) {
            if (!original_trigger[fa]) continue;

            // 向左扩展
            for (int d = 1; d <= g_centerValveInflate; d++) {
                int left = fa - d;
                if (left >= 0) valve_trigger[left] = true;
                else break;
            }
            // 向右扩展
            for (int d = 1; d <= g_centerValveInflate; d++) {
                int right = fa + d;
                if (right < VALVE_COUNT) valve_trigger[right] = true;
                else break;
            }
        }
    }

    // ===== Step 3: 阀冷却过滤（v2.4 新增）=====
    // 对每个 valve_trigger=true 的阀：
    //   如果距上次发命令 < g_valveCooldownMs，则跳过（设回 false）
    //   否则记录"本帧将发命令"，更新 g_lastFireTimeMs
    //
    // 注意：g_valveCooldownMs = 0 时跳过冷却（关闭功能）
    unsigned long long now = NowMs();
    int suppressedCount = 0;

    if (g_valveCooldownMs > 0) {
        for (int fa = 0; fa < VALVE_COUNT; fa++) {
            if (!valve_trigger[fa]) continue;

            unsigned long long lastFire = g_lastFireTimeMs[fa];
            if (lastFire > 0 && (now - lastFire) < (unsigned long long)g_valveCooldownMs) {
                // 冷却中 → 跳过这个阀
                valve_trigger[fa] = false;
                suppressedCount++;
            }
            else {
                // 允许发命令 → 记录时刻
                g_lastFireTimeMs[fa] = now;
            }
        }
    }

    // ===== Step 4 + 5 + 6: 合并区间 + 算延迟 + 打包发送 =====
    unsigned char txBuf[VALVE_COUNT * 7];
    int txLen = 0;
    int cmdCount = 0;

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

        // 算该区间的命中像素数（用于 DETECT 日志）
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

        // 把这条命令塞进发送缓冲
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

    // ===== Step 6: 一次性 WriteFile =====
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

    // ===== Step 7: 写日志（保持原格式不变）=====
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

    // SUMRY 日志（每帧一条）
    LogBatchSummary(currentBatch, rawClassified, rawClassified, cmdCount);

    // ===== LATE 累计警告 =====
    if (wasNegative) {
        static int g_lateCount = 0;
        g_lateCount++;
        if (g_lateCount % 100 == 1) {
            std::cerr << "[WARN] actualDelay 被截断为 0 已累计 " << g_lateCount
                << " 次，建议增大 g_totalDelay" << std::endl;
        }
    }

    // ===== 串口慢写入告警 =====
    if (writeTime > 5) {
        static int g_slowWriteCount = 0;
        g_slowWriteCount++;
        if (g_slowWriteCount % 50 == 1) {
            std::cerr << "[WARN] 串口 WriteFile 耗时 " << writeTime
                << "ms (累计 " << g_slowWriteCount << " 次>5ms)" << std::endl;
        }
    }

    // ===== 冷却统计（每 10 秒一次到控制台，不写日志文件）=====
    static unsigned long long g_lastStatTick = 0;
    static unsigned long long g_totalSuppressed = 0;
    static unsigned long long g_totalFired = 0;
    g_totalSuppressed += suppressedCount;
    g_totalFired += cmdCount;
    if (now - g_lastStatTick >= 10000) {
        g_lastStatTick = now;
        unsigned long long total = g_totalSuppressed + g_totalFired;
        if (total > 0) {
            std::cout << "[Stats] 10s 内: 实发命令=" << g_totalFired
                << "  冷却跳过阀次=" << g_totalSuppressed
                << "  冷却率=" << (g_totalSuppressed * 100 / (g_totalSuppressed + g_totalFired))
                << "%" << std::endl;
            g_totalSuppressed = 0;
            g_totalFired = 0;
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
            for (int i = 0; i < 12; i++) {
                sel_type[i] = buf[12 + i * 2];
                percent[i] = buf[13 + i * 2];   // v2 不用，保持兼容写入
            }
            fa_ctl = buf[40];                    // v2 不用，保持兼容写入
        }
    }

    closesocket(udpReceive);
    WSACleanup();
}