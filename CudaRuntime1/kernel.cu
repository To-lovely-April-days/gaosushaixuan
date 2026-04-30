// ============================================================================
// 文件名：kernel.cu
// 作用  ：CUDA 核函数实现 + 显存管理
//         GPU 端逐像素并行执行：校准 → 扣底 → 预处理 → PLS-DA 分类
// ============================================================================
#include "kernel.h"
#include "AIModel.h"
#include "CalibrationInfo.h"

// ===== 主程序中的全局变量（外部引用）=====
extern int dealCount;     // 一次处理的帧数
extern int g_Samples;     // 每帧像素数
extern int g_bands;       // 每帧波段数

extern AIModel aiModel;
extern CalibrationInfo calibrationInfo;

// ============================================================================
// 显存指针定义（声明在 kernel.h）
// ============================================================================
char* cuda_frameData;
float* cuda_tempData;
float* cuda_outputData;
char* cuda_tags;
float* cuda_Intercept;
float* cuda_Coef;
float* cuda_StdX;
float* cuda_MeanX;
float* cuda_k_White;
float* cuda_White_ReadBytes;
float* cuda_Black_ReadBytes;
int* cuda_modelWaveList;
int* cuda_preprocessList;
OtherParams* cuda_otherParams;

// ============================================================================
// initGpu: 初始化 GPU
// ============================================================================
bool initGpu()
{
    cudaError_t cudaStatus;
    int deviceCount;
    cudaGetDeviceCount(&deviceCount);

    if (deviceCount == 0) {
        fprintf(stderr, "[GPU] 未检测到 CUDA 设备\n");
        return false;
    }

    // 选择第一个 GPU
    cudaStatus = cudaSetDevice(0);
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "[GPU] cudaSetDevice 失败: %s\n", cudaGetErrorString(cudaStatus));
        return false;
    }

    // 打印关键参数（确认尺寸正确）
    fprintf(stderr, "[GPU] 初始化参数: dealCount=%d, g_Samples=%d, g_bands=%d\n",
        dealCount, g_Samples, g_bands);
    fprintf(stderr, "[GPU] 单帧大小: %d bytes (%.2f KB)\n",
        g_Samples * g_bands * 2, g_Samples * g_bands * 2 / 1024.0);
    fprintf(stderr, "[GPU] 一批数据: %d bytes (%.2f MB)\n",
        dealCount * g_Samples * g_bands * 2,
        dealCount * g_Samples * g_bands * 2 / 1024.0 / 1024.0);

    if (g_Samples <= 0 || g_bands <= 0 || dealCount <= 0) {
        fprintf(stderr, "[GPU] 错误: 尺寸参数无效\n");
        return false;
    }

    if (aiModel.CoreList.size() == 0) {
        fprintf(stderr, "[GPU] 错误: 模型 CoreList 为空\n");
        return false;
    }

    // ===== 分配帧数据缓冲（dealCount 帧的原始数据）=====
    cudaStatus = cudaMalloc((void**)&cuda_frameData,
        dealCount * g_Samples * g_bands * 2 * sizeof(char));
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "[GPU] cudaMalloc cuda_frameData 失败: %s\n", cudaGetErrorString(cudaStatus));
        return false;
    }

    cudaStatus = cudaMalloc((void**)&cuda_outputData,
        dealCount * g_Samples * g_bands * sizeof(float));
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "[GPU] cudaMalloc cuda_outputData 失败: %s\n", cudaGetErrorString(cudaStatus));
        return false;
    }

    cudaStatus = cudaMalloc((void**)&cuda_tempData,
        dealCount * g_Samples * g_bands * sizeof(float));
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "[GPU] cudaMalloc cuda_tempData 失败: %s\n", cudaGetErrorString(cudaStatus));
        return false;
    }

    cudaStatus = cudaMalloc((void**)&cuda_tags,
        dealCount * g_Samples * sizeof(char));
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "[GPU] cudaMalloc cuda_tags 失败: %s\n", cudaGetErrorString(cudaStatus));
        return false;
    }

    // ===== 模型参数显存分配 =====
    cudaStatus = cudaMalloc((void**)&cuda_Intercept,
        aiModel.CoreList.size() * aiModel.CoreList[0].Intercept.size() * sizeof(float));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc cuda_Intercept 失败\n"); return false; }

    cudaStatus = cudaMalloc((void**)&cuda_Coef,
        aiModel.CoreList.size() * aiModel.CoreList[0].Coef.size() * sizeof(float));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc cuda_Coef 失败\n"); return false; }

    cudaStatus = cudaMalloc((void**)&cuda_StdX,
        aiModel.CoreList.size() * aiModel.CoreList[0].StdX.size() * sizeof(float));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc cuda_StdX 失败\n"); return false; }

    cudaStatus = cudaMalloc((void**)&cuda_MeanX,
        aiModel.CoreList.size() * aiModel.CoreList[0].MeanX.size() * sizeof(float));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc cuda_MeanX 失败\n"); return false; }

    // 把每个分类的参数拷贝到 GPU
    for (size_t i = 0; i < aiModel.CoreList.size(); i++)
    {
        cudaStatus = cudaMemcpy(cuda_Intercept + aiModel.CoreList[0].Intercept.size() * i,
            aiModel.CoreList[i].Intercept.data(),
            aiModel.CoreList[0].Intercept.size() * sizeof(float),
            cudaMemcpyHostToDevice);
        if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 Intercept 失败\n"); return false; }

        cudaStatus = cudaMemcpy(cuda_Coef + aiModel.CoreList[0].Coef.size() * i,
            aiModel.CoreList[i].Coef.data(),
            aiModel.CoreList[0].Coef.size() * sizeof(float),
            cudaMemcpyHostToDevice);
        if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 Coef 失败\n"); return false; }

        cudaStatus = cudaMemcpy(cuda_StdX + aiModel.CoreList[0].StdX.size() * i,
            aiModel.CoreList[i].StdX.data(),
            aiModel.CoreList[0].StdX.size() * sizeof(float),
            cudaMemcpyHostToDevice);
        if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 StdX 失败\n"); return false; }

        cudaStatus = cudaMemcpy(cuda_MeanX + aiModel.CoreList[0].MeanX.size() * i,
            aiModel.CoreList[i].MeanX.data(),
            aiModel.CoreList[0].MeanX.size() * sizeof(float),
            cudaMemcpyHostToDevice);
        if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 MeanX 失败\n"); return false; }
    }

    // ===== 校准数据 =====
    cudaStatus = cudaMalloc((void**)&cuda_k_White, calibrationInfo.k_White.size() * sizeof(float));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc k_White 失败\n"); return false; }
    cudaStatus = cudaMalloc((void**)&cuda_White_ReadBytes, calibrationInfo.White_ReadBytes.size() * sizeof(float));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc White_ReadBytes 失败\n"); return false; }
    cudaStatus = cudaMalloc((void**)&cuda_Black_ReadBytes, calibrationInfo.Black_ReadBytes.size() * sizeof(float));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc Black_ReadBytes 失败\n"); return false; }

    cudaStatus = cudaMemcpy(cuda_k_White, calibrationInfo.k_White.data(),
        calibrationInfo.k_White.size() * sizeof(float), cudaMemcpyHostToDevice);
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 k_White 失败\n"); return false; }
    cudaStatus = cudaMemcpy(cuda_White_ReadBytes, calibrationInfo.White_ReadBytes.data(),
        calibrationInfo.White_ReadBytes.size() * sizeof(float), cudaMemcpyHostToDevice);
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 White_ReadBytes 失败\n"); return false; }
    cudaStatus = cudaMemcpy(cuda_Black_ReadBytes, calibrationInfo.Black_ReadBytes.data(),
        calibrationInfo.Black_ReadBytes.size() * sizeof(float), cudaMemcpyHostToDevice);
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 Black_ReadBytes 失败\n"); return false; }

    // ===== 模型选用的波段索引 =====
    cudaStatus = cudaMalloc((void**)&cuda_modelWaveList,
        aiModel.CoreList[0].selectIndex.size() * sizeof(int));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc modelWaveList 失败\n"); return false; }
    cudaStatus = cudaMemcpy(cuda_modelWaveList, aiModel.CoreList[0].selectIndex.data(),
        aiModel.CoreList[0].selectIndex.size() * sizeof(int),
        cudaMemcpyHostToDevice);
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 modelWaveList 失败\n"); return false; }

    // ===== 预处理列表 =====
    cudaStatus = cudaMalloc((void**)&cuda_preprocessList,
        aiModel.Preprocessings.size() * sizeof(int));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc preprocessList 失败\n"); return false; }
    cudaStatus = cudaMemcpy(cuda_preprocessList, aiModel.Preprocessings.data(),
        aiModel.Preprocessings.size() * sizeof(int),
        cudaMemcpyHostToDevice);
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 preprocessList 失败\n"); return false; }

    // ===== OtherParams =====
    cudaStatus = cudaMalloc((void**)&cuda_otherParams, sizeof(OtherParams));
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] cudaMalloc otherParams 失败\n"); return false; }

    OtherParams* lOtherParams = new OtherParams;
    lOtherParams->waveLength = g_bands;
    lOtherParams->startBandIndex = aiModel.StartBandIndex;
    lOtherParams->endBandIndex = aiModel.EndBandIndex;
    lOtherParams->BandsCount = aiModel.CoreList[0].selectIndex.size();
    lOtherParams->RoiBandStartIndex = aiModel.RoiBandStartIndex;
    lOtherParams->LimitScopeFlag = aiModel.LimitScopeFlag;
    lOtherParams->LowestValue = aiModel.LowestValue;
    lOtherParams->HighestValue = aiModel.HighestValue;
    lOtherParams->PreprocessCount = aiModel.Preprocessings.size();
    lOtherParams->FilterStrength = aiModel.FilterStrength;
    lOtherParams->modeNum = aiModel.CoreList.size();
    for (size_t i = 0; i < lOtherParams->modeNum; i++)
    {
        lOtherParams->modeIndex[i] = aiModel.CoreList[i].classid;
        lOtherParams->Threshold[i] = aiModel.CoreList[i].threshold;
    }

    cudaStatus = cudaMemcpy(cuda_otherParams, lOtherParams, sizeof(OtherParams),
        cudaMemcpyHostToDevice);
    if (cudaStatus != cudaSuccess) { fprintf(stderr, "[GPU] 拷贝 otherParams 失败\n"); delete lOtherParams; return false; }
    delete lOtherParams;

    fprintf(stderr, "[GPU] 初始化成功\n");
    return true;
}


