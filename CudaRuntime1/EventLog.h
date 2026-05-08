#pragma once
// ============================================================================
// EventLog.h
// 高频事件日志（线程安全，毫秒精度时间戳）
//
// v2 改动：
//   1. 新增 LogValveFiredFrame（按帧的命令日志，去掉 gpuTime/postGpu 字段）
//   2. 旧的 LogValveFired/LogValveFiredCompensated 标记为 deprecated 但保留
//      （便于回滚或对照测试）
//   3. flush 策略：默认不 flush（让 OS 管理），仅在 LATE 时 flush 一次保住诊断
// ============================================================================
#include <string>
#include <atomic>

bool StartEventLog(const std::string& filename);
void StopEventLog();

// A. 获取到一批数据
void LogDataAcquired(int batchNo, int frameCount, int totalPixels);

// B. 找到要吹的阀（一段连续阀号）
void LogValveDetected(int batchNo, int valveStart, int valveEnd, int hitPixels);

// C. 实际发送吹气命令（v1 老路径：固定 delay；保留兼容）
void LogValveFired(int valveStart, int valveEnd,
    unsigned short duration, unsigned short delay,
    bool success);

// C+ . v1 旧版（批中点延迟）：保留以备回滚使用
void LogValveFiredCompensated(int valveStart, int valveEnd,
    unsigned short duration,
    unsigned short actualDelay,
    int gpuTimeMs,
    int totalElapsedMs,
    int postGpuLagMs,
    int writeTimeMs,
    bool wasNegative,
    bool success);

// ★★★ v2 新增：按帧版本（每帧拍摄→命令发出）★★★
//   字段精简：去掉 gpuTime / postGpu（按帧已无意义）
//   新字段：elapsed = 这一帧拍摄到命令发出的总耗时
void LogValveFiredFrame(int valveStart, int valveEnd,
    unsigned short duration,
    unsigned short actualDelay,         // 实际下发给 FPGA 的延迟
    int totalElapsedMs,                 // 这一帧拍摄 → 命令发出
    int writeTimeMs,                    // WriteFile 实际耗时（一帧整批）
    bool wasNegative,                   // 补偿不及，已截断为 0
    bool success);

// D. 一帧/一批结束的汇总
void LogBatchSummary(int batchNo, int rawClassified, int afterFilter, int valveCmdCount);

// 全局批次号
extern std::atomic<int> g_batchNo;

void LogStats(int frameNo,
    int pixCls0, int pixCls1, int pixCls2, int pixOther,
    int valveHit1, int valveHit2, int valveHit3,
    int threshold);