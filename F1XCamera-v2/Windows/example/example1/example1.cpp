// Example1.cpp :
// 功能介绍(Function introduce )
// 1	链接相机(Connect camera)
// 2	相机设置(Setting camera)
// 3	数据采集(Grab camera data)
// 4	数据解析(Data Parser)
// 5	反射率校准(Calibration)
// 6	关闭相机(Close camera)

#include <iostream>
#include <thread>
#include "GrabData.h"
#include "Camera.h"
#include "CameraEnumerate.h"
#include "CameraInfo.h"
#include <windows.h>

using namespace std;
using namespace BaseCamera;
using namespace	FactoryCameras;

vector<GrabData*> grabDatas;
void UserGrabedDataEvent(GrabData* grabData);
unsigned short*** SaveToArray(vector<GrabData*> grabDataList);
float*** CalcReflectivity(unsigned short*** datas, int leng1, int leng2, int leng3);
void Example();

int main()
{
	// 设置控制台输出为 UTF-8
	SetConsoleOutputCP(65001);  // 65001 是 UTF-8 的代码页
	std::cout << "Hello World!\n";
	Example();
}

void Example()
{
	Camera* camera = nullptr;
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
		return;
	}
	// 订阅事件接收高光谱图像
// Subscription event to receive hyperspectral images
	Camera::CameraGrabedData = UserGrabedDataEvent;
	std::cout << "Subscribe to receive hyperspectral image events" << std::endl;

	camera = (*cameras)[0];
	try
	{
		//打开高光谱相机
		//Open the hyperspectral camera
		camera->OpenCamera(nullptr);
		std::cout << "Open the hyperspectral camera)" << std::endl;
	}
	catch (const char* exception)
	{
		std::cout << exception << std::endl;
		string num;
		std::cin >> num;
		return;
	}
	//获取版本
	string v = camera->getVersion();
	//auto other = camera->getOtherInfo();

	//设置像素格式
	//Set pixel format
	if (camera->SetPixelFormat(EnumPixelFormat::Mono14))
		std::cout << "Set pixel format to Mono14)" << std::endl;
	else
		std::cout << "Failed to set pixel format)" << std::endl;

	//设置空间合并
	//Set Space Merge
	if (camera->SetSampleBinning(EnumBinning::Binning1))
		std::cout << ("Set space merge to 1 merge)") << std::endl;
	else std::cout << ("Failed to merge space settings)") << std::endl;

	//设置光谱合并
	//Set spectral merging
	if (camera->SetBandBinning(EnumBinning::Binning1))
		std::cout << ("Set spectrum merge to 1 merge)") << std::endl;
	else std::cout << ("Failed to set spectral merge)") << std::endl;

	//设置帧频
	//Set frame rate
	if (camera->SetAcquisitionFrameRate(10))
		std::cout << ("Set the frame rate to 10fps)") << std::endl;
	else std::cout << ("Failed to set frame rate)") << std::endl;

	//设置曝光时间
	//Set exposure time
	if (camera->SetExposureTime(60))
		std::cout << ("Set the exposure time to 60us)") << std::endl;
	else std::cout << ("Failed to set exposure time)") << std::endl;

	//设置增益 0 高增益 1 中增益 2 低增益
	//Set gain  0: high gain  1: medium gain  2: low gain
	if (camera->SetGain(0))
		std::cout << ("Set the gain to high gain)") << std::endl;
	else std::cout << ("Failed to set gain)") << std::endl;

	//设置触发模式
	//Set trigger mode
	if (camera->SetTriggerMode(EnumTriggerMode::Off))
		std::cout << ("Set the trigger mode to internal trigger)") << std::endl;
	else std::cout << ("Failed to set trigger mode)") << std::endl;

	//开始采集
	if (camera->getOpen())
	{
		camera->StartGrab();
		std::cout << ("Start collecting hyperspectral data)") << std::endl;
	}
	

	while (grabDatas.size() < 10)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	//停止采集
	//Stop collecting
	camera->StopGrab();
	std::cout << ("Stop collecting hyperspectral data)") << std::endl;

	//关闭相机
	//Turn off the camera
	camera->CloseCamera();
	std::cout << ("Turn off the hyperspectral camera)") << std::endl;

	unsigned short*** datas = SaveToArray(grabDatas);
	std::cout << ("(datas[帧数][空间通道][光谱通道])(Put hyperspectral data into a three-dimensional array (data [frame rate] [spatial channel] [spectral channel]))") << std::endl;

	float*** reflectivitys = CalcReflectivity(datas, grabDatas.size(), grabDatas[0]->Samples, grabDatas[0]->Bands);
	std::cout << ("(reflectivitys[帧数][空间通道][光谱通道])(After calculating the reflectivity data, it is placed in a three-dimensional array (reflectivity [frame rate] [spatial channel] [spectral channel]))") << std::endl;

	//内存释放
	//Memory Release
	for (auto& val : grabDatas)
	{
		delete val;
	}
	std::cout << ("Memory Release)") << std::endl;

	std::cout << ("=================(Execution completed=================)") << std::endl;
	std::cin.get();  // 等待用户按回车
}