// ============================================================================
// clearGpu: 释放显存 + 重置设备
// ============================================================================
bool clearGpu()
{
    cudaError_t cudaStatus;
    cudaStatus = cudaFree(cuda_frameData);
    cudaStatus = cudaFree(cuda_tempData);
    cudaStatus = cudaFree(cuda_outputData);
    cudaStatus = cudaFree(cuda_tags);
    cudaStatus = cudaFree(cuda_Intercept);
    cudaStatus = cudaFree(cuda_Coef);
    cudaStatus = cudaFree(cuda_StdX);
    cudaStatus = cudaFree(cuda_MeanX);
    cudaStatus = cudaFree(cuda_k_White);
    cudaStatus = cudaFree(cuda_White_ReadBytes);
    cudaStatus = cudaFree(cuda_Black_ReadBytes);
    cudaStatus = cudaFree(cuda_modelWaveList);
    cudaStatus = cudaFree(cuda_preprocessList);
    cudaStatus = cudaFree(cuda_otherParams);

    // 重置 CUDA 设备（清理所有上下文）
    cudaStatus = cudaDeviceReset();
    if (cudaStatus != cudaSuccess) {
        fprintf(stderr, "cudaDeviceReset failed!");
    }
    return true;
}

// ============================================================================
// calcKernel_GPU: GPU 核函数（每个像素一个线程并行执行）
//
// 线程组织：
//   gridDim  = (dealCount, 2)            网格 = 帧数 × 2
//   blockDim = (g_Samples / 2)           每块 = 像素数 / 2
//   总线程数 = dealCount × g_Samples
//   每个线程负责 1 个像素的全部计算
//
// 计算流程（针对单个像素）：
//   1) 校准  ：原始值 → 反射率   (value - black) / (white - black) * k_white
//   2) 扣底  ：总反射率超出范围 → 标记为 255（背景）
//   3) 预处理：滑动平滑 / 一阶导 / 二阶导 / 最大最小归一化
//   4) 分选  ：对每个分类做 PLS-DA 预测，超阈值则标记为该分类 ID
// ============================================================================
__global__ void calcKernel_GPU(char* frameData, float* tempData, float* outputData,
    float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
    OtherParams* otherParams, int* preprocessList, int* modelWaveList,
    float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags)
{
    // ===== 计算当前线程负责的像素位置 =====
    int idx = blockIdx.x * gridDim.y * blockDim.x
        + blockIdx.y * blockDim.x + threadIdx.x;   // 全局像素索引

    // 镜像索引（保留：原代码用于翻转输出，当前未使用）
    int jx_idx = blockIdx.x * gridDim.y * blockDim.x
        + (gridDim.y * blockDim.x - (blockIdx.y * blockDim.x + threadIdx.x) - 1);

    int line = blockIdx.x;                                    // 当前在第几帧
    int sample = blockIdx.y * blockDim.x + threadIdx.x;         // 当前是第几个像素（行内）
    int AllSamples = gridDim.y * blockDim.x;                        // 每帧像素总数

    int waveLength = otherParams->waveLength;                              // 波段总数
    int startBandIndex = otherParams->startBandIndex;                          // 模型起始波段
    int lengthModel = otherParams->endBandIndex - otherParams->startBandIndex + 1; // 模型波段长度

    // ===== Step 1: 黑白校准 =====
    float KDsum = 0;   // 用于扣底判断的累积和
    {
        for (int i = 0; i < lengthModel; i++)
        {
            int trueBand = i + otherParams->RoiBandStartIndex;       // 在采集数据中的真实波段索引
            int refIndex = sample + trueBand * AllSamples;           // 在白板/黑板中的索引
            int lineIndex = line * AllSamples * waveLength;           // 当前帧偏移
            // 原始值（uint16）
            float needOri = ((unsigned short*)frameData)[lineIndex + refIndex];

            float needRefK = k_White[trueBand];          // 白板修正系数
            float needWhite = White_ReadBytes[refIndex];  // 白板像素值
            float needBlack = Black_ReadBytes[refIndex];  // 黑板像素值

            // 异常情况（白板≤黑板 或 原始值≤黑板）→ 反射率置 0
            if (needWhite <= needBlack || needOri <= needBlack)
            {
                needOri = 0;
            }
            else
            {
                // 标准反射率公式
                needOri = (needOri - needBlack) / (needWhite - needBlack) * needRefK;
            }
            outputData[idx * waveLength + i] = needOri;
            KDsum += needOri;
        }
    }

    // ===== Step 2: 扣底（限制反射率范围）=====
    if (otherParams->LimitScopeFlag == 1)
    {
        // 注意：这里 LowestValue/HighestValue 被乘以 lengthModel
        // 即把"平均反射率阈值"转换为"累积反射率阈值"
        int lower = otherParams->LowestValue * lengthModel;
        int higher = otherParams->HighestValue * lengthModel;

        if (KDsum >= lower && KDsum <= higher)
        {
            tags[idx] = 255;   // 标记为背景，跳过后续计算
            return;
        }
    }

    // ===== Step 3: 预处理（按 PreprocessList 顺序执行）=====
    for (int i = 0; i < otherParams->PreprocessCount; i++)
    {
        int Type = preprocessList[i];
        int windowSize = otherParams->FilterStrength;

        // -------- Type 0: 滑动平滑 --------
        if (Type == 0 && windowSize > 1)
        {
            for (int y = 0; y < lengthModel; y++)
            {
                int halfSize = windowSize / 2;
                int minY = y - halfSize;
                int maxY = y + halfSize;
                minY = minY < 0 ? 0 : minY;
                maxY = maxY < lengthModel ? maxY : lengthModel - 1;

                float sum = 0.0f;
                int count = maxY - minY + 1;

                for (int i = minY; i <= maxY; i++)
                {
                    sum += outputData[idx * waveLength + i];
                }
                tempData[idx * waveLength + y] = sum / count;
            }
            // 把临时缓冲拷回 outputData
            for (int y = 0; y < lengthModel; y++)
            {
                outputData[idx * waveLength + y] = tempData[idx * waveLength + y];
            }
        }
        // -------- Type 1: 一阶求导 --------
        else if (Type == 1)
        {
            for (int i = 0; i < lengthModel - 1; i++)
            {
                outputData[idx * waveLength + i] =
                    outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
            }
            outputData[idx * waveLength + lengthModel - 1]
                = outputData[idx * waveLength + lengthModel - 2];
        }
        // -------- Type 2: 二阶求导（连续做两次一阶）--------
        else if (Type == 2)
        {
            for (int i = 0; i < lengthModel - 1; i++)
            {
                outputData[idx * waveLength + i] =
                    outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
            }
            outputData[idx * waveLength + lengthModel - 1]
                = outputData[idx * waveLength + lengthModel - 2];
            for (int i = 0; i < lengthModel - 1; i++)
            {
                outputData[idx * waveLength + i] =
                    outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
            }
            outputData[idx * waveLength + lengthModel - 1]
                = outputData[idx * waveLength + lengthModel - 2];
        }
        // -------- Type 3: 最大最小归一化 --------
        else if (Type == 3)
        {
            float maxValue = outputData[idx * waveLength + 0];
            float minValue = outputData[idx * waveLength + 0];
            for (int i = 0; i < lengthModel; i++)
            {
                float value = outputData[idx * waveLength + i];
                if (value < minValue) minValue = value;
                if (value > maxValue) maxValue = value;
            }
            if (maxValue != minValue)
            {
                for (int i = 0; i < lengthModel; i++)
                {
                    outputData[idx * waveLength + i] =
                        (outputData[idx * waveLength + i] - minValue) / (maxValue - minValue);
                }
            }
            else
            {
                // 整段相同 → 全置 0 避免除零
                for (int i = 0; i < lengthModel; i++)
                {
                    outputData[idx * waveLength + i] = 0.0f;
                }
            }
        }
    }

    // ===== Step 4: PLS-DA 分选 =====
    // 对每个分类（modeNum 个），计算判别值；第一个超阈值的分类即为结果
    tags[idx] = 0;  // 默认为 0（未识别为任何目标）
    for (size_t px = 0; px < otherParams->modeNum; px++)
    {
        KDsum = 0;
        // 对每个选用的波段做标准化和加权
        for (int j = 0; j < otherParams->BandsCount; j++)
        {
            // 取该波段的反射率（注意波段索引转换）
            float ref1 = outputData[idx * waveLength + modelWaveList[j] - startBandIndex];
            // 中心化 × 系数
            ref1 = (ref1 - MeanX[px * otherParams->BandsCount + j])
                * Coef[px * otherParams->BandsCount + j];
            // 除以标准差
            ref1 = ref1 / StdX[px * otherParams->BandsCount + j];
            KDsum += ref1;
        }
        float _acc = KDsum + Intercept[px];
        // 判别值超阈值 → 标记为该分类 ID，跳出（先到先得）
        if (_acc > otherParams->Threshold[px])
        {
            tags[idx] = otherParams->modeIndex[px];
            break;
        }
    }
}

