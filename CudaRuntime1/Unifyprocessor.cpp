// ============================================================================
// File: UnifyProcessor.cpp
// Purpose: Cross-batch connected-component unify processor (v3.6)
//
// Algorithm: ported from C# RealtimeUnifyProcessor
//   - Two-Pass 8-connectivity labeling + Union-Find with path compression
//   - bbox accumulation + histogram statistics
//   - confidence/majority-vote decision + coloring
//
// Performance optimization:
//   - all buffers pre-allocated and reused; no new() in hot path
//   - histogram only counts cls 1..12 (sel_type max is 12)
// ============================================================================

#include "UnifyProcessor.h"

#include <cstring>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <climits>      // INT_MAX
#include <new>           // std::nothrow

// ============================================================================
// Constants
// ============================================================================
#define MAX_CLASS_ID    13     // 0..12 total 13 classes (cls=0 is background)
#define RESERVED_MIN    254    // 254/255 are reserved values (treated as background)
#define MAX_LABELS      4096   // max 4096 connected components per batch

// ============================================================================
// Global parameter definitions (initialized in main.cpp)
// ============================================================================
bool  g_unifyEnable = true;
int   g_unifyTailFrames = 24;
int   g_unifyForceClassId = 1;
float g_unifyThreshold = 0.30f;
bool  g_unifyFillBackground = false;
int   g_unifyMinArea = 0;             // majority-vote noise filter: min component px (0=off)

// ============================================================================
// Module-level state
// ============================================================================
static char* g_tail = nullptr;          // tail cache (K x samples)
static int   g_tailFrames = 0;          // actual cached frames (0 for first batch)
static int   g_tailSamples = 0;         // cached samples count

// combined buffer (K + N) x samples
static char* g_combined = nullptr;
static int   g_combinedCapacity = 0;    // allocated size

// processed buffer
static char* g_processed = nullptr;
static int   g_processedCapacity = 0;

// label buffer
static int* g_labels = nullptr;
static int   g_labelsCapacity = 0;

// Union-Find parent array
static int   g_parent[MAX_LABELS];

// bbox + histogram buffers
static int   g_bboxR0[MAX_LABELS];
static int   g_bboxR1[MAX_LABELS];
static int   g_bboxC0[MAX_LABELS];
static int   g_bboxC1[MAX_LABELS];
static int   g_fgPixels[MAX_LABELS];
static int   g_hist[MAX_LABELS][MAX_CLASS_ID];

// label-to-compact mapping
static int   g_rootToCompact[MAX_LABELS];

// Performance monitoring
static int   g_unifyCallCount = 0;
static long long g_unifyTotalUs = 0;
static int   g_unifyMaxUs = 0;

// ============================================================================
// Utility functions
// ============================================================================
static inline bool IsForeground(unsigned char cls)
{
    // non-zero and non-reserved (254/255) = foreground
    return cls != 0 && cls < RESERVED_MIN;
}

static int Find(int* parent, int x)
{
    int root = x;
    while (parent[root] != root) root = parent[root];
    // path compression
    while (parent[x] != root) {
        int next = parent[x];
        parent[x] = root;
        x = next;
    }
    return root;
}

static void Union(int* parent, int a, int b)
{
    int ra = Find(parent, a);
    int rb = Find(parent, b);
    if (ra != rb) {
        if (ra < rb) parent[rb] = ra;
        else         parent[ra] = rb;
    }
}

// ============================================================================
// Buffer allocation (grow on demand)
// ============================================================================
static bool EnsureBuffers(int totalLines, int samples)
{
    int totalPixels = totalLines * samples;

    if (g_combinedCapacity < totalPixels) {
        delete[] g_combined;
        g_combined = new (std::nothrow) char[totalPixels];
        if (!g_combined) {
            std::cerr << "[Unify] g_combined alloc failed size=" << totalPixels << std::endl;
            return false;
        }
        g_combinedCapacity = totalPixels;
    }

    if (g_processedCapacity < totalPixels) {
        delete[] g_processed;
        g_processed = new (std::nothrow) char[totalPixels];
        if (!g_processed) {
            std::cerr << "[Unify] g_processed alloc failed" << std::endl;
            return false;
        }
        g_processedCapacity = totalPixels;
    }

    if (g_labelsCapacity < totalPixels) {
        delete[] g_labels;
        g_labels = new (std::nothrow) int[totalPixels];
        if (!g_labels) {
            std::cerr << "[Unify] g_labels alloc failed" << std::endl;
            return false;
        }
        g_labelsCapacity = totalPixels;
    }

    return true;
}