/// <summary>
/// 数据消息接收方法 (Data collection  receive message)
/// </summary>
/// <param name="grabData"></param>
void UserGrabedDataEvent(GrabData* grabData)
{
	if (grabDatas.size() < 10)
	{
		grabDatas.push_back(grabData);
		std::cout << "Received )" << grabDatas.size() << " the Hyperspectral data)" << std::endl;
	}
}

/// <summary>
/// 建立一个三维数组 HSData[frameNum][samples][bands] (Create a 3D array HSData [frameNum] [samples] [bands])
/// 把数据放进去,其中frameNum 为帧数，samples为空间通道数，bands为光谱通道数。(Put the data in, where frameNum is the number of frames, samples is the number of spatial channels, and bands is the number of spectral channels.)
/// </summary>
/// <param name="grabDataList"></param>
unsigned short*** SaveToArray(vector<GrabData*> grabDataList)
{
	//10帧高光谱数据 (10 frames of hyperspectral data)
	int frameNum = grabDataList.size();

	//建立一个三维数组 HSData[frameNum][samples][bands] 把数据放进去 (Create a 3D array HSData [frameNum] [samples] [bands] and put the data in it)
	int x; int y; int z;
	x = frameNum;
	y = grabDataList[0]->Samples;
	z = grabDataList[0]->Bands;

	unsigned short*** HSData = new unsigned short** [x];;
	for (int i = 0; i < x; ++i) {
		HSData[i] = new unsigned short* [y];
		for (int j = 0; j < y; ++j) {
			HSData[i][j] = new unsigned short[z];
		}
	}

	for (int f = 0; f < frameNum; f++)
	{
		GrabData* grabData = grabDataList[f]; //采集的数据 (Collected data)
		for (int p = 0; p < grabData->Samples; p++) //空间像素(Spatial pixels)
		{
			//光谱波长(spectral wavelength)
			for (int w = 0; w < grabData->Bands; w++)
			{
				if (grabData->PixelFormat == EnumPixelFormat::Mono8)
				{
					HSData[f][p][w] = grabData->Values[p + w * grabData->Samples];
				}
				else
				{
					HSData[f][p][w] = static_cast<unsigned short>(*(reinterpret_cast<unsigned short*>(&grabData->Values[(p + w * grabData->Samples) * 2])));
				}
			}
		}
	}
	return HSData;
}

/// <summary>
/// 计算反射率 (Calculate reflectance)
/// </summary>
float*** CalcReflectivity(unsigned short*** datas, int leng1, int leng2, int leng3)
{
	//用户选择测量区域中白板的平均信号量 注：这里假设白板的所有波长信号量为3000
	//The user selects the average signal intensity of the whiteboard in the measurement area. Note: It is assumed that the signal intensity of all wavelengths on the whiteboard is 3000
	unsigned short* white = new unsigned short[leng3];
	for (int i = 0; i < leng3; i++)
	{
		white[i] = 3000;
	}

	unsigned short* black = new unsigned short[leng3];
	for (int i = 0; i < leng3; i++)
	{
		black[i] = 100;
	}
	//标准白板反射率系数  注：该反射率系数可以在随机U盘中找到。这里假设所有光谱波段的信号量都是80%
	//Standard whiteboard reflectivity coefficient note: This reflectivity coefficient can be found on a random USB drive. Assuming that the signal intensity of all spectral bands is 80% here
	float* scale = new  float[leng3];
	for (int i = 0; i < leng3; i++)
	{
		scale[i] = 0.8f;
	}
	//高光谱反射率值 三维数组 reflectivitys[帧数][空间通道][光谱通道]
	//Hyperspectral reflectance value three-dimensional array reflectivities [frame rate] [spatial channel] [spectral channel]
	int x; int y; int z;
	x = leng1;
	y = leng2;
	z = leng3;
	float*** reflectivitys = new float** [x];;
	for (int i = 0; i < x; ++i) {
		reflectivitys[i] = new float* [y];
		for (int j = 0; j < y; ++j) {
			reflectivitys[i][j] = new float[z];
		}
	}
	//反射率校正
	//Reflectivity correction
	for (int f = 0; f < leng1; f++)//帧数 (Number of frames)
	{
		for (int p = 0; p < leng2; p++)//空间(space)
		{
			for (int w = 0; w < leng3; w++)  //光谱(spectrum)
			{
				reflectivitys[f][p][w] = datas[f][p][w] == 0 ? 0 : (datas[f][p][w] -black[w]) /(white[w] -black[w]) * scale[w];
			}
		}
	}
	return reflectivitys;
}