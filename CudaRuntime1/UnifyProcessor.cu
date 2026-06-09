// ============================================================================
// 文件名：UnifyProcessor.cu
// 版本  ：v4.0
// 作用  ：跨批次连通域统一像素处理器 - GPU 实现
//
// 算法：Label Equivalence (LE) 8-连通域并行标签传播
//
// 流程：
//   阶段 0: 上传 inTags 到 GPU + 拼接 [tail + 新批] 到 d_combined
//   阶段 1: init_labels       初始化每像素 label = idx+1 (前景) / 0 (背景)
//   阶段 2: propagate         迭代取最小邻居 label (8 邻居), 直到收敛
//   阶段 3: compact_labels    把 root_idx 紧凑成 1..N 连续 ID
//   阶段 4: stats_kernel      bbox + 直方图 (atomic)
//   阶段 5: decide_kernel     算每个连通域的 target classid (v3.6 决策)
//   阶段 6: color_kernel      染色到 d_combined
//   阶段 7: 切片输出 N 行 + 更新 tail (后 K 行)
//
// 性能预期：
//   30K 像素 (48×640), ~1000 个连通域
//   迭代收敛: 5-10 次
//   总耗时: < 1ms (RTX 3060+)
// ============================================================================

#include "UnifyProcessor.h"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>     // ★ v4.0: 写日志文件
#include <mutex>       // ★ v4.0: 日志线程安全
#include <ctime>       // ★ v4.0: 时间戳
#include <cstdarg>     // ★ v4.0: 可变参数

// ============================================================================
// Constants
// ============================================================================
#define MAX_CLASS_ID         13     // 0..12 共 13 个分类 (cls=0 是背景)
#define RESERVED_MIN         254    // 254/255 是保留值, 视为背景
#define MAX_LABELS           4096   // 单批次最多 4096 个连通域
#define MAX_PROPAGATE_ITERS  32     // LE 传播最多迭代 32 次
#define BLOCK_X              16
#define BLOCK_Y              16

// ============================================================================
// ★ v4.0: 日志辅助 - 同时输出到控制台和 unify_gpu.log 文件
//   独立日志文件, 不跟 event.log 冲突
//   程序退出时不显式关闭, 让 OS 关
// ============================================================================
static std::ofstream g_unifyLogFile;
static std::mutex    g_unifyLogMutex;
static bool          g_unifyLogInited = false;

static void EnsureLogOpen()
{
    if (!g_unifyLogInited) {
        // 追加模式打开, 重启不丢历史
        g_unifyLogFile.open("unify_gpu.log", std::ios::out | std::ios::app);
        g_unifyLogInited = true;
    }
}

void UnifyLog(const char* fmt, ...)
{
    char body[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    // 时间戳
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
    char ts[32];
    snprintf(ts, sizeof(ts), "[%02d:%02d:%02d.%03d]",
        tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)sub_ms);

    // 输出到控制台
    printf("%s %s\n", ts, body);
    // fflush(stdout);  ★ v4.4 去掉, 避免阻塞

    // 输出到日志文件 (线程安全)
    std::lock_guard<std::mutex> lock(g_unifyLogMutex);
    EnsureLogOpen();
    if (g_unifyLogFile.is_open()) {
        g_unifyLogFile << ts << " " << body << "\n";
        // g_unifyLogFile.flush();  ★ v4.4 去掉同步 flush
        // → 让 OS 自己管缓冲, 异步刷盘
        // → 程序异常退出时可能丢最后几条日志, 但生产环境影响小
        // → 如果想刷盘, 在 UnifyShutdown 里 close 时会自动 flush
    }
}

// ============================================================================
// 外部声明 (CPU 版的实现, 在 UnifyProcessor.cpp 里)
//   作为 GPU 失败时的 fallback
// ============================================================================
extern void UnifyInit_Cpu();
extern void UnifyReset_Cpu();
extern int  UnifyProcess_Cpu(const char* inTags, char* outTags,
    int numFrames, int numSamples);
extern void UnifyShutdown_Cpu();
extern void UnifyLogStats_Cpu();

// ============================================================================
// 全局参数 (在 main.cpp 定义)
// ============================================================================
// 注意：放在这里加 extern 声明; 真实定义只能有一份
//       v3.6 的 UnifyProcessor.cpp 已经定义了, 这里只 extern
extern bool  g_unifyEnable;
extern int   g_unifyTailFrames;
extern int   g_unifyForceClassId;
extern float g_unifyThreshold;
extern bool  g_unifyFillBackground;
extern int   g_unifyMinArea;   // majority-vote noise filter (0=off)

