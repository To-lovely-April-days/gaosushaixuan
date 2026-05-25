// ============================================================================
// 文件名：PerfLog.cpp
// 作用  ：异步性能日志模块的实现
//
// 实现细节：
//   - 用 SPSC 环形缓冲 (固定 4096 槽, 每槽 256 字节)
//   - 生产者 (任意业务线程): 写入 head, 原子推进
//   - 消费者 (单个后台线程): 读 tail, 批量写文件后 fflush
//   - 缓冲满直接丢弃, 用 atomic 计数器记录
//   - 后台线程每 100ms 主动 flush 一次, 防止程序崩溃丢日志
// ============================================================================
#include "PerfLog.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// 配置
// ============================================================================
#define PERF_RING_SLOTS    4096      // 环形缓冲槽数 (必须是 2 的幂)
#define PERF_SLOT_BYTES    256       // 每槽容量 (含时间戳)
#define PERF_FLUSH_MS      100       // 后台线程多久 flush 一次

static_assert((PERF_RING_SLOTS& (PERF_RING_SLOTS - 1)) == 0,
    "PERF_RING_SLOTS must be a power of 2");

// ============================================================================
// 全局状态
// ============================================================================
struct PerfSlot {
    char data[PERF_SLOT_BYTES];
    int  len;       // 实际有效字节数 (含换行)
};

static PerfSlot* g_ring = nullptr;
static std::atomic<unsigned long long> g_head{ 0 };   // 生产者写入位置
static std::atomic<unsigned long long> g_tail{ 0 };   // 消费者读出位置

static FILE* g_file = nullptr;
static std::thread g_thread;
static std::atomic<bool> g_running{ false };

static std::atomic<unsigned long long> g_dropCount{ 0 };
static std::atomic<unsigned long long> g_writtenCount{ 0 };
static std::atomic<unsigned int>       g_maxQueueDepth{ 0 };

// ============================================================================
// 时间戳 (类似 UnifyLog 风格)
// ============================================================================
static int FormatTimestamp(char* buf, int bufLen)
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto sub_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    return snprintf(buf, bufLen, "[%02d:%02d:%02d.%03d] ",
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)sub_ms);
}