// ============================================================================
// Two-Pass 8-connectivity labeling
//   returns number of components (1..numComponents)
// ============================================================================
static int ConnectedComponents8(const char* tags, int* labels, int lines, int samples)
{
    int totalPixels = lines * samples;
    memset(labels, 0, totalPixels * sizeof(int));

    g_parent[0] = 0;
    int nextLabel = 1;

    // ========== Pass 1: scan + provisional labels ==========
    for (int i = 0; i < lines; i++) {
        for (int j = 0; j < samples; j++) {
            unsigned char cls = (unsigned char)tags[i * samples + j];
            if (!IsForeground(cls)) continue;

            // scanned neighbors in 8-conn: 3 from prev row + 1 left
            int n1 = (i > 0 && j > 0) ? labels[(i - 1) * samples + (j - 1)] : 0;
            int n2 = (i > 0) ? labels[(i - 1) * samples + j] : 0;
            int n3 = (i > 0 && j < samples - 1) ? labels[(i - 1) * samples + (j + 1)] : 0;
            int n4 = (j > 0) ? labels[i * samples + (j - 1)] : 0;

            int minLabel = INT_MAX;
            if (n1 > 0 && n1 < minLabel) minLabel = n1;
            if (n2 > 0 && n2 < minLabel) minLabel = n2;
            if (n3 > 0 && n3 < minLabel) minLabel = n3;
            if (n4 > 0 && n4 < minLabel) minLabel = n4;

            if (minLabel == INT_MAX) {
                // new label
                if (nextLabel >= MAX_LABELS) {
                    // label count exceeds limit -> treat as background
                    continue;
                }
                g_parent[nextLabel] = nextLabel;
                labels[i * samples + j] = nextLabel;
                nextLabel++;
            }
            else {
                labels[i * samples + j] = minLabel;
                if (n1 > 0 && n1 != minLabel) Union(g_parent, minLabel, n1);
                if (n2 > 0 && n2 != minLabel) Union(g_parent, minLabel, n2);
                if (n3 > 0 && n3 != minLabel) Union(g_parent, minLabel, n3);
                if (n4 > 0 && n4 != minLabel) Union(g_parent, minLabel, n4);
            }
        }
    }

    // ========== Pass 2: compact labels to contiguous IDs ==========
    memset(g_rootToCompact, 0, nextLabel * sizeof(int));
    int compactCount = 0;
    for (int old = 1; old < nextLabel; old++) {
        int r = Find(g_parent, old);
        if (g_rootToCompact[r] == 0) {
            compactCount++;
            g_rootToCompact[r] = compactCount;
        }
    }
    for (int i = 0; i < totalPixels; i++) {
        int lab = labels[i];
        if (lab != 0) {
            labels[i] = g_rootToCompact[Find(g_parent, lab)];
        }
    }

    return compactCount;
}

