#include <stdio.h>
#include <iostream>
#include <thread>
#include <thread>
#include <mutex>
#include <chrono>
#include <queue>
#include <chrono>
#include "windows.h"
#include <ShlObj.h>

#include "GrabData.h"
#include "Camera.h"
#include "CameraEnumerate.h"
#include "CameraInfo.h"


#include "kernel.h"
#include "PlsAI.h"
#include "CalibrationInfo.h"
#include "LockFreeQueue.h"
#include "AIModel.h"
#include "Eject.h"


using namespace std;
using namespace BaseCamera;
using namespace	FactoryCameras;

//当前状态
string currentStatus = "1";
//分选状态
bool startFlag = false;

Camera* g_camera = nullptr;
vector<CameraInfo*>* g_cameraInfos = nullptr;

int g_Samples = 640;
int g_bands = 102;

AIModel aiModel;
CalibrationInfo calibrationInfo;
int dealCount = 50;
int frameq;

// 共享的线程安全队列
LockFreeQueue<GrabData*> grabDatas;
LockFreeQueue<char*> sortResultDatas;
LockFreeQueue<float> sortResultTime;
LockFreeQueue<char> firstByteList;

//方法
bool Connect();
bool Configure();
bool Start();
bool Stop();
bool Disconnect();

//事件
void UserGrabedDataEvent(GrabData* grabData);

HANDLE hEvent = NULL;
HANDLE hEventSort = NULL;

BOOL CtrlHandler(DWORD fdwCtrlType) {
	switch (fdwCtrlType) {
	case CTRL_CLOSE_EVENT:
		std::cout << "控制台窗口正在关闭...\n";
		if (startFlag)
		{
			//停止采集
			bool result = Stop();
			//关闭相机
			result = Disconnect();
		}
		// 在这里执行任何清理操作
		return TRUE;
	default:
		return FALSE;
	}
}
string GetDocumentPath()
{
	char path[MAX_PATH];
	if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path) == S_OK)
		return string(path);
	else
		return "";
}

int main()
{
	// 注册关闭事件处理程序
	SetConsoleCtrlHandler(CtrlHandler, TRUE);
	// 注册消息句柄
	hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	hEventSort = CreateEvent(NULL, FALSE, TRUE, NULL);

	cout << "正在启动分选程序... " << endl;

Restart:
	bool result;
	//连接相机
	result = Connect();

	string pathDoc = GetDocumentPath();
	//读取模型参数
	string pathMode = pathDoc + "\\SortingExpert\\param\\modeCpp.models";
	std::ifstream inputFile(pathMode);
	json jRead;
	inputFile >> jRead;
	aiModel.from_json(jRead);
	inputFile.close();  // 关闭文件

	//读取校准文件
	string pathCali = pathDoc + "\\SortingExpert\\param\\modelCpp.calibrationInfo";
	calibrationInfo.loadData(pathCali);

	//配置相机
	result = Configure();
	g_Samples = g_camera->GetSamples()->getValue();
	g_bands = g_camera->GetBands()->getValue();

	//初始化GPU
	result = initGpu();

	//发送气吹初始化
	Start_send();
	std::thread udpReceiveThread(UDP_receive_thread);
	udpReceiveThread.detach();

	//开始采集
	startFlag = true;
	result = Start();
	cout << "分选算法运行中 " << endl;
	goto Menu;

Stop:
	//停止采集
	result = Stop();
	startFlag = false;

	//关闭相机
	result = Disconnect();
	if (currentStatus == "0")
	{
		return 0;
	}
	goto Menu;

Menu:
	while (true)
	{
		string x;
		cout << "输入：0 关闭程序; 1 启动; 2 停止 " << endl;
		cin >> x;
		if (x == "1" && currentStatus != "1")
		{
			currentStatus = "2";
			goto Restart;
		}
		else if (x == "2" && currentStatus != "2")
		{
			currentStatus = "2";
			goto Stop;
		}
		else if (x == "0")
		{
			if (currentStatus == "1")
			{
				currentStatus = "0";
				goto Stop;
			}
			else if (currentStatus == "2")
			{
				return 0;
			}
		}
	}
}

