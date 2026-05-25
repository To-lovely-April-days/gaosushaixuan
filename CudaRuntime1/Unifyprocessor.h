#pragma once
// ============================================================================
// 文件名：UnifyProcessor.h
// 版本  ：v4.0 (GPU 化)
// 作用  ：跨批次连通域统一像素处理器
//
// v4.0 变化：
//   - 新增 GPU 实现 (CUDA Label Equivalence 算法)
//   - UnifyProcess() 接口不变, 内部根据 g_unifyUseGpu 自动选路
//   - 保留 CPU 实现作为 fallback / 验证基准
//
// 使用方式（跟 v3.6 一样, 只多一行）：
//   1. main.cpp #include "UnifyProcessor.h"
//   2. 启动时调 UnifyInit()
//   3. 启动分选 / 停止分选时调 UnifyReset()
//   4. threadFunction_dis2 里调 UnifyProcess(in, out, N, samples)
//   5. 退出时调 UnifyShutdown()
//
//   ★ 新增: 控制 GPU/CPU 选择
//      g_unifyUseGpu = true   → 走 GPU 版 (推荐)
//      g_unifyUseGpu = false  → 走 CPU 版 (出问题时调试 fallback)
// ============================================================================

#include <cstdint>

// ============================================================================
// 全局参数（在 main.cpp 定义）
// ============================================================================

extern bool  g_unifyEnable;          // 总开关
extern bool  g_unifyUseGpu;          // ★ v4.0: true=GPU, false=CPU
extern int   g_unifyTailFrames;      // K (建议=dealCount)
extern int   g_unifyForceClassId;    // 染色目标 (1~12)
extern float g_unifyThreshold;       // 占比阈值 (0.0~1.0)
extern bool  g_unifyFillBackground;  // 填洞 (false=保守)

// ============================================================================
// 主接口（外部只需调这几个，跟 v3.6 兼容）
// ============================================================================

// 程序启动时调一次。dealCount 和 numSamples 用于 GPU 显存预分配。
//   如果 g_unifyUseGpu=true 且 GPU 初始化失败, 会自动 fallback 到 CPU。
void UnifyInit(int dealCount = 24, int numSamples = 640);

// 启动/停止分选时调（清空内部缓冲）
void UnifyReset();

// 处理一批 tags
//   inTags     : CPU 内存输入 (numFrames × numSamples)
//   outTags    : CPU 内存输出 (外部预分配)
//   返回耗时 (微秒)
//
// 内部根据 g_unifyUseGpu 自动选择 GPU 或 CPU 路径
int  UnifyProcess(const char* inTags, char* outTags,
    int numFrames, int numSamples);

// 退出清理
void UnifyShutdown();

// 打印累计性能统计 (一段时间调一次, 用于诊断)
void UnifyLogStats();

// ★ v4.2: 通用日志接口
//   线程安全, 自动加时间戳, 同时输出到控制台和 unify_gpu.log
//   供 main.cpp 等外部模块写诊断信息, 避免文件锁竞争
void UnifyLog(const char* fmt, ...);