// ============================================================================
// calc_GPU: 在主机端发起 GPU 计算
//   网格维度：dealCount × 2
//   块维度  ：g_Samples / 2
//   总线程数 = dealCount × g_Samples（每像素一线程）
// ============================================================================
bool calc_GPU(char* frameData, float* tempData, float* outputData,
    float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
    OtherParams* otherParams, int* preprocessList, int* modelWaveList,
    float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags)
{
    cudaError_t cudaStatus;
    dim3 gridDim(dealCount, 2);
    dim3 blockDim(g_Samples / 2);

    // 启动核函数（注意全部使用模块级显存指针，参数仅用于签名兼容）
    calcKernel_GPU << <gridDim, blockDim >> > (
        cuda_frameData, cuda_tempData, cuda_outputData,
        cuda_k_White, cuda_White_ReadBytes, cuda_Black_ReadBytes,
        cuda_otherParams, cuda_preprocessList, cuda_modelWaveList,
        cuda_Intercept, cuda_Coef, cuda_StdX, cuda_MeanX, cuda_tags);

    // 检查启动错误
    cudaStatus = cudaGetLastError();
    if (cudaStatus != cudaSuccess) {
        return false;
    }

    // 等待 GPU 计算完成
    cudaStatus = cudaDeviceSynchronize();
    if (cudaStatus != cudaSuccess) {
        return false;
    }
    return true;
}