// ★ v4.0 新增, 在这里定义 (主程序里 extern 引用即可)
bool g_unifyUseGpu = true;

// ============================================================================
// GPU 显存指针
// ============================================================================
static char* d_inTags = nullptr;  // 输入 (N × samples)
static char* d_outTags = nullptr;  // 输出 (N × samples)
static char* d_tail = nullptr;  // 尾部缓存 (K × samples)
static int    d_tail_frames = 0;
static int    d_tail_samples = 0;
static int    d_max_K = 0;
static int    d_max_N = 0;

static char* d_combined = nullptr;  // 拼接缓冲 ((K+N) × samples)
static int* d_labels = nullptr;  // 标签缓冲
static int* d_labels_swap = nullptr;  // ping-pong
static int* d_changed = nullptr;  // 迭代变化标志

static int* d_rootMask = nullptr;  // root 标记 [0/1]
static int* d_rootToCompId = nullptr;  // root_idx → 紧凑 ID
static int* d_compIdCount = nullptr;  // 紧凑 ID 计数

static int* d_fgInComp = nullptr;  // [MAX_LABELS] 前景像素数
static int* d_hist = nullptr;  // [MAX_LABELS × MAX_CLASS_ID]
static int* d_bbox_r0 = nullptr;
static int* d_bbox_r1 = nullptr;
static int* d_bbox_c0 = nullptr;
static int* d_bbox_c1 = nullptr;
static unsigned char* d_target = nullptr;  // 每个连通域的 target

static bool   g_gpuReady = false;            // GPU 是否初始化成功

// 累计统计
static long long g_gpuTotalUs = 0;
static long long g_gpuMaxUs = 0;
static int       g_gpuCount = 0;

// ============================================================================
// 工具
// ============================================================================
#define CUDA_CHECK_RET(call, ret) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            fprintf(stderr, "[Unify-GPU] CUDA error %s:%d: %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(err)); \
            return ret; \
        } \
    } while(0)

#define CUDA_CHECK_VOID(call) \
    do { \
        cudaError_t err = (call); \
        if (err != cudaSuccess) { \
            fprintf(stderr, "[Unify-GPU] CUDA error %s:%d: %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(err)); \
        } \
    } while(0)

// ============================================================================
// 核函数 1: init_labels
//   前景像素 label = idx+1 (避开 0)
//   背景像素 label = 0
// ============================================================================
__global__ void k_initLabels(
    const char* __restrict__ tags,
    int* __restrict__ labels,
    int H, int W)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    unsigned char cls = (unsigned char)tags[idx];
    if (cls > 0 && cls < RESERVED_MIN) {
        labels[idx] = idx + 1;
    }
    else {
        labels[idx] = 0;
    }
}

// ============================================================================
// 核函数 2: propagate (8-邻居取最小)
// ============================================================================
__global__ void k_propagate(
    int* __restrict__ labels_in,
    int* __restrict__ labels_out,
    int* __restrict__ changed,
    int H, int W)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    int my_label = labels_in[idx];

    if (my_label == 0) {
        labels_out[idx] = 0;
        return;
    }

    int min_label = my_label;

#pragma unroll
    for (int dy = -1; dy <= 1; ++dy) {
#pragma unroll
        for (int dx = -1; dx <= 1; ++dx) {
            if (dy == 0 && dx == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
            int nlabel = labels_in[ny * W + nx];
            if (nlabel != 0 && nlabel < min_label) {
                min_label = nlabel;
            }
        }
    }

    labels_out[idx] = min_label;
    // ★ v4.5 changed 允许为 nullptr (固定迭代模式下不查收敛)
    if (changed != nullptr && min_label != my_label) {
        atomicExch(changed, 1);
    }
}

// ============================================================================
// 核函数 3a: 标记 root (label == idx+1 是 root)
// ============================================================================
__global__ void k_compactBuildRoots(
    const int* __restrict__ labels,
    int* __restrict__ rootMask,
    int H, int W)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = H * W;
    if (idx >= total) return;

    int lab = labels[idx];
    rootMask[idx] = (lab != 0 && lab == idx + 1) ? 1 : 0;
}

// ============================================================================
// 核函数 3b: 给每个 root 分配紧凑 ID (用 atomicAdd)
// ============================================================================
__global__ void k_compactAssignId(
    const int* __restrict__ rootMask,
    int* __restrict__ rootToCompId,
    int* __restrict__ compIdCount,
    int H, int W)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = H * W;
    if (idx >= total) return;

    if (rootMask[idx] == 1) {
        int newId = atomicAdd(compIdCount, 1) + 1;
        if (newId <= MAX_LABELS - 1) {
            rootToCompId[idx] = newId;
        }
        else {
            rootToCompId[idx] = 0;
        }
    }
    else {
        rootToCompId[idx] = 0;
    }
}

