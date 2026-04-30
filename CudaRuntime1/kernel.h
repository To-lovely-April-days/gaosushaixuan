// ============================================================================
// 文件名：kernel.h
// 作用  ：声明 GPU 计算所需的：
//         (1) 核函数参数结构体 OtherParams
//         (2) 显存指针（在 kernel.cu 中分配）
//         (3) 初始化/计算/清理三个函数
// ============================================================================
#pragma once
#include "cuda_runtime.h"
#include "device_launch_parameters.h"

// ============================================================================
// OtherParams: 传给 GPU 核函数的所有标量参数（一个结构体一次性传到显存）
// 这样核函数签名简洁，且参数变更时只改这一处
// ============================================================================
struct OtherParams
{
    // ===== 设备相关 =====
    int waveLength;       // 光谱数据波段总数（ROI 后实际采集的波段数）

    // ===== 建模参数 =====
    int startBandIndex;   // 模型起始波段（绝对索引）
    int endBandIndex;     // 模型结束波段（绝对索引）
    int BandsCount;       // 模型选用的波段数

    // ===== ROI 偏移 =====
    int RoiBandStartIndex;// 训练时设置的起始位置与相机能设置的起始位置之差

    // ===== 扣底（限制总反射率范围）=====
    int   LimitScopeFlag; // 1=启用扣底  0=不扣底
    float LowestValue;    // 总反射率下限（低于此值视为背景）
    float HighestValue;   // 总反射率上限（高于此值视为强反光等）

    // ===== 预处理参数 =====
    int PreprocessCount;  // 预处理步骤个数
    int FilterStrength;   // 滑动平滑窗口大小

    // ===== 模型参数 =====
    int   modeNum;         // 模型中的目标种类数
    int   modeIndex[255];  // 各种类的 ID（用于结果标记）
    float Threshold[255];  // 各种类的判定阈值
};

// ============================================================================
// GPU 显存指针（在 kernel.cu 中分配，主程序通过 extern 引用）
// ============================================================================
extern char*  cuda_frameData;        // 原始帧数据（uint16，按字节存储）
extern float* cuda_tempData;         // 临时缓冲（预处理用）
extern float* cuda_outputData;       // 校准后的反射率数据
extern char*  cuda_tags;             // 每像素分选结果

// PLS-DA 模型参数
extern float* cuda_Intercept;        // 截距（每个分类一个）
extern float* cuda_Coef;             // 回归系数（按分类×波段二维数组）
extern float* cuda_StdX;             // 各波段标准差
extern float* cuda_MeanX;            // 各波段均值

// 校准数据
extern float* cuda_k_White;          // 白板修正系数
extern float* cuda_White_ReadBytes;  // 白板原始数据
extern float* cuda_Black_ReadBytes;  // 黑板原始数据

// 索引数组
extern int* cuda_modelWaveList;      // 模型选用的波段索引
extern int* cuda_preprocessList;     // 预处理步骤列表
extern OtherParams* cuda_otherParams;// 标量参数

// ============================================================================
// 函数声明
// ============================================================================
// 初始化 GPU：检查设备 → 分配显存 → 拷贝模型/校准/参数到显存
bool initGpu();

// GPU 计算（核函数 calcKernel_GPU 的封装入口）
bool calc_GPU(char* frameData, float* tempData, float* outputData,
              float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
              OtherParams* otherParams, int* preprocessList, int* modelWaveList,
              float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags);

// CPU 版本（同样的算法，仅用于无 GPU 时验证或对比测试）
void calc_CPU(char* frameData, float* tempData, float* outputData,
              float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
              OtherParams* otherParams, int* preprocessList, int* modelWaveList,
              float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags);

// 释放 GPU 资源（程序退出前调用）
bool clearGpu();