// ============================================================================
// ApplyUnify: core algorithm
// ============================================================================
static void ApplyUnify(const char* inTags, char* outTags,
    int lines, int samples,
    unsigned char forceClassId,
    float threshold,
    bool fillBackground)
{
    int totalPixels = lines * samples;

    // ===== Phase 1: connected-component labeling =====
    int numComponents = ConnectedComponents8(inTags, g_labels, lines, samples);

    if (numComponents == 0) {
        // no components, pass-through
        memcpy(outTags, inTags, totalPixels);
        return;
    }
    if (numComponents >= MAX_LABELS) {
        // abnormal case, pass-through
        memcpy(outTags, inTags, totalPixels);
        return;
    }

    // ===== Phase 2: bbox + histogram per component =====
    // init bbox to reversed extremes
    for (int k = 1; k <= numComponents; k++) {
        g_bboxR0[k] = lines;
        g_bboxR1[k] = -1;
        g_bboxC0[k] = samples;
        g_bboxC1[k] = -1;
        g_fgPixels[k] = 0;
        memset(g_hist[k], 0, sizeof(g_hist[k]));
    }

    for (int i = 0; i < lines; i++) {
        for (int j = 0; j < samples; j++) {
            int idx = i * samples + j;
            int k = g_labels[idx];
            if (k == 0) continue;
            unsigned char cls = (unsigned char)inTags[idx];
            // only count valid classes
            if (cls < MAX_CLASS_ID) {
                g_hist[k][cls]++;
            }
            g_fgPixels[k]++;
            if (i < g_bboxR0[k]) g_bboxR0[k] = i;
            if (i > g_bboxR1[k]) g_bboxR1[k] = i;
            if (j < g_bboxC0[k]) g_bboxC0[k] = j;
            if (j > g_bboxC1[k]) g_bboxC1[k] = j;
        }
    }

    // ===== Phase 3: decision + coloring =====
    // init output as input
    memcpy(outTags, inTags, totalPixels);

    for (int k = 1; k <= numComponents; k++) {
        int fgInComp = g_fgPixels[k];
        if (fgInComp <= 0) continue;

        // ----- decision: pure majority vote (+ minArea noise filter) -----
        unsigned char target;
        if (g_unifyMinArea > 0 && fgInComp < g_unifyMinArea) {
            // component too small (noise) -> wipe to background, never ejected
            target = 0;
        }
        else {
            int bestCls = 0;
            int bestCnt = 0;
            for (int c = 1; c < MAX_CLASS_ID; c++) {
                if (g_hist[k][c] > bestCnt) {
                    bestCnt = g_hist[k][c];
                    bestCls = c;   // tie keeps the smaller cls (deterministic)
                }
            }
            // bestCls == 0 only when no votable class -> also wipe to background
            target = (unsigned char)bestCls;
        }

        // ----- coloring: only the real foreground pixels of this component -----
        int r0 = g_bboxR0[k], r1 = g_bboxR1[k];
        int c0 = g_bboxC0[k], c1 = g_bboxC1[k];

        for (int i = r0; i <= r1; i++) {
            for (int j = c0; j <= c1; j++) {
                int idx = i * samples + j;
                if (g_labels[idx] == k) {
                    outTags[idx] = (char)target;
                }
            }
        }
    }
}

// ============================================================================
// UnifyInit / UnifyReset / UnifyShutdown
// ============================================================================
void UnifyInit_Cpu()
{
    g_tail = nullptr;
    g_tailFrames = 0;
    g_tailSamples = 0;
    g_combined = nullptr;
    g_combinedCapacity = 0;
    g_processed = nullptr;
    g_processedCapacity = 0;
    g_labels = nullptr;
    g_labelsCapacity = 0;

    g_unifyCallCount = 0;
    g_unifyTotalUs = 0;
    g_unifyMaxUs = 0;

    std::cout << "[Unify] initialized" << std::endl;
}

void UnifyReset_Cpu()
{
    delete[] g_tail;
    g_tail = nullptr;
    g_tailFrames = 0;
    g_tailSamples = 0;

    g_unifyCallCount = 0;
    g_unifyTotalUs = 0;
    g_unifyMaxUs = 0;
}

void UnifyShutdown_Cpu()
{
    delete[] g_tail;          g_tail = nullptr;
    delete[] g_combined;      g_combined = nullptr;
    delete[] g_processed;     g_processed = nullptr;
    delete[] g_labels;        g_labels = nullptr;
    g_tailFrames = 0;
    g_tailSamples = 0;
    g_combinedCapacity = 0;
    g_processedCapacity = 0;
    g_labelsCapacity = 0;
}

