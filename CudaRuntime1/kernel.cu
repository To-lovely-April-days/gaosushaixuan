#include "kernel.h"
#include "AIModel.h"
#include "CalibrationInfo.h"

extern int dealCount;
extern int g_Samples;
extern int g_bands;

extern AIModel aiModel;;
extern CalibrationInfo calibrationInfo;


char* cuda_frameData;
float* cuda_tempData;
float* cuda_outputData;
char* cuda_tags;
float* cuda_Intercept;
float* cuda_Coef;//二维数组
float* cuda_StdX;
float* cuda_MeanX;
float* cuda_k_White;
float* cuda_White_ReadBytes;
float* cuda_Black_ReadBytes;
//模型使用的波段对应的序号列表
int* cuda_modelWaveList;
int* cuda_preprocessList;
OtherParams* cuda_otherParams;

/// <summary>
/// 开辟空间
/// 参数赋值
/// </summary>
bool initGpu()
{
	try
	{
		cudaError_t cudaStatus;
		int deviceCount;
		cudaGetDeviceCount(&deviceCount);
		// Choose which GPU to run on, change this on a multi-GPU system.
		//cudaStatus = cudaSetDevice(0);
		if (deviceCount == 0) {
			//printf("No CUDA-capable device detected, falling back to CPU execution.\n");
			// 代码将在 CPU 上执行
			return false;
		}
		else {
			// 选择 GPU 进行计算
			cudaStatus = cudaSetDevice(0); // 选择第一个 GPU
			if (cudaStatus != cudaSuccess) {
				//fprintf(stderr, "cudaSetDevice failed!  Do you have a CUDA-capable GPU installed?");
				goto Error;
			}
		}

		// Allocate GPU buffers for three vectors (two input, one output)    .
		cudaStatus = cudaMalloc((void**)&cuda_frameData, dealCount * g_Samples * g_bands * 2 * sizeof(char));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMalloc((void**)&cuda_outputData, dealCount * g_Samples * g_bands * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMalloc((void**)&cuda_tempData, dealCount * g_Samples * g_bands * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}

		cudaStatus = cudaMalloc((void**)&cuda_tags, dealCount * g_Samples * sizeof(char));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}

		//模型数据
		cudaStatus = cudaMalloc((void**)&cuda_Intercept, aiModel.CoreList.size() * aiModel.CoreList[0].Intercept.size() * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMalloc((void**)&cuda_Coef, aiModel.CoreList.size() * aiModel.CoreList[0].Coef.size() * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMalloc((void**)&cuda_StdX, aiModel.CoreList.size() * aiModel.CoreList[0].StdX.size() * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMalloc((void**)&cuda_MeanX, aiModel.CoreList.size() * aiModel.CoreList[0].MeanX.size() * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}

		for (size_t i = 0; i < aiModel.CoreList.size(); i++)
		{
			//赋值
			// Copy input vectors from host memory to GPU buffers.
			cudaStatus = cudaMemcpy(cuda_Intercept + aiModel.CoreList[0].Intercept.size() * i, aiModel.CoreList[i].Intercept.data(), aiModel.CoreList[0].Intercept.size() * sizeof(float), cudaMemcpyHostToDevice);
			if (cudaStatus != cudaSuccess) {
				//fprintf(stderr, "cudaMemcpy failed!");
				goto Error;
			}
			cudaStatus = cudaMemcpy(cuda_Coef + aiModel.CoreList[0].Coef.size() * i, aiModel.CoreList[i].Coef.data(), aiModel.CoreList[0].Coef.size() * sizeof(float), cudaMemcpyHostToDevice);
			if (cudaStatus != cudaSuccess) {
				//fprintf(stderr, "cudaMemcpy failed!");
				goto Error;
			}
			cudaStatus = cudaMemcpy(cuda_StdX + aiModel.CoreList[0].StdX.size() * i, aiModel.CoreList[i].StdX.data(), aiModel.CoreList[0].StdX.size() * sizeof(float), cudaMemcpyHostToDevice);
			if (cudaStatus != cudaSuccess) {
				//fprintf(stderr, "cudaMemcpy failed!");
				goto Error;
			}
			cudaStatus = cudaMemcpy(cuda_MeanX + aiModel.CoreList[0].MeanX.size() * i, aiModel.CoreList[i].MeanX.data(), aiModel.CoreList[0].MeanX.size() * sizeof(float), cudaMemcpyHostToDevice);
			if (cudaStatus != cudaSuccess) {
				//fprintf(stderr, "cudaMemcpy failed!");
				goto Error;
			}
		}

		//校准数据
		cudaStatus = cudaMalloc((void**)&cuda_k_White, calibrationInfo.k_White.size() * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMalloc((void**)&cuda_White_ReadBytes, calibrationInfo.White_ReadBytes.size() * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMalloc((void**)&cuda_Black_ReadBytes, calibrationInfo.Black_ReadBytes.size() * sizeof(float));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}

		//赋值
		cudaStatus = cudaMemcpy(cuda_k_White, calibrationInfo.k_White.data(), calibrationInfo.k_White.size() * sizeof(float), cudaMemcpyHostToDevice);
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMemcpy failed!");
			goto Error;
		}
		cudaStatus = cudaMemcpy(cuda_White_ReadBytes, calibrationInfo.White_ReadBytes.data(), calibrationInfo.White_ReadBytes.size() * sizeof(float), cudaMemcpyHostToDevice);
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMemcpy failed!");
			goto Error;
		}
		cudaStatus = cudaMemcpy(cuda_Black_ReadBytes, calibrationInfo.Black_ReadBytes.data(), calibrationInfo.Black_ReadBytes.size() * sizeof(float), cudaMemcpyHostToDevice);
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMemcpy failed!");
			goto Error;
		}

		//模型参数
		cudaStatus = cudaMalloc((void**)&cuda_modelWaveList, aiModel.CoreList[0].selectIndex.size() * sizeof(int));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMemcpy(cuda_modelWaveList, aiModel.CoreList[0].selectIndex.data(), aiModel.CoreList[0].selectIndex.size() * sizeof(int), cudaMemcpyHostToDevice);
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMemcpy failed!");
			goto Error;
		}

		//预处理
		cudaStatus = cudaMalloc((void**)&cuda_preprocessList, aiModel.Preprocessings.size() * sizeof(int));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
		cudaStatus = cudaMemcpy(cuda_preprocessList, aiModel.Preprocessings.data(), aiModel.Preprocessings.size() * sizeof(int), cudaMemcpyHostToDevice);
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMemcpy failed!");
			goto Error;
		}

		//核函数参数
		cudaStatus = cudaMalloc((void**)&cuda_otherParams, sizeof(OtherParams));
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMalloc failed!");
			goto Error;
		}
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
			lOtherParams->modeIndex[i] = aiModel.CoreList[i].classid;//目标的ID
			lOtherParams->Threshold[i] = aiModel.CoreList[i].threshold;
		}

		cudaStatus = cudaMemcpy(cuda_otherParams, lOtherParams, sizeof(OtherParams), cudaMemcpyHostToDevice);
		if (cudaStatus != cudaSuccess) {
			//fprintf(stderr, "cudaMemcpy failed!");
			goto Error;
		}
		delete lOtherParams;
	Error:
		return false;
	}
	catch (const char* exception)
	{

		return false;
	}
	return true;
}