// ============================================================================
// calc_CPU: GPU 算法的 CPU 实现，仅用于无 GPU 时的备用或对比验证
// 算法逻辑与 calcKernel_GPU 完全一致，只是改成两层 for 循环
// ============================================================================
void calc_CPU(char* frameData, float* tempData, float* outputData,
    float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
    OtherParams* otherParams, int* preprocessList, int* modelWaveList,
    float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags)
{
    for (size_t kk = 0; kk < dealCount; kk++)
    {
        for (size_t xx = 0; xx < g_Samples; xx++)
        {
            int idx = kk * g_Samples + xx;
            int line = kk;
            int sample = xx;
            int AllSamples = g_Samples;
            int waveLength = otherParams->waveLength;
            int startBandIndex = otherParams->startBandIndex;
            int lengthModel = otherParams->endBandIndex - otherParams->startBandIndex + 1;

            float KDsum = 0;

            // === 校准 ===
            {
                for (int i = 0; i < lengthModel; i++)
                {
                    int trueBand = i + otherParams->RoiBandStartIndex;
                    int refIndex = sample + trueBand * AllSamples;
                    int lineIndex = line * AllSamples * waveLength;
                    // 手动从字节流构造 uint16
                    unsigned char byte0 = frameData[lineIndex + refIndex * 2];
                    unsigned char byte1 = frameData[lineIndex + refIndex * 2 + 1];
                    float needOri = (unsigned short)(byte0 | (byte1 << 8));

                    float needRefK = k_White[trueBand];
                    float needWhite = White_ReadBytes[refIndex];
                    float needBlack = Black_ReadBytes[refIndex];
                    if (needWhite <= needBlack || needOri <= needBlack)
                        needOri = 0;
                    else
                        needOri = (needOri - needBlack) / (needWhite - needBlack) * needRefK;
                    outputData[idx * waveLength + i] = needOri;
                    KDsum += needOri;
                }
            }

            // === 扣底 ===
            if (otherParams->LimitScopeFlag == 1)
            {
                if (KDsum < otherParams->LowestValue || KDsum > otherParams->HighestValue)
                {
                    tags[idx] = 255;
                }
            }

            // === 预处理（与 GPU 版完全一致，省略详细注释）===
            for (int i = 0; i < otherParams->PreprocessCount; i++)
            {
                int Type = preprocessList[i];
                int windowSize = otherParams->FilterStrength;
                if (Type == 0 && windowSize > 1) {
                    // 滑动平滑（同 GPU 版本）
                    for (int y = 0; y < lengthModel; y++)
                    {
                        int halfSize = windowSize / 2;
                        int minY = y - halfSize; if (minY < 0) minY = 0;
                        int maxY = y + halfSize; if (maxY >= lengthModel) maxY = lengthModel - 1;
                        float sum = 0.0f; int count = maxY - minY + 1;
                        for (int i = minY; i <= maxY; i++)
                            sum += outputData[idx * waveLength + i];
                        tempData[idx * waveLength + y] = sum / count;
                    }
                    for (int y = 0; y < lengthModel; y++)
                        outputData[idx * waveLength + y] = tempData[idx * waveLength + y];
                }
                else if (Type == 1) {
                    // 一阶导
                    for (int i = 0; i < lengthModel - 1; i++)
                        outputData[idx * waveLength + i] =
                        outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
                    outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
                }
                else if (Type == 2) {
                    // 二阶导
                    for (int i = 0; i < lengthModel - 1; i++)
                        outputData[idx * waveLength + i] =
                        outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
                    outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
                    for (int i = 0; i < lengthModel - 1; i++)
                        outputData[idx * waveLength + i] =
                        outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
                    outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
                }
                else if (Type == 3) {
                    // 最大最小归一化
                    float maxValue = outputData[idx * waveLength + 0];
                    float minValue = outputData[idx * waveLength + 0];
                    for (int i = 0; i < lengthModel; i++) {
                        float value = outputData[idx * waveLength + i];
                        if (value < minValue) minValue = value;
                        if (value > maxValue) maxValue = value;
                    }
                    if (maxValue != minValue) {
                        for (int i = 0; i < lengthModel; i++)
                            outputData[idx * waveLength + i] =
                            (outputData[idx * waveLength + i] - minValue) / (maxValue - minValue);
                    }
                    else {
                        for (int i = 0; i < lengthModel; i++)
                            outputData[idx * waveLength + i] = 0.0f;
                    }
                }
            }

            // === 分选（多分类，先到先得）===
            tags[idx] = 0;
            for (size_t px = 0; px < otherParams->modeNum; px++)
            {
                KDsum = 0;
                for (int j = 0; j < otherParams->BandsCount; j++)
                {
                    float ref1 = outputData[idx * waveLength + modelWaveList[j] - startBandIndex];
                    ref1 = (ref1 - MeanX[px * otherParams->BandsCount + j])
                        * Coef[px * otherParams->BandsCount + j];
                    ref1 = ref1 / StdX[px * otherParams->BandsCount + j];
                    KDsum += ref1;
                }
                float _acc = KDsum + Intercept[px];
                if (_acc > otherParams->Threshold[px])
                {
                    tags[idx] = otherParams->modeIndex[px];
                    break;
                }
            }
        }
    }
}