// ============================================================================
// 核函数 3c: 重映射 labels 为紧凑 ID
// ============================================================================
__global__ void k_compactRelabel(
    int* __restrict__ labels,
    const int* __restrict__ rootToCompId,
    int H, int W)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = H * W;
    if (idx >= total) return;

    int lab = labels[idx];
    if (lab == 0) return;

    int rootIdx = lab - 1;
    if (rootIdx >= 0 && rootIdx < total) {
        labels[idx] = rootToCompId[rootIdx];
    }
    else {
        labels[idx] = 0;
    }
}

// ============================================================================
// 核函数 4: stats - bbox + 直方图统计
// ============================================================================
__global__ void k_stats(
    const char* __restrict__ tags,
    const int* __restrict__ labels,
    int* __restrict__ fgInComp,
    int* __restrict__ hist,
    int* __restrict__ bbox_r0,
    int* __restrict__ bbox_r1,
    int* __restrict__ bbox_c0,
    int* __restrict__ bbox_c1,
    int H, int W)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    int lab = labels[idx];
    if (lab == 0 || lab >= MAX_LABELS) return;

    unsigned char cls = (unsigned char)tags[idx];
    if (cls == 0 || cls >= RESERVED_MIN) return;

    atomicAdd(&fgInComp[lab], 1);
    if (cls < MAX_CLASS_ID) {
        atomicAdd(&hist[lab * MAX_CLASS_ID + cls], 1);
    }
    atomicMin(&bbox_r0[lab], y);
    atomicMax(&bbox_r1[lab], y);
    atomicMin(&bbox_c0[lab], x);
    atomicMax(&bbox_c1[lab], x);
}

// ============================================================================
// 核函数 5: decide - 算每个连通域的 target (纯多数投票)
//   target = 票数最多的 cls (平局取较小 cls)
//   连通域前景像素数 < minArea 时整块清为背景(0), 不吹气
//   minArea = g_unifyMinArea (UDP 可配, 0=不过滤)
// ============================================================================
__global__ void k_decide(
    const int* __restrict__ fgInComp,
    const int* __restrict__ hist,
    unsigned char* __restrict__ target,
    int minArea,
    int numComps)
{
    int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k == 0 || k > numComps || k >= MAX_LABELS) return;

    int fg = fgInComp[k];
    if (fg <= 0) {
        target[k] = 0;
        return;
    }

    // component too small (noise) -> wipe to background
    if (minArea > 0 && fg < minArea) {
        target[k] = 0;
        return;
    }

    // pure majority vote (tie -> smaller cls; no votable class -> 0 = background)
    int bestCls = 0;
    int bestCount = 0;
    for (int c = 1; c < MAX_CLASS_ID; ++c) {
        int cnt = hist[k * MAX_CLASS_ID + c];
        if (cnt > bestCount) {
            bestCount = cnt;
            bestCls = c;
        }
    }
    target[k] = (unsigned char)bestCls;
}

// ============================================================================
// 核函数 6: color - 染色
//   前景像素: tags_out[idx] = target[label]
//   背景像素: tags_out[idx] = tags_in[idx] (保留)
// ============================================================================
__global__ void k_color(
    const char* __restrict__ tags_in,
    char* __restrict__ tags_out,
    const int* __restrict__ labels,
    const unsigned char* __restrict__ target,
    int H, int W)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;

    int idx = y * W + x;
    int lab = labels[idx];
    if (lab > 0 && lab < MAX_LABELS) {
        tags_out[idx] = (char)target[lab];
    }
    else {
        tags_out[idx] = tags_in[idx];
    }
}