bool Connect()
{
	vector<Camera*>* cameras = nullptr;
	try
	{
		//通过调用GetCameras方法，您可以获取该电脑已连接的相机列表
		//By calling the GetCameras method, you can obtain a list of cameras connected to the computer
		cameras = CameraEnumerate::GetCameras(nullptr);
		std::cout << "find " << cameras->size() << " camera" << std::endl;
		if (cameras->size() == 0)
		{
			std::cout << ("=================(Execution completed=================)") << std::endl;
			string num;
			std::cin >> num;
		}
		for (size_t i = 0; i < cameras->size(); i++)
		{
			std::cout << "Number" << (i + 1) << "The camera serial number is" << (*cameras)[i]->Info->InstrumentSN << std::endl;
		}
	}
	catch (const char* exception)
	{
		std::cout << exception << std::endl;
		string num;
		std::cin >> num;
		return false;
	}

	// 订阅事件接收高光谱图像
	// Subscription event to receive hyperspectral images
	Camera::CameraGrabedData = UserGrabedDataEvent;
	std::cout << "Subscribe to receive hyperspectral image events" << std::endl;

	g_camera = (*cameras)[0];
	try
	{
		//打开高光谱相机
		//Open the hyperspectral camera
		g_camera->OpenCamera(nullptr);
		std::cout << "Open the hyperspectral camera)" << std::endl;
	}
	catch (const char* exception)
	{
		std::cout << exception << std::endl;
		string num;
		std::cin >> num;
		return false;
	}
	//获取版本
	//string v = g_camera->getVersion();
	//auto other = camera->getOtherInfo();
	return true;
}

bool Configure()
{
	try
	{
		//设置像素格式
		//Set pixel format
		if (g_camera->SetPixelFormat(EnumPixelFormat::Mono14))
			std::cout << "设置像素格式为Mono14（Set pixel format to Mono14）" << std::endl;
		else
			std::cout << "设置像素格式失败（Failed to set pixel format）" << std::endl;

		//设置空间合并
		//Set Space Merge
		if (g_camera->SetSampleBinning(EnumBinning::Binning1))
			std::cout << ("设置空间合并为1合并（Set space merge to 1 merge）") << std::endl;
		else std::cout << ("设置空间合并失败（Failed to merge space settings）") << std::endl;

		//设置光谱合并
		//Set spectral merging
		if (g_camera->SetBandBinning(EnumBinning::Binning1))
			std::cout << ("设置光谱合并为1合并（Set spectrum merge to 1 merge）") << std::endl;
		else std::cout << ("设置光谱合并失败（Failed to set spectral merge）") << std::endl;
		//设置增益 0 高增益 1 中增益 2 低增益
		//Set gain  0: high gain  1: medium gain  2: low gain
		if (g_camera->SetGain(aiModel.Gain))
			std::cout << ("设置增益（Set the gain to high gain） :") << aiModel.Gain << std::endl;
		else std::cout << ("设置增益失败（Failed to set gain）") << std::endl;

		//设置曝光时间
		//Set exposure time
		if (g_camera->SetExposureTime(aiModel.ExpTime))
			std::cout << ("设置曝光时间（Set the exposure time）:") << aiModel.ExpTime << std::endl;
		else std::cout << ("设置曝光时间失败（Failed to set exposure time）") << std::endl;

		//设置触发模式
		//Set trigger mode
		if (g_camera->SetTriggerMode(EnumTriggerMode::Off))
			std::cout << ("设置触发模式为内触发（Set the trigger mode to internal trigger）") << std::endl;
		else std::cout << ("设置触发模式失败（Failed to set trigger mode）") << std::endl;
	/*	if (g_camera->SetSampleDirection(EnumDirection::Reverse))
			std::cout << ("设置镜像采集成功") << std::endl;
		else
			std::cout << ("设置镜像采集失败") << std::endl;*/

		//设置工作模式为bank模式(Set the working mode to bank mode)
		auto bandz = g_camera->GetBandROI()->getBandZones();
		if (aiModel.EndBandIndex - aiModel.StartBandIndex > 2 && aiModel.EndBandIndex - aiModel.StartBandIndex + 1 != g_camera->GetBands()->getValue() && bandz->size() > 0)
		{
			(*bandz)[0]->setEnable(true);

			auto ofst = (*bandz)[0]->getOffset();
			int offsetInc = ofst->getInc();
			int offmod = aiModel.StartBandIndex % offsetInc;
			int offset = aiModel.StartBandIndex - offmod;
			ofst->setValue(offset);
			(*bandz)[0]->setOffset(ofst);

			auto sz = (*bandz)[0]->getSize();
			int size = aiModel.EndBandIndex - offset + 1;
			int szInc = sz->getInc();
			int szMod = size % szInc;
			size = szMod == 0 ? size : size + szInc - szMod;
			
			sz->setValue(size);
			(*bandz)[0]->setSize(sz);

			//设置工作模式为1个BANK
			//Set the working mode to 1 bank
			if (g_camera->SetBandROI((*bandz)[0]))
				std::cout << ("设置工作模式为bank模式(Set the working mode to bank mode)") << std::endl;
			else std::cout << ("设置工作模式失败(Failed to set working mode)") << std::endl;
		}

		//设置帧频
		//Set frame rate
		//if (g_camera->SetFrameRate(3500))
		frameq = aiModel.FrameRate;
		if (g_camera->SetAcquisitionFrameRate(frameq))
			//if (g_camera->SetFrameRate(info->FrameRateMax))
			std::cout << ("设置帧频（Set the frame rate）") << frameq << std::endl;
		else std::cout << ("设置帧频失败（Failed to set frame rate）") << std::endl;
	}
	catch (const char* exception)
	{
		std::cout << exception << std::endl;
		string num;
		std::cin >> num;
		return false;
	}
	return true;
}

