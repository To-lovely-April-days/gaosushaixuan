#pragma once
#include "cuda_runtime.h"
#include "device_launch_parameters.h"

//gpu CUDA 参数
struct OtherParams
{
	//设备参数
	int waveLength;//光谱数据波长个数
	//建模参数
	int startBandIndex;//起始波段
	int endBandIndex;//结束波段
	int BandsCount;//模型波段个数
	//模型参数与实际参数差异
	int RoiBandStartIndex;//偏移量
	//扣底 1 扣底
	int LimitScopeFlag;
	float LowestValue;
	float HighestValue;
	//预处理
	int PreprocessCount;
	int FilterStrength;
	//模型中目标个数
	int modeNum;
	int modeIndex[255];//目标的ID
	float Threshold[255];//目标的阈值
};

extern char* cuda_frameData;
extern float* cuda_tempData;
extern float* cuda_outputData;
extern char* cuda_tags;
extern float* cuda_Intercept;
extern float* cuda_Coef;//二维数组
extern float* cuda_StdX;
extern float* cuda_MeanX;
extern float* cuda_k_White;
extern float* cuda_White_ReadBytes;
extern float* cuda_Black_ReadBytes;
//模型使用的波段对应的序号列表
extern int* cuda_modelWaveList;
extern int* cuda_preprocessList;
extern OtherParams* cuda_otherParams;


bool initGpu();

bool calc_GPU(char* frameData, float* tempData, float* outputData, float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
	OtherParams* otherParams, int* preprocessList, int* modelWaveList, float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags);
void calc_CPU(char* frameData, float* tempData, float* outputData, float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
	OtherParams* otherParams, int* preprocessList, int* modelWaveList, float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags);

bool clearGpu();