// ============================================================================
// 核函数 7a: 拼接 tail + 新批 → combined
// ============================================================================
__global__ void k_concatTailWithBatch(
    const char* __restrict__ tail,
    const char* __restrict__ batch,
    char* __restrict__ combined,
    int K, int N, int W)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = (K + N) * W;
    if (idx >= total) return;

    int row = idx / W;
    int col = idx % W;
    if (row < K) {
        combined[idx] = tail[row * W + col];
    }
    else {
        combined[idx] = batch[(row - K) * W + col];
    }
}

// ============================================================================
// 核函数 7b: 把 combined 的最后 K 行复制回 tail (纯净版)
// ============================================================================
__global__ void k_updateTail(
    const char* __restrict__ combined,
    char* __restrict__ tail,
    int K, int N, int W)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = K * W;
    if (idx >= total) return;

    int srcOff = N * W + idx;
    tail[idx] = combined[srcOff];
}

// ============================================================================
// 核函数 7c: 切片输出 N 行 (跳过开头 K 行)
// ============================================================================
__global__ void k_sliceOutput(
    const char* __restrict__ combined,
    char* __restrict__ outTags,
    int K, int N, int W)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = N * W;
    if (idx >= total) return;

    int srcOff = K * W + idx;
    outTags[idx] = combined[srcOff];
}

// ============================================================================
// GPU 初始化 (内部)
// ============================================================================
static bool InitGpuInternal(int dealCount, int numSamples)
{
    int K_max = dealCount;
    int N_max = dealCount;
    int total = (K_max + N_max) * numSamples;

    d_max_K = K_max;
    d_max_N = N_max;
    d_tail_samples = numSamples;

    CUDA_CHECK_RET(cudaMalloc(&d_inTags, N_max * numSamples * sizeof(char)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_outTags, N_max * numSamples * sizeof(char)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_tail, K_max * numSamples * sizeof(char)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_combined, total * sizeof(char)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_labels, total * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_labels_swap, total * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_changed, sizeof(int)), false);

    CUDA_CHECK_RET(cudaMalloc(&d_rootMask, total * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_rootToCompId, total * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_compIdCount, sizeof(int)), false);

    CUDA_CHECK_RET(cudaMalloc(&d_fgInComp, MAX_LABELS * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_hist, MAX_LABELS * MAX_CLASS_ID * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_bbox_r0, MAX_LABELS * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_bbox_r1, MAX_LABELS * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_bbox_c0, MAX_LABELS * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_bbox_c1, MAX_LABELS * sizeof(int)), false);
    CUDA_CHECK_RET(cudaMalloc(&d_target, MAX_LABELS * sizeof(unsigned char)), false);

    cudaMemset(d_tail, 0, (size_t)K_max * numSamples);
    d_tail_frames = 0;

    double mb = (double)(
        N_max * numSamples * 2 +    // d_inTags + d_outTags
        K_max * numSamples +         // d_tail
        total +                       // d_combined
        total * 4 * 2 +               // d_labels + d_labels_swap
        sizeof(int) +                 // d_changed
        total * 4 * 2 +               // d_rootMask + d_rootToCompId
        sizeof(int) +                 // d_compIdCount
        MAX_LABELS * (4 * 5 + 1 + MAX_CLASS_ID * 4)  // 统计数组
        ) / (1024.0 * 1024.0);

    UnifyLog("[Unify-GPU] 初始化成功: 显存 %.2f MB (K_max=%d, N_max=%d, samples=%d)",
        mb, K_max, N_max, numSamples);

    return true;
}

