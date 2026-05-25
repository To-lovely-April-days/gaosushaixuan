// ============================================================================
// 文件名：PerfLog.h
// 作用  ：异步性能日志模块
//
// 设计原则：
//   - 热路径上只做"格式化 + push 到环形缓冲"，几百纳秒级别
//   - 后台独立线程负责写文件 (带 buffer, 不每条都 flush)
//   - 缓冲满了直接丢弃 + 计数, 绝不阻塞主流程
//   - 输出到独立文件 perf.log, 不跟 unify_gpu.log / event.log 抢锁
//
// 使用：
//   PerfLogInit("perf.log");
//   ...
//   PERF_LOG("threadFunction h2d=%lldus calc=%lldus d2h=%lldus", a, b, c);
//   ...
//   PerfLogShutdown();
// ============================================================================
#pragma once
#include <cstdarg>

// 初始化 (开线程, 打开文件)
bool PerfLogInit(const char* filename);

// 关闭 (停线程, 把缓冲剩余日志刷盘, 关文件)
void PerfLogShutdown();

// 写一条日志 (异步, 不阻塞)
//   max 长度 220 字节, 超长截断
//   线程安全
void PerfLog(const char* fmt, ...);

// 宏: 让调用方写起来更顺手
#define PERF_LOG(fmt, ...)  PerfLog(fmt, ##__VA_ARGS__)

// 打印统计信息 (丢弃多少条, 队列水位等)
void PerfLogPrintStats();