// ============================================================================
// 后台写文件线程
// ============================================================================
static void PerfLogThread()
{
#ifdef _WIN32
    // 后台线程优先级低一些, 不抢业务
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    auto lastFlush = std::chrono::steady_clock::now();

    while (g_running.load(std::memory_order_acquire)) {
        // ---- 批量读取 ----
        unsigned long long head = g_head.load(std::memory_order_acquire);
        unsigned long long tail = g_tail.load(std::memory_order_relaxed);

        int written = 0;
        while (tail < head) {
            unsigned int idx = (unsigned int)(tail & (PERF_RING_SLOTS - 1));
            PerfSlot& s = g_ring[idx];
            if (g_file && s.len > 0) {
                fwrite(s.data, 1, s.len, g_file);
                g_writtenCount.fetch_add(1, std::memory_order_relaxed);
                written++;
            }
            tail++;
            // 每次循环都更新 tail, 让生产者及时看到空间
            if (written % 64 == 0) {
                g_tail.store(tail, std::memory_order_release);
            }
        }
        g_tail.store(tail, std::memory_order_release);

        // ---- 定期 flush ----
        auto now = std::chrono::steady_clock::now();
        long long ms_since_flush = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastFlush).count();
        if (g_file && (written > 0 || ms_since_flush >= PERF_FLUSH_MS)) {
            fflush(g_file);
            lastFlush = now;
        }

        // ---- 没活干就睡一下 ----
        if (written == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // ---- 退出前最后清空 ----
    unsigned long long head = g_head.load(std::memory_order_acquire);
    unsigned long long tail = g_tail.load(std::memory_order_relaxed);
    while (tail < head) {
        unsigned int idx = (unsigned int)(tail & (PERF_RING_SLOTS - 1));
        PerfSlot& s = g_ring[idx];
        if (g_file && s.len > 0) {
            fwrite(s.data, 1, s.len, g_file);
        }
        tail++;
    }
    g_tail.store(tail, std::memory_order_release);
    if (g_file) fflush(g_file);
}

// ============================================================================
// 公开接口
// ============================================================================
bool PerfLogInit(const char* filename)
{
    if (g_running.load()) return true;

    g_ring = new (std::nothrow) PerfSlot[PERF_RING_SLOTS];
    if (!g_ring) {
        fprintf(stderr, "[PerfLog] alloc ring failed\n");
        return false;
    }
    memset(g_ring, 0, sizeof(PerfSlot) * PERF_RING_SLOTS);
    g_head.store(0);
    g_tail.store(0);
    g_dropCount.store(0);
    g_writtenCount.store(0);
    g_maxQueueDepth.store(0);

    g_file = fopen(filename, "a");   // 追加模式
    if (!g_file) {
        fprintf(stderr, "[PerfLog] open %s failed\n", filename);
        delete[] g_ring;
        g_ring = nullptr;
        return false;
    }
    // setvbuf: 用 64KB block buffer, 减少 fwrite 系统调用次数
    static char filebuf[65536];
    setvbuf(g_file, filebuf, _IOFBF, sizeof(filebuf));

    g_running.store(true, std::memory_order_release);
    g_thread = std::thread(PerfLogThread);

    // 写一条启动标记
    PerfLog("==== PerfLog started, ring=%d slots, slot=%d bytes ====",
        PERF_RING_SLOTS, PERF_SLOT_BYTES);
    return true;
}

void PerfLogShutdown()
{
    if (!g_running.load()) return;

    // 写一条结束标记 (异步, 会被后台线程刷掉)
    PerfLog("==== PerfLog shutting down, written=%llu dropped=%llu maxQ=%u ====",
        (unsigned long long)g_writtenCount.load(),
        (unsigned long long)g_dropCount.load(),
        (unsigned int)g_maxQueueDepth.load());

    g_running.store(false, std::memory_order_release);
    if (g_thread.joinable()) g_thread.join();

    if (g_file) {
        fflush(g_file);
        fclose(g_file);
        g_file = nullptr;
    }
    delete[] g_ring;
    g_ring = nullptr;
}

void PerfLog(const char* fmt, ...)
{
    if (!g_running.load(std::memory_order_acquire)) return;

    // 1) 申请一个槽 (原子取序号, 不阻塞)
    unsigned long long head = g_head.fetch_add(1, std::memory_order_acq_rel);
    unsigned long long tail = g_tail.load(std::memory_order_acquire);

    // 2) 检查队列是否满
    if (head - tail >= PERF_RING_SLOTS) {
        // 满了, 丢弃, 计数
        g_dropCount.fetch_add(1, std::memory_order_relaxed);
        // 注意: head 已经 +1 了, 但这个槽我们不写, 后台会读到空 (len=0) 槽
        // 但实际上槽内可能还有旧数据, 所以我们写一个 len=0 标记
        unsigned int idx = (unsigned int)(head & (PERF_RING_SLOTS - 1));
        g_ring[idx].len = 0;
        return;
    }

    // 3) 更新水位记录
    unsigned int depth = (unsigned int)(head - tail + 1);
    unsigned int old_max = g_maxQueueDepth.load(std::memory_order_relaxed);
    while (depth > old_max &&
        !g_maxQueueDepth.compare_exchange_weak(old_max, depth,
            std::memory_order_relaxed)) {
    }

    // 4) 写入槽
    unsigned int idx = (unsigned int)(head & (PERF_RING_SLOTS - 1));
    PerfSlot& s = g_ring[idx];

    int tsLen = FormatTimestamp(s.data, PERF_SLOT_BYTES);
    if (tsLen < 0 || tsLen >= PERF_SLOT_BYTES) { s.len = 0; return; }

    va_list args;
    va_start(args, fmt);
    int bodyLen = vsnprintf(s.data + tsLen,
        PERF_SLOT_BYTES - tsLen - 2, // 留 \n\0
        fmt, args);
    va_end(args);
    if (bodyLen < 0) { s.len = 0; return; }
    int total = tsLen + bodyLen;
    if (total > PERF_SLOT_BYTES - 2) total = PERF_SLOT_BYTES - 2;

    s.data[total] = '\n';
    s.data[total + 1] = '\0';
    s.len = total + 1;   // 包含 \n, 不含 \0
}

void PerfLogPrintStats()
{
    fprintf(stderr, "[PerfLog] written=%llu dropped=%llu maxQ=%u\n",
        (unsigned long long)g_writtenCount.load(),
        (unsigned long long)g_dropCount.load(),
        (unsigned int)g_maxQueueDepth.load());
}