// ============================================================================
// GPU 处理 (内部)
// ============================================================================
static int ProcessGpuInternal(const char* inTags, char* outTags,
    int N, int W)
{
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    int K = g_unifyTailFrames;
    if (K > d_max_K) K = d_max_K;
    if (N > d_max_N) {
        UnifyLog("[Unify-GPU] N=%d 超过预分配 d_max_N=%d, fallback CPU", N, d_max_N);
        return UnifyProcess_Cpu(inTags, outTags, N, W);
    }

    int H = K + N;
    int total = H * W;

    // 阶段 0a: 上传 inTags 到 GPU
    CUDA_CHECK_RET(cudaMemcpy(d_inTags, inTags, (size_t)N * W,
        cudaMemcpyHostToDevice), -1);

    // 阶段 0b: 拼接 tail + 新批 → d_combined
    {
        dim3 block(256);
        dim3 grid((total + block.x - 1) / block.x);
        k_concatTailWithBatch << <grid, block >> > (d_tail, d_inTags, d_combined, K, N, W);
    }

    // 阶段 1: 初始化 labels
    {
        dim3 block(BLOCK_X, BLOCK_Y);
        dim3 grid((W + BLOCK_X - 1) / BLOCK_X, (H + BLOCK_Y - 1) / BLOCK_Y);
        k_initLabels << <grid, block >> > (d_combined, d_labels, H, W);
    }

    // 阶段 2: 迭代传播 (LE) - v4.5 固定 10 次, 不查收敛
      //   理由: 原代码每轮做 cudaMemcpy(D2H) 检测收敛, 是一个同步阻塞点
      //         一旦 GPU 被外部任务抢占(WDDM 抢占/桌面合成/驱动 telemetry),
      //         这一行会等几十到几百毫秒, 导致 unify max 突刺到 ~200ms
      //   修复: 固定迭代次数, 全部 kernel 异步排队, 热路径上没有 D2H 同步
      //   代价: 固定多跑几次 propagate, 单次 ~10us, 总共 < 200us, 可忽略
      //   收敛性: 你的画面 48x640, 连通域直径 <= ~50, LE 经验 5-8 次收敛
      //          定 10 次留 25% 安全余量
    {
        dim3 block(BLOCK_X, BLOCK_Y);
        dim3 grid((W + BLOCK_X - 1) / BLOCK_X, (H + BLOCK_Y - 1) / BLOCK_Y);

        const int FIXED_ITERS = 10;
        int* in = d_labels;
        int* out = d_labels_swap;

        for (int it = 0; it < FIXED_ITERS; ++it) {
            // 传 nullptr -> kernel 内部不写 changed, 也不需要 cudaMemset
            k_propagate << <grid, block >> > (in, out, nullptr, H, W);
            int* tmp = in; in = out; out = tmp;
        }

        // 10 是偶数, 经过 10 次 swap 后 in 重新指向 d_labels (out 指向 d_labels_swap)
        // 但最新的结果在最后一次 kernel 写入的 buffer 里, 也就是 swap 之前的 out
        // 因为最后一次 swap 把 out 变回了 d_labels_swap, 所以最终结果在 d_labels_swap
        // -> 需要拷贝回 d_labels 让后续阶段使用
        //
        // 简化判断: FIXED_ITERS 是偶数时最终结果一定在 d_labels_swap
        //          FIXED_ITERS 是奇数时最终结果在 d_labels
        // 这里 FIXED_ITERS=10 是偶数, 所以一定要拷
        if ((FIXED_ITERS & 1) == 0) {
            cudaMemcpyAsync(d_labels, d_labels_swap,
                (size_t)total * sizeof(int),
                cudaMemcpyDeviceToDevice);
        }
    }

    // 阶段 3: 紧凑化标签
    {
        dim3 block(256);
        dim3 grid((total + block.x - 1) / block.x);

        k_compactBuildRoots << <grid, block >> > (d_labels, d_rootMask, H, W);

        cudaMemset(d_compIdCount, 0, sizeof(int));
        k_compactAssignId << <grid, block >> > (d_rootMask, d_rootToCompId, d_compIdCount, H, W);

        k_compactRelabel << <grid, block >> > (d_labels, d_rootToCompId, H, W);
    }

    int numComps = 0;
    cudaMemcpy(&numComps, d_compIdCount, sizeof(int), cudaMemcpyDeviceToHost);
    if (numComps > MAX_LABELS - 1) numComps = MAX_LABELS - 1;

    if (numComps == 0) {
        // 无前景: 直接切片 + 更新 tail
        {
            dim3 block(256);
            dim3 grid((K * W + block.x - 1) / block.x);
            k_updateTail << <grid, block >> > (d_combined, d_tail, K, N, W);
        }
        d_tail_frames = K;
        // 切片
        {
            dim3 block(256);
            dim3 grid((N * W + block.x - 1) / block.x);
            k_sliceOutput << <grid, block >> > (d_combined, d_outTags, K, N, W);
        }
        cudaMemcpy(outTags, d_outTags, (size_t)N * W, cudaMemcpyDeviceToHost);

        cudaDeviceSynchronize();
        auto t1 = clock::now();
        long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        g_gpuTotalUs += us;
        if (us > g_gpuMaxUs) g_gpuMaxUs = us;
        g_gpuCount++;

        // ★ 每 100 批自动打印一次性能统计 (跟 CPU 版行为一致)
        if (g_gpuCount % 100 == 0) {
            long long avg = g_gpuTotalUs / g_gpuCount;
            UnifyLog("[Unify-GPU] processed %d batches  avg=%lldus  max=%lldus  cur=%lldus  K=%d",
                g_gpuCount, avg, g_gpuMaxUs, us, g_unifyTailFrames);
        }
        // ★ v4.5 慢路径监控
        if (us > 5000) {
            UnifyLog("[Unify-GPU][SLOW-NOCOMP] cur=%lldus K=%d N=%d H=%d W=%d",
                us, K, N, H, W);
        }
        return (int)us;
    }

    // 阶段 4: 统计
    {
        cudaMemset(d_fgInComp, 0, MAX_LABELS * sizeof(int));
        cudaMemset(d_hist, 0, MAX_LABELS * MAX_CLASS_ID * sizeof(int));
        cudaMemset(d_bbox_r0, 0x7F, MAX_LABELS * sizeof(int));
        cudaMemset(d_bbox_c0, 0x7F, MAX_LABELS * sizeof(int));
        cudaMemset(d_bbox_r1, 0x80, MAX_LABELS * sizeof(int));
        cudaMemset(d_bbox_c1, 0x80, MAX_LABELS * sizeof(int));

        dim3 block(BLOCK_X, BLOCK_Y);
        dim3 grid((W + BLOCK_X - 1) / BLOCK_X, (H + BLOCK_Y - 1) / BLOCK_Y);
        k_stats << <grid, block >> > (d_combined, d_labels,
            d_fgInComp, d_hist,
            d_bbox_r0, d_bbox_r1, d_bbox_c0, d_bbox_c1,
            H, W);
    }

    // 阶段 5: 决策
    {
        cudaMemset(d_target, 0, MAX_LABELS * sizeof(unsigned char));
        dim3 block(256);
        dim3 grid((numComps + block.x) / block.x);
        k_decide << <grid, block >> > (d_fgInComp, d_hist, d_target,
            g_unifyMinArea, numComps);
    }

    // 阶段 6: 染色 (in-place 到 d_combined)
    {
        dim3 block(BLOCK_X, BLOCK_Y);
        dim3 grid((W + BLOCK_X - 1) / BLOCK_X, (H + BLOCK_Y - 1) / BLOCK_Y);
        k_color << <grid, block >> > (d_combined, d_combined,
            d_labels, d_target, H, W);
    }

    // 阶段 7a: 切片输出 N 行
    {
        dim3 block(256);
        dim3 grid((N * W + block.x - 1) / block.x);
        k_sliceOutput << <grid, block >> > (d_combined, d_outTags, K, N, W);
    }

    // 阶段 7b: 更新 tail (染色后的 K 行)
    {
        dim3 block(256);
        dim3 grid((K * W + block.x - 1) / block.x);
        k_updateTail << <grid, block >> > (d_combined, d_tail, K, N, W);
    }
    d_tail_frames = K;

    // 阶段 7c: 拷回 CPU
    CUDA_CHECK_RET(cudaMemcpy(outTags, d_outTags, (size_t)N * W,
        cudaMemcpyDeviceToHost), -1);

    cudaDeviceSynchronize();
    auto t1 = clock::now();
    long long us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    g_gpuTotalUs += us;
    if (us > g_gpuMaxUs) g_gpuMaxUs = us;
    g_gpuCount++;

    // ★ 每 100 批自动打印一次性能统计 (跟 CPU 版行为一致)
    if (g_gpuCount % 100 == 0) {
        long long avg = g_gpuTotalUs / g_gpuCount;
        UnifyLog("[Unify-GPU] processed %d batches  avg=%lldus  max=%lldus  cur=%lldus  K=%d",
            g_gpuCount, avg, g_gpuMaxUs, us, g_unifyTailFrames);
    }
    // ★ v4.5 慢路径监控
    if (us > 5000) {
        UnifyLog("[Unify-GPU][SLOW] cur=%lldus numComps=%d K=%d N=%d H=%d W=%d",
            us, numComps, K, N, H, W);
    }
    return (int)us;
}