// ============================================================================
// UnifyProcess: main entry
// ============================================================================
int UnifyProcess_Cpu(const char* inTags, char* outTags, int numFrames, int numSamples)
{
    auto t0 = std::chrono::high_resolution_clock::now();

    // ===== Disabled -> pass-through =====
    if (!g_unifyEnable) {
        memcpy(outTags, inTags, numFrames * numSamples);
        UnifyReset();   // clear cache to avoid stale data
        return 0;
    }

    unsigned char force = (unsigned char)g_unifyForceClassId;

    // ===== Allocate buffers on demand =====
    int K = g_unifyTailFrames;
    if (K < 0) K = 0;
    int totalLines = K + numFrames;
    if (!EnsureBuffers(totalLines, numSamples)) {
        // allocation failed -> pass-through
        memcpy(outTags, inTags, numFrames * numSamples);
        return 0;
    }

    // ===== Phase A: concat [tail + new] =====
    int actualK = 0;
    int sendOffsetRows = 0;

    if (g_tail == nullptr || g_tailFrames == 0) {
        // first batch, process directly
        actualK = 0;
        sendOffsetRows = 0;
        memcpy(g_combined, inTags, numFrames * numSamples);
    }
    else if (g_tailSamples != numSamples) {
        // samples changed -> reset cache
        UnifyReset();
        actualK = 0;
        sendOffsetRows = 0;
        memcpy(g_combined, inTags, numFrames * numSamples);
    }
    else {
        // normal concat
        actualK = g_tailFrames;
        sendOffsetRows = actualK;
        memcpy(g_combined, g_tail, actualK * numSamples);
        memcpy(g_combined + actualK * numSamples, inTags, numFrames * numSamples);
    }

    int processLines = actualK + numFrames;

    // ===== Phase B: apply unify (clean version) =====
    ApplyUnify(g_combined, g_processed,
        processLines, numSamples,
        force, g_unifyThreshold,
        false);   // clean version must have fillBackground=false

    // ===== Phase C: update tail cache =====
    if (K > 0) {
        int keepLines = std::min(K, processLines);

        if (g_tail == nullptr || g_tailSamples != numSamples) {
            delete[] g_tail;
            g_tail = new (std::nothrow) char[K * numSamples];
            if (!g_tail) {
                std::cerr << "[Unify] g_tail alloc failed" << std::endl;
                g_tailFrames = 0;
                g_tailSamples = 0;
            }
        }

        if (g_tail) {
            int srcOffset = (processLines - keepLines) * numSamples;
            memcpy(g_tail, g_processed + srcOffset, keepLines * numSamples);
            g_tailFrames = keepLines;
            g_tailSamples = numSamples;
        }
    }

    // ===== Phase D: slice and return new batch result =====
    memcpy(outTags, g_processed + sendOffsetRows * numSamples,
        numFrames * numSamples);

    // ===== Phase E removed: majority-vote version never fills background =====

    // ===== Performance monitoring =====
    auto t1 = std::chrono::high_resolution_clock::now();
    int us = (int)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    g_unifyCallCount++;
    g_unifyTotalUs += us;
    if (us > g_unifyMaxUs) g_unifyMaxUs = us;

    // print perf stats every 100 batches
    if (g_unifyCallCount % 100 == 0) {
        long long avg = g_unifyTotalUs / g_unifyCallCount;
        std::cout << "[Unify] processed " << g_unifyCallCount << " batches"
            << "  avg=" << avg << "us"
            << "  max=" << g_unifyMaxUs << "us"
            << "  cur=" << us << "us"
            << "  K=" << actualK << std::endl;
    }

    return us;
}

// ============================================================================
// ★ v4.0 新增: 外部调用的 CPU 版统计打印 (供 GPU dispatcher 调用)
// ============================================================================
void UnifyLogStats_Cpu()
{
    if (g_unifyCallCount > 0) {
        long long avg = g_unifyTotalUs / g_unifyCallCount;
        std::cout << "[Unify-CPU] processed " << g_unifyCallCount << " batches"
            << "  avg=" << avg << "us  max=" << g_unifyMaxUs << "us" << std::endl;
    }
}