bool clearGpu()
{
	cudaError_t cudaStatus;
	cudaStatus = cudaFree(cuda_frameData);
	cudaStatus = cudaFree(cuda_tempData);
	cudaStatus = cudaFree(cuda_outputData);
	cudaStatus = cudaFree(cuda_tags);
	cudaStatus = cudaFree(cuda_Intercept);
	cudaStatus = cudaFree(cuda_Coef);//二维数组
	cudaStatus = cudaFree(cuda_StdX);
	cudaStatus = cudaFree(cuda_MeanX);
	cudaStatus = cudaFree(cuda_k_White);
	cudaStatus = cudaFree(cuda_White_ReadBytes);
	cudaStatus = cudaFree(cuda_Black_ReadBytes);
	cudaStatus = cudaFree(cuda_modelWaveList);
	cudaStatus = cudaFree(cuda_preprocessList);
	cudaStatus = cudaFree(cuda_otherParams);

	// cudaDeviceReset must be called before exiting in order for profiling and
	// tracing tools such as Nsight and Visual Profiler to show complete traces.
	cudaStatus = cudaDeviceReset();
	if (cudaStatus != cudaSuccess) {
		fprintf(stderr, "cudaDeviceReset failed!");
	}
	return true;
}


/// <summary>
/// 计算用的核函数
/// </summary>
__global__ void calcKernel_GPU(char* frameData, float* tempData, float* outputData, float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
	OtherParams* otherParams, int* preprocessList, int* modelWaveList, float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags)
{
	//// blockIdx 当前线程块的序号  blockDim 线程块中线程总数  threadIdx 当前线程的序号
	int idx = blockIdx.x * gridDim.y * blockDim.x + blockIdx.y * blockDim.x + threadIdx.x;  // 全局索引
	//空间通道镜像处理
	int jx_idx = blockIdx.x * gridDim.y * blockDim.x + (gridDim.y * blockDim.x - (blockIdx.y * blockDim.x + threadIdx.x) - 1);

	int line = blockIdx.x;//当前线程块的序号  第几帧
	//int AllLines = gridDim.x;//线程块总数  
	int sample = blockIdx.y * blockDim.x + threadIdx.x;//当前线程的序号 第几个像素
	int AllSamples = gridDim.y * blockDim.x;//线程块中线程总数   像素总个数

	int waveLength = otherParams->waveLength;//roi之后采集数据的波段个数
	int startBandIndex = otherParams->startBandIndex;//起始波段
	int lengthModel = otherParams->endBandIndex - otherParams->startBandIndex + 1;//建模时设置的波长个数
	//int RoiBandStartIndex = otherParams->RoiBandStartIndex;//偏移量
	//printf("--otherParams->BandsCount %d, Intercept %f ,Coef %f Coef88 %f, StdX %f StdX88 %f,MeanX %f MeanX88 %f,\n", otherParams->BandsCount, Intercept[0], Coef[0], Coef[88], StdX[0], StdX[88], MeanX[0], MeanX[88]);
	//printf("Thread %d executing\n", idx);

	//定义临时变量
	//float lref[90];
	float KDsum = 0;
	//校准
	{
		for (int i = 0; i < lengthModel; i++)
		{
			int trueBand = i + otherParams->RoiBandStartIndex;   //有效波长，0起始
			int refIndex = sample + trueBand * AllSamples;
			int lineIndex = line * AllSamples * waveLength;
			float needOri = ((unsigned short*)frameData)[lineIndex + refIndex];
			/*unsigned char byte0 = frameData[lineIndex + refIndex * 2];
			unsigned char byte1 = frameData[lineIndex + refIndex * 2 + 1];
			float needOri = (unsigned short)(byte0 | (byte1 << 8));*/

			float needRefK = k_White[trueBand];
			float needWhite = White_ReadBytes[refIndex];
			float needBlack = Black_ReadBytes[refIndex];
			if (needWhite <= needBlack || needOri <= needBlack)
			{
				needOri = 0;
			}
			else
			{
				needOri = (needOri - needBlack) / (needWhite - needBlack) * needRefK;
			}
			outputData[idx * waveLength + i] = needOri;
			KDsum += needOri;
		}
	}
	//扣底
	if (otherParams->LimitScopeFlag == 1)
	{
		int lower = otherParams->LowestValue * lengthModel;
		int higher = otherParams->HighestValue * lengthModel;

		if (KDsum >= lower && KDsum <= higher)
		{
			tags[idx] = 255;
			return;
		}
	}
	//预处理   仅支持滑动平滑，一阶求导，二阶求导，最大最小归一化
	for (int i = 0; i < otherParams->PreprocessCount; i++)
	{
		int Type = preprocessList[i];
		int windowSize = otherParams->FilterStrength;//设置参数
		//滑动平滑
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

				// 使用单一循环计算总和
				for (int i = minY; i <= maxY; i++)
				{
					sum += outputData[idx * waveLength + i];
				}
				// 计算平均值并存储到 tempData 中
				tempData[idx * waveLength + y] = sum / count;
			}
			for (int y = 0; y < lengthModel; y++)
			{
				outputData[idx * waveLength + y] = tempData[idx * waveLength + y];
			}
		}
		else if (Type == 1)
		{
			//一阶求导
			for (int i = 0; i < lengthModel - 1; i++)
			{
				outputData[idx * waveLength + i] = outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
			}
			outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
		}
		else if (Type == 2)
		{
			//二阶求导
			for (int i = 0; i < lengthModel - 1; i++)
			{
				outputData[idx * waveLength + i] = outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
			}
			outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
			for (int i = 0; i < lengthModel - 1; i++)
			{
				outputData[idx * waveLength + i] = outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
			}
			outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
		}
		else if (Type == 3)
		{
			//最大最小归一化
			float maxValue = outputData[idx * waveLength + 0];
			float minValue = outputData[idx * waveLength + 0];
			for (int i = 0; i < lengthModel; i++)
			{
				float value = outputData[idx * waveLength + i];
				if (value < minValue)
				{
					minValue = value;
				}
				if (value > maxValue)
				{
					maxValue = value;
				}
			}
			if (maxValue != minValue)
			{
				for (int i = 0; i < lengthModel; i++)
				{
					outputData[idx * waveLength + i] = (outputData[idx * waveLength + i] - minValue) / (maxValue - minValue);
				}
			}
			else
			{
				for (int i = 0; i < lengthModel; i++)
				{
					outputData[idx * waveLength + i] = 0.0f;
				}
			}
		}
	}
	//分选
	// //分选
	tags[idx] = 0;
	for (size_t px = 0; px < otherParams->modeNum; px++)
	{
		KDsum = 0;
		for (int j = 0; j < otherParams->BandsCount; j++) //选择的波段数
		{
			float ref1 = outputData[idx * waveLength + modelWaveList[j] - startBandIndex];//&&&&&&&&&&&&&&&可优化
			ref1 = (ref1 - MeanX[px * otherParams->BandsCount+ j]) * Coef[px * otherParams->BandsCount + j]; //plsda 预测  //&&&&&&&&&&&&&&&&&可优化  ;// 
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
	//模型只有一个
	//float record = 0.0f;
	//KDsum = 0;
	//for (int j = 0; j < otherParams->BandsCount; j++) //选择的波段数
	//{
	//	float ref1 = outputData[idx * waveLength + modelWaveList[j] - startBandIndex];//&&&&&&&&&&&&&&&可优化
	//	ref1 = (ref1 - MeanX[j]) * Coef[j]; //plsda 预测  //&&&&&&&&&&&&&&&&&可优化  ;// 
	//	ref1 = ref1 / StdX[j];
	//	KDsum += ref1;
	//}
	//float _acc = KDsum + Intercept[0];
	//if (_acc > otherParams->Threshold && _acc > record)
	//{
	//	record = _acc;
	//	tags[jx_idx] = 1;
	//}
	//else
	//{
	//	tags[jx_idx] = 0;
	//}
}


bool calc_GPU(char* frameData, float* tempData, float* outputData, float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
	OtherParams* otherParams, int* preprocessList, int* modelWaveList, float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags)
{
	cudaError_t cudaStatus;
	// 记录计算开始时间
			//cudaEventRecord(start);
	dim3 gridDim(dealCount, 2);    // 网格包含 5 个线程块
	dim3 blockDim(g_Samples / 2);   // 每个线程块包含 10 个线程
		// Launch a kernel on the GPU with one thread for each element.
	calcKernel_GPU <<<gridDim, blockDim >> > (cuda_frameData, cuda_tempData, cuda_outputData, cuda_k_White, cuda_White_ReadBytes, cuda_Black_ReadBytes,
		cuda_otherParams, cuda_preprocessList, cuda_modelWaveList, cuda_Intercept, cuda_Coef, cuda_StdX, cuda_MeanX, cuda_tags);

	// Check for any errors launching the kernel
	cudaStatus = cudaGetLastError();
	if (cudaStatus != cudaSuccess) {
		//fprintf(stderr, "addKernel launch failed: %s\n", cudaGetErrorString(cudaStatus));
		return false;
	}

	// cudaDeviceSynchronize waits for the kernel to finish, and returns
	// any errors encountered during the launch.
	cudaStatus = cudaDeviceSynchronize();
	if (cudaStatus != cudaSuccess) {
		//fprintf(stderr, "cudaDeviceSynchronize returned error code %d after launching addKernel!\n", cudaStatus);
		return false;
	}
	return true;
}



/// <summary>
/// 计算用的核函数
/// </summary>
void calc_CPU(char* frameData, float* tempData, float* outputData, float* k_White, float* White_ReadBytes, float* Black_ReadBytes,
	OtherParams* otherParams, int* preprocessList, int* modelWaveList, float* Intercept, float* Coef, float* StdX, float* MeanX, char* tags)
{
	for (size_t kk = 0; kk < dealCount; kk++)
	{
		for (size_t xx = 0; xx < g_Samples; xx++)
		{
			//// blockIdx 线程块X索引  blockDim 线程块中线程个数  threadIdx 当前线程序号
			int idx = kk * g_Samples + xx;  // 全局索引
			int line = kk;//线程块序号
			//int AllLines = gridDim.x;//线程块中线程个数
			int sample = xx;//空间像素序号
			int AllSamples = g_Samples;//空间像素总数
			int waveLength = otherParams->waveLength;//roi之后采集数据的波段个数
			int startBandIndex = otherParams->startBandIndex;//起始波段
			int lengthModel = otherParams->endBandIndex - otherParams->startBandIndex + 1;//建模时设置的波长个数
			//int RoiBandStartIndex = otherParams->RoiBandStartIndex;//偏移量
			//printf("--otherParams->BandsCount %d, Intercept %f ,Coef %f Coef88 %f, StdX %f StdX88 %f,MeanX %f MeanX88 %f,\n", otherParams->BandsCount, Intercept[0], Coef[0], Coef[88], StdX[0], StdX[88], MeanX[0], MeanX[88]);
			//printf("Thread %d executing\n", idx);

			//定义临时变量
			//float lref[90];
			float KDsum = 0;
			//校准
			{
				for (int i = 0; i < lengthModel; i++)
				{
					int trueBand = i + otherParams->RoiBandStartIndex;   //有效波长，0起始
					int refIndex = sample + trueBand * AllSamples;
					int lineIndex = line * AllSamples * waveLength;
					unsigned char byte0 = frameData[lineIndex + refIndex * 2];
					unsigned char byte1 = frameData[lineIndex + refIndex * 2 + 1];

					float needOri = (unsigned short)(byte0 | (byte1 << 8));

					float needRefK = k_White[trueBand];
					float needWhite = White_ReadBytes[refIndex];
					float needBlack = Black_ReadBytes[refIndex];
					if (needWhite <= needBlack || needOri <= needBlack)
					{
						needOri = 0;
					}
					else
					{
						needOri = (needOri - needBlack) / (needWhite - needBlack) * needRefK;
					}
					outputData[idx * waveLength + i] = needOri;
					KDsum += needOri;
				}
			}
			//扣底
			if (otherParams->LimitScopeFlag == 1)
			{
				if (KDsum< otherParams->LowestValue || KDsum>otherParams->HighestValue)
				{
					tags[idx] = 255;
				}
			}
			//预处理   仅支持滑动平滑，一阶求导，二阶求导，最大最小归一化
			for (int i = 0; i < otherParams->PreprocessCount; i++)
			{
				int Type = preprocessList[i];
				int windowSize = otherParams->FilterStrength;//设置参数
				//滑动平滑
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

						// 使用单一循环计算总和
						for (int i = minY; i <= maxY; i++)
						{
							sum += outputData[idx * waveLength + i];
						}
						// 计算平均值并存储到 tempData 中
						tempData[idx * waveLength + y] = sum / count;
					}
					for (int y = 0; y < lengthModel; y++)
					{
						outputData[idx * waveLength + y] = tempData[idx * waveLength + y];
					}
				}
				else if (Type == 1)
				{
					//一阶求导
					for (int i = 0; i < lengthModel - 1; i++)
					{
						outputData[idx * waveLength + i] = outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
					}
					outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
				}
				else if (Type == 2)
				{
					//二阶求导
					for (int i = 0; i < lengthModel - 1; i++)
					{
						outputData[idx * waveLength + i] = outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
					}
					outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
					for (int i = 0; i < lengthModel - 1; i++)
					{
						outputData[idx * waveLength + i] = outputData[idx * waveLength + i + 1] - outputData[idx * waveLength + i];
					}
					outputData[idx * waveLength + lengthModel - 1] = outputData[idx * waveLength + lengthModel - 2];
				}
				else if (Type == 3)
				{
					//最大最小归一化
					float maxValue = outputData[idx * waveLength + 0];
					float minValue = outputData[idx * waveLength + 0];
					for (int i = 0; i < lengthModel; i++)
					{
						float value = outputData[idx * waveLength + i];
						if (value < minValue)
						{
							minValue = value;
						}
						if (value > maxValue)
						{
							maxValue = value;
						}
					}
					if (maxValue != minValue)
					{
						for (int i = 0; i < lengthModel; i++)
						{
							outputData[idx * waveLength + i] = (outputData[idx * waveLength + i] - minValue) / (maxValue - minValue);
						}
					}
					else
					{
						for (int i = 0; i < lengthModel; i++)
						{
							outputData[idx * waveLength + i] = 0.0f;
						}
					}
				}
			}
			//分选
			tags[idx] = 0;
			for (size_t px = 0; px < otherParams->modeNum; px++)
			{
				KDsum = 0;
				for (int j = 0; j < otherParams->BandsCount; j++) //选择的波段数
				{
					float ref1 = outputData[idx * waveLength + modelWaveList[j] - startBandIndex];//&&&&&&&&&&&&&&&可优化
					ref1 = (ref1 - MeanX[px * otherParams->BandsCount+ j]) * Coef[px * otherParams->BandsCount + j]; //plsda 预测  //&&&&&&&&&&&&&&&&&可优化  ;// 
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