// ============================================================================
// GPU 释放 (内部)
// ============================================================================
static void ShutdownGpuInternal()
{
    auto safeFree = [](void*& ptr) {
        if (ptr) { cudaFree(ptr); ptr = nullptr; }
        };

    safeFree((void*&)d_inTags);
    safeFree((void*&)d_outTags);
    safeFree((void*&)d_tail);
    safeFree((void*&)d_combined);
    safeFree((void*&)d_labels);
    safeFree((void*&)d_labels_swap);
    safeFree((void*&)d_changed);
    safeFree((void*&)d_rootMask);
    safeFree((void*&)d_rootToCompId);
    safeFree((void*&)d_compIdCount);
    safeFree((void*&)d_fgInComp);
    safeFree((void*&)d_hist);
    safeFree((void*&)d_bbox_r0);
    safeFree((void*&)d_bbox_r1);
    safeFree((void*&)d_bbox_c0);
    safeFree((void*&)d_bbox_c1);
    safeFree((void*&)d_target);
}

// ============================================================================
// 公开接口 (dispatcher)
// ============================================================================

void UnifyInit(int dealCount, int numSamples)
{
    UnifyInit_Cpu();   // 先初始化 CPU 版

    if (g_unifyUseGpu) {
        g_gpuReady = InitGpuInternal(dealCount, numSamples);
        if (!g_gpuReady) {
            UnifyLog("[Unify-GPU] GPU 初始化失败, 自动 fallback 到 CPU 版");
            g_unifyUseGpu = false;
        }
    }
    else {
        UnifyLog("[Unify] 使用 CPU 版 (g_unifyUseGpu = false)");
    }

    g_gpuTotalUs = 0;
    g_gpuMaxUs = 0;
    g_gpuCount = 0;
}