/// <summary>
/// 数据消息接收方法 (Data collection  receive message)
/// </summary>
/// <param name="grabData"></param>
void UserGrabedDataEvent(GrabData* grabData)
{
	grabDatas.enqueue(grabData);
	if (grabDatas.size() >= dealCount)
	{
		SetEvent(hEventSort);
	}
}

//#define TIME_RECORD   //打印时间 
#define OUTPUT_SORTING_RESULT   //打印结果
#define GPU_MODE
//#define CPU_MODE

// 线程函数
void threadFunction()
{
	try
	{
#ifdef CPU_MODE
		char* cpu_frameData;
		cpu_frameData = (char*)malloc(dealCount * g_Samples * g_bands * 2 * sizeof(char));
		float* cpu_outputData;
		cpu_outputData = (float*)malloc(dealCount * g_Samples * g_bands * sizeof(float));
		float* cpu_tempData;
		cpu_tempData = (float*)malloc(dealCount * g_Samples * g_bands * sizeof(float));
		char* tags;
		tags = (char*)malloc(dealCount * g_Samples * sizeof(char));

		OtherParams* cpu_lOtherParams = new OtherParams;
		cpu_lOtherParams->waveLength = g_bands;
		cpu_lOtherParams->startBandIndex = aiModel.StartBandIndex;
		cpu_lOtherParams->endBandIndex = aiModel.EndBandIndex;
		cpu_lOtherParams->BandsCount = aiModel.CoreList[0].selectIndex.size();
		cpu_lOtherParams->Threshold = aiModel.CoreList[0].threshold;
		cpu_lOtherParams->RoiBandStartIndex = aiModel.RoiBandStartIndex;
		cpu_lOtherParams->LimitScopeFlag = aiModel.LimitScopeFlag;
		cpu_lOtherParams->LowestValue = aiModel.LowestValue;
		cpu_lOtherParams->HighestValue = aiModel.HighestValue;
		cpu_lOtherParams->PreprocessCount = aiModel.Preprocessings.size();
		cpu_lOtherParams->FilterStrength = aiModel.FilterStrength;
#endif
		cudaError_t cudaStatus;
		// 创建 CUDA 事件

		while (startFlag)
		{	// 记录数据拷贝开始时间
			WaitForSingleObject(hEventSort, INFINITE);
			int count = grabDatas.size();
			if (count >= dealCount)
			{
#ifdef GPU_MODE
				if (true)
				{
					GrabData* data;
					for (size_t i = 0; i < dealCount; i++)
					{
						grabDatas.dequeue(data);
						// Copy input vectors from host memory to GPU buffers.
						cudaStatus = cudaMemcpy(cuda_frameData + i * g_Samples * g_bands * 2, data->Values, g_Samples * g_bands * 2 * sizeof(char), cudaMemcpyHostToDevice);
						////记录频率
						if (i == 0)
						{
							firstByteList.enqueue(data->Values[0]);
						}
						delete data;
						if (cudaStatus != cudaSuccess)
						{
							fprintf(stderr, "cudaMemcpy failed!");
							goto Error;
						}
					}

#ifdef TIME_RECORD
					std::cout << "Data copy time: " << copyTime << " ms" << std::endl;
#endif // TIME_RECORD
					calc_GPU(cuda_frameData, cuda_tempData, cuda_outputData, cuda_k_White, cuda_White_ReadBytes, cuda_Black_ReadBytes,
						cuda_otherParams, cuda_preprocessList, cuda_modelWaveList, cuda_Intercept, cuda_Coef, cuda_StdX, cuda_MeanX, cuda_tags);

					// 为主机内存分配空间（假设数据大小为 Samples 个 char）
					char* sortResult = (char*)malloc(dealCount * g_Samples * sizeof(char));
					if (sortResult == NULL) {
						fprintf(stderr, "Host memory allocation failed!");
						goto Error;
					}
					// Copy output vector from GPU buffer to host memory.
					cudaStatus = cudaMemcpy(sortResult, cuda_tags, dealCount * g_Samples * sizeof(char), cudaMemcpyDeviceToHost);
					if (cudaStatus != cudaSuccess) {
						fprintf(stderr, "cudaMemcpy failed!");
						goto Error;
					}

#ifdef OUTPUT_SORTING_RESULT
					sortResultDatas.enqueue(sortResult);
					SetEvent(hEvent);
#endif // OUTPUT_SORTING_RESULT_RIGHTTIME
				}
#endif
#ifdef CPU_MODE
				if (false)
				{
					// 获取开始时间
					auto start = std::chrono::high_resolution_clock::now();
					GrabData* data;
					for (size_t i = 0; i < dealCount; i++)
					{
						grabDatas.dequeue(data);
						memcpy(cpu_frameData + i * g_Samples * g_bands * 2 * sizeof(char), data->Values, g_Samples * g_bands * 2 * sizeof(char));
						delete data;
					}
					// 获取结束时间
					auto end = std::chrono::high_resolution_clock::now();
					// 计算持续时间
					std::chrono::duration<double> duration = end - start;
					// 输出程序运行时间
					std::cout << "程序运行时间: " << duration.count() << " 秒" << std::endl;
					// Launch a kernel on the GPU with one thread for each element.
					calc_CPU(cpu_frameData, cpu_tempData, cpu_outputData, calibrationInfo.k_White.data(), calibrationInfo.White_ReadBytes.data(), calibrationInfo.Black_ReadBytes.data(),
						cpu_lOtherParams, aiModel.Preprocessings.data(), aiModel.CoreList[0].selectIndex.data(), aiModel.CoreList[0].Intercept.data(), aiModel.CoreList[0].Coef.data(), aiModel.CoreList[0].StdX.data(), aiModel.CoreList[0].MeanX.data(), tags);
				}
#endif // CPU_MODE
			}
		Error:
			;
		}
#ifdef GPU_MODE
		clearGpu();
#endif // GPU_MODE
#ifdef CPU_MODE
		delete cpu_frameData;
		delete cpu_outputData;
		delete cpu_tempData;
		delete tags;
		delete cpu_lOtherParams;
#endif // CPU_MODE
	}
	catch (const char* exception)
	{
		std::cout << exception << std::endl;
		string num;
		std::cin >> num;
		return;
	}
}

