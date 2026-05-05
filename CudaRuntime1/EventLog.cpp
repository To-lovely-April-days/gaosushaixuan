// ============================================================================
// EventLog.cpp
//
// v2 改动：
//   1. 默认不 flush（让 OS buffer 管理，省 IO 开销）
//   2. 新增 LogValveFiredFrame（按帧版本）
//   3. 仅在 [LATE!] 出现时 flush 一次，保证诊断信息不丢
//   4. 旧的 LogValveFired/LogValveFiredCompensated 保留（兼容）
// ============================================================================
#include "EventLog.h"
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <windows.h>

static std::ofstream g_logFile;
static std::mutex g_logMutex;
static bool g_logEnabled = false;
static std::chrono::steady_clock::time_point g_startTime;

// ============================================================================
// 时间戳工具：返回 "HH:MM:SS.mmm  +Nms" 格式
// ============================================================================
static std::string getTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_startTime).count();

    char buf[64];
    sprintf_s(buf, "%02d:%02d:%02d.%03d  +%lldms",
        tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count(), elapsed);
    return std::string(buf);
}

// ============================================================================
// StartEventLog: 启动日志（覆盖模式打开文件）
// ============================================================================
bool StartEventLog(const std::string& filename)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile.open(filename, std::ios::out | std::ios::trunc);
    if (!g_logFile.is_open()) return false;
    g_startTime = std::chrono::steady_clock::now();
    g_logEnabled = true;

    g_logFile << "==== Event Log started ====" << std::endl;
    g_logFile << "Format: [time] [type] details" << std::endl;
    g_logFile << "  ACQ    = data batch acquired" << std::endl;
    g_logFile << "  DETECT = valve segment detected" << std::endl;
    g_logFile << "  FIRE   = serial cmd sent (legacy fixed delay)" << std::endl;
    g_logFile << "  FIREC  = serial cmd sent (legacy batch-mid compensated)" << std::endl;
    g_logFile << "  FIREF  = serial cmd sent (per-frame compensated, v2)" << std::endl;
    g_logFile << "  SUMRY  = batch summary" << std::endl;
    g_logFile << "===========================" << std::endl;
    g_logFile.flush();   // 头部需要立即可见
    return true;
}

// ============================================================================
// StopEventLog: 停止日志（关闭文件，强制刷盘）
// ============================================================================
void StopEventLog()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile << "==== Event Log stopped ====" << std::endl;
        g_logFile.flush();
        g_logFile.close();
    }
    g_logEnabled = false;
}

// ============================================================================
// LogDataAcquired: 一批数据采集完成
// 不 flush（高频事件，让 OS buffer）
// ============================================================================
void LogDataAcquired(int batchNo, int frameCount, int totalPixels)
{
    if (!g_logEnabled) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile << "[" << getTimestamp() << "] ACQ    "
        << "batch=" << batchNo
        << " frames=" << frameCount
        << " pixels=" << totalPixels
        << "\n";   // 用 \n 而不是 endl，避免隐式 flush
}

// ============================================================================
// LogValveDetected: 检测到一段要吹的阀号
// 不 flush
// ============================================================================
void LogValveDetected(int batchNo, int valveStart, int valveEnd, int hitPixels)
{
    if (!g_logEnabled) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile << "[" << getTimestamp() << "] DETECT "
        << "batch=" << batchNo
        << " valves=[" << valveStart << "~" << valveEnd << "]"
        << " width=" << (valveEnd - valveStart + 1)
        << " hitPixels=" << hitPixels
        << "\n";
}

// ============================================================================
// LogValveFired: 老路径（固定 delay），保留兼容，不 flush
// ============================================================================
void LogValveFired(int valveStart, int valveEnd,
    unsigned short duration, unsigned short delay,
    bool success)
{
    if (!g_logEnabled) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile << "[" << getTimestamp() << "] FIRE   "
        << "valves=[" << valveStart << "~" << valveEnd << "]"
        << " duration=" << duration << "ms"
        << " delay=" << delay << "ms"
        << " result=" << (success ? "OK" : "FAIL")
        << "\n";
}

// ============================================================================
// LogValveFiredCompensated: v1 老版（批中点延迟）保留兼容
// 不 flush（除非 LATE）
// ============================================================================
void LogValveFiredCompensated(int valveStart, int valveEnd,
    unsigned short duration, unsigned short actualDelay,
    int gpuTimeMs, int totalElapsedMs, int postGpuLagMs,
    int writeTimeMs,
    bool wasNegative, bool success)
{
    if (!g_logEnabled) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile << "[" << getTimestamp() << "] FIREC  "
        << "valves=[" << valveStart << "~" << valveEnd << "]"
        << " duration=" << duration << "ms"
        << " actualDelay=" << actualDelay << "ms"
        << " gpuTime=" << gpuTimeMs << "ms"
        << " elapsed=" << totalElapsedMs << "ms"
        << " postGpu=" << postGpuLagMs << "ms"
        << " writeTime=" << writeTimeMs << "ms"
        << (wasNegative ? " [LATE!]" : "")
        << " result=" << (success ? "OK" : "FAIL")
        << "\n";
    if (wasNegative) g_logFile.flush();   // LATE 才 flush
}

// ============================================================================
// LogValveFiredFrame: v2 新版（按帧补偿）
// 字段：actualDelay、elapsed（这帧拍摄→命令发出）、writeTime
// 不 flush（除非 LATE）
// ============================================================================
void LogValveFiredFrame(int valveStart, int valveEnd,
    unsigned short duration,
    unsigned short actualDelay,
    int totalElapsedMs,
    int writeTimeMs,
    bool wasNegative, bool success)
{
    if (!g_logEnabled) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile << "[" << getTimestamp() << "] FIREF  "
        << "valves=[" << valveStart << "~" << valveEnd << "]"
        << " duration=" << duration << "ms"
        << " actualDelay=" << actualDelay << "ms"
        << " elapsed=" << totalElapsedMs << "ms"
        << " writeTime=" << writeTimeMs << "ms"
        << (wasNegative ? " [LATE!]" : "")
        << " result=" << (success ? "OK" : "FAIL")
        << "\n";
    if (wasNegative) g_logFile.flush();   // LATE 才 flush，保住诊断
}

// ============================================================================
// LogBatchSummary: 一批/一帧汇总
// 不 flush
// ============================================================================
void LogBatchSummary(int batchNo, int rawClassified, int afterFilter, int valveCmdCount)
{
    if (!g_logEnabled) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logFile << "[" << getTimestamp() << "] SUMRY  "
        << "batch=" << batchNo
        << " raw=" << rawClassified
        << " afterFilter=" << afterFilter
        << " filtered=" << (rawClassified - afterFilter)
        << " cmdSent=" << valveCmdCount
        << "\n";
}