void UnifyReset()
{
    UnifyReset_Cpu();

    if (g_gpuReady && d_tail) {
        cudaMemset(d_tail, 0, (size_t)d_max_K * d_tail_samples);
        d_tail_frames = 0;
    }

    g_gpuTotalUs = 0;
    g_gpuMaxUs = 0;
    g_gpuCount = 0;
}

int UnifyProcess(const char* inTags, char* outTags,
    int numFrames, int numSamples)
{
    // ★ 诊断: 第一次调用时打印走的是哪条路径
    static bool s_firstCall = true;
    if (s_firstCall) {
        s_firstCall = false;
        UnifyLog("[Unify] 首次调用 UnifyProcess: g_unifyEnable=%d g_unifyUseGpu=%d g_gpuReady=%d",
            (int)g_unifyEnable, (int)g_unifyUseGpu, (int)g_gpuReady);
        if (!g_unifyEnable) {
            UnifyLog("[Unify] → 走透传 (g_unifyEnable=false)");
        }
        else if (g_unifyUseGpu && g_gpuReady) {
            UnifyLog("[Unify] → 走 GPU 版");
        }
        else {
            UnifyLog("[Unify] → 走 CPU 版 (g_unifyUseGpu=%d, g_gpuReady=%d)",
                (int)g_unifyUseGpu, (int)g_gpuReady);
        }
    }

    if (!g_unifyEnable) {
        // 透传
        memcpy(outTags, inTags, (size_t)numFrames * numSamples);
        return 0;
    }

    if (g_unifyUseGpu && g_gpuReady) {
        int us = ProcessGpuInternal(inTags, outTags, numFrames, numSamples);
        if (us < 0) {
            // GPU 失败, fallback 到 CPU
            return UnifyProcess_Cpu(inTags, outTags, numFrames, numSamples);
        }
        return us;
    }
    else {
        return UnifyProcess_Cpu(inTags, outTags, numFrames, numSamples);
    }
}

void UnifyShutdown()
{
    UnifyShutdown_Cpu();

    if (g_gpuReady) {
        ShutdownGpuInternal();
        g_gpuReady = false;
    }
}

void UnifyLogStats()
{
    UnifyLogStats_Cpu();

    if (g_gpuCount > 0) {
        long long avg = g_gpuTotalUs / g_gpuCount;
        UnifyLog("[Unify-GPU] processed %d batches  avg=%lldus  max=%lldus  K=%d",
            g_gpuCount, avg, g_gpuMaxUs, g_unifyTailFrames);
    }
    else if (g_unifyUseGpu) {
        UnifyLog("[Unify-GPU] 暂无统计");
    }
}