#define DIS_SORT_RESULT
void threadFunction_dis()
{
	while (startFlag)
	{
		WaitForSingleObject(hEvent, INFINITE);
		if (sortResultDatas.size() > 0)
		{
			// 获取程序开始时间
			char* data2;// = new char[dealCount * 640];
			sortResultDatas.dequeue(data2);
			firstByteList.dequeue(firstByte);
#ifdef DIS_SORT_RESULT
			std::string dis = "";
			int couttt1 = 0;
			for (size_t i = 0; i < dealCount; i++)
			{
				couttt1 = 0;
				dis = "";
				for (size_t pair = 0; pair < g_Samples; pair++)
				{
					if (data2[i * g_Samples + pair] != 0)
					{
						dis += std::to_string(data2[i * g_Samples + pair]) + " ";
					}
				}
				if (dis != "")
				{
					std::cout << dis << std::endl;
				}
			}
#elif
			ControlValvesWithRows(data2, dealCount);
#endif // DIS_SORT_RESULT
			delete data2;
		}
	}
}
//启动
bool Start()
{
	try
	{
		//开始采集
		if (g_camera->getOpen())
		{
			g_camera->StartGrab();
			std::cout << ("开始采集高光谱数据（Start collecting hyperspectral data）") << std::endl;
		}
	}
	catch (const char* exception)
	{
		return false;
	}
	try
	{
		std::thread t1(threadFunction);
		t1.detach();  // 分离线程，主线程不会等待子线程结束
		std::thread t2(threadFunction_dis);
		t2.detach();  // 分离线程，主线程不会等待子线程结束
	}
	catch (const char* exception)
	{
		return false;
	}
	return true;
}

//停止
bool Stop()
{
	try
	{
		//停止采集
		//Stop collecting
		g_camera->StopGrab();
		std::cout << ("停止采集高光谱数据（Stop collecting hyperspectral data）") << std::endl;
	}
	catch (const char* exception)
	{
		return false;
	}
	return true;
}

//断开连接
bool Disconnect()
{
	try
	{
		//关闭相机
		//Turn off the camera
		g_camera->CloseCamera();
		std::cout << ("关闭高光谱相机（Turn off the hyperspectral camera）") << std::endl;
	}
	catch (const char* exception)
	{
		return false;
	}
	return true;
}
