// Example2.cpp :
// 功能介绍(Function introduce )
// 1	链接相机(Connect camera)
// 2	相机设置(Setting camera)
// 3	数据采集(Grab camera data)
// 4	数据解析(Data Parser)
// 5	反射率校准(Calibration)
// 6	保存高光谱数据到Hdr文件(Save data to file)
// 7	关闭相机(Close camera)

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#include <cstring> // 添加头文件
#include <cerrno>  // 访问 errno 宏
#endif

#include "GrabData.h"
#include "Camera.h"
#include "CameraEnumerate.h"
#include "CameraInfo.h"

using namespace std;
using namespace BaseCamera;
using namespace FactoryCameras;

vector<GrabData*> grabDatas;

void UserGrabedDataEvent(GrabData* grabData);
unsigned short*** SaveToArray(vector<GrabData*> grabDataList);
float*** CalcReflectivity(unsigned short*** datas, int leng1, int leng2, int leng3);
string SaveToFile(Camera* camera, vector<GrabData*> grabDataList);
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
	Camera* camera = NULL;
	vector<Camera*>* cameras = nullptr;
	try
	{
		// 通过调用GetCameras方法，您可以获取该电脑已连接的相机列表
		// By calling the GetCameras method, you can obtain a list of cameras connected to the computer
		cameras = CameraEnumerate::GetCameras(nullptr);
		std::cout << "(find)" << cameras->size() << " 台相机(camera)" << std::endl;
		if (cameras->size() == 0)
		{
			std::cout << ("================(Execution completed=================)") << std::endl;
			string num;
			std::cin >> num;
		}
		for (int i = 0; i < cameras->size(); i++)
		{
			std::cout << "Number" << (i + 1) << "The camera serial number is: " << (*cameras)[i]->Info->InstrumentSN << std::endl;
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
		// 打开高光谱相机
		// Open the hyperspectral camera
		camera->OpenCamera(nullptr);
		std::cout << "Open the hyperspectral camera" << std::endl;
	}
	catch (const char* exception)
	{
		std::cout << exception << std::endl;
		string num;
		std::cin >> num;
		return;
	}

	// 设置像素格式
	// Set pixel format
	if (camera->SetPixelFormat(EnumPixelFormat::Mono14))
		std::cout << "Set pixel format to Mono14)" << std::endl;
	else
		std::cout << "Failed to set pixel format)" << std::endl;

	// 设置空间合并(Set Space Merge)
	if (camera->SetSampleBinning(EnumBinning::Binning1))
		std::cout << ("Set space merge to 1 merge)") << std::endl;
	else
		std::cout << ("Failed to merge space settings)") << std::endl;

	// 设置光谱合并(Set spectral merging)
	if (camera->SetBandBinning(EnumBinning::Binning1))
		std::cout << ("Set spectrum merge to 1 merge)") << std::endl;
	else
		std::cout << ("Failed to set spectral merge)") << std::endl;

	// 设置帧频(Set frame rate)
	if (camera->SetAcquisitionFrameRate(10))
		std::cout << ("Set the frame rate to 10fps)") << std::endl;
	else
		std::cout << ("Failed to set frame rate)") << std::endl;

	// 设置曝光时间(Set exposure time)
	if (camera->SetExposureTime(60))
		std::cout << ("Set the exposure time to 60us)") << std::endl;
	else
		std::cout << ("Failed to set exposure time)") << std::endl;

	// 设置增益 0 高增益 1 中增益 2 低增益(Set gain  0: high gain  1: medium gain  2: low gain)
	if (camera->SetGain(0))
		std::cout << ("Set the gain to high gain)") << std::endl;
	else
		std::cout << ("Failed to set gain)") << std::endl;

	// 设置触发模式(Set trigger mode)
	if (camera->SetTriggerMode(EnumTriggerMode::Off))
		std::cout << ("Set the trigger mode to internal trigger)") << std::endl;
	else
		std::cout << ("Failed to set trigger mode)") << std::endl;

	// 开始采集
	// Start collecting
	if (camera->getOpen())
	{
		camera->StartGrab();
		std::cout << ("Start collecting hyperspectral data)") << std::endl;
	}

	while (grabDatas.size() < 10)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	// 停止采集(Stop collecting)
	camera->StopGrab();
	std::cout << ("Stop collecting hyperspectral data)") << std::endl;

	unsigned short*** datas = SaveToArray(grabDatas);
	std::cout << ("(datas[帧数][空间通道][光谱通道])(Put hyperspectral data into a three-dimensional array (data [frame rate] [spatial channel] [spectral channel]))") << std::endl;

	float*** reflectivitys = CalcReflectivity(datas, grabDatas.size(), grabDatas[0]->Samples, grabDatas[0]->Bands);
	std::cout << ("(reflectivitys[帧数][空间通道][光谱通道])(After calculating the reflectivity data, it is placed in a three-dimensional array (reflectivity [frame rate] [spatial channel] [spectral channel]))") << std::endl;

	string directory = SaveToFile(camera, grabDatas);
	std::cout << "(Save hyperspectral data to directory:)" << directory << std::endl;

	// 关闭相机(Stop collecting)
	camera->CloseCamera();
	std::cout << ("Turn off the hyperspectral camera)") << std::endl;
	// 内存释放
	// Memory Release
	for (auto& val : grabDatas)
	{
		delete val;
	}
	std::cout << ("(Memory Release)") << std::endl;

	std::cout << ("=================(Execution completed=================)") << std::endl;
	string num;
	std::cin >> num;
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
		std::cout << "Received the" << grabDatas.size() << " Hyperspectral data" << std::endl;
	}
}

/// <summary>
/// 建立一个三维数组 HSData[frameNum][samples][bands](Create a 3D array HSData [frameNum] [samples] [bands])
/// 把数据放进去,其中frameNum 为帧数，samples为空间通道数，bands为光谱通道数。(Put the data in, where frameNum is the number of frames, samples is the number of spatial channels, and bands is the number of spectral channels.)
/// </summary>
/// <param name="grabDataList"></param>
unsigned short*** SaveToArray(vector<GrabData*> grabDataList)
{
	// 10帧高光谱数据(10 frames of hyperspectral data)
	int frameNum = grabDataList.size();

	// 建立一个三维数组 HSData[frameNum][samples][bands] 把数据放进去(Create a 3D array HSData [frameNum] [samples] [bands] and put the data in it)
	int x;
	int y;
	int z;
	x = frameNum;
	y = grabDataList[0]->Samples;
	z = grabDataList[0]->Bands;

	unsigned short*** HSData = new unsigned short** [x];
	;
	for (int i = 0; i < x; ++i)
	{
		HSData[i] = new unsigned short* [y];
		for (int j = 0; j < y; ++j)
		{
			HSData[i][j] = new unsigned short[z];
		}
	}

	for (int f = 0; f < frameNum; f++)
	{
		GrabData* grabData = grabDataList[f];		// 采集的数据 (Collected data)
		for (int p = 0; p < grabData->Samples; p++) // 空间像素 (Spatial pixels)
		{
			// HSData[f][p] = new ushort[grabData.Bands];
			// 光谱波长 //spectral wavelength
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
	// 用户选择测量区域中白板的平均信号量 注：这里假设白板的所有波长信号量为3000
	// The user selects the average signal intensity of the whiteboard in the measurement area. Note: It is assumed that the signal intensity of all wavelengths on the whiteboard is 3000
	unsigned short* standard = new unsigned short[leng3];
	for (size_t i = 0; i < leng3; i++)
	{
		standard[i] = 3000;
	}
	// 标准白板反射率系数  注：该反射率系数可以在随机U盘中找到。这里假设所有光谱波段的信号量都是80%
	// Standard whiteboard reflectivity coefficient note: This reflectivity coefficient can be found on a random USB drive. Assuming that the signal intensity of all spectral bands is 80% here
	float* scale = new float[leng3];
	for (size_t i = 0; i < leng3; i++)
	{
		scale[i] = 0.8f;
	}
	// 高光谱反射率值 三维数组 reflectivitys[帧数][空间通道][光谱通道]
	// Hyperspectral reflectance value three-dimensional array reflectivities [frame rate] [spatial channel] [spectral channel]
	int x;
	int y;
	int z;
	x = leng1;
	y = leng2;
	z = leng3;
	float*** reflectivitys = new float** [x];
	;
	for (int i = 0; i < x; ++i)
	{
		reflectivitys[i] = new float* [y];
		for (int j = 0; j < y; ++j)
		{
			reflectivitys[i][j] = new float[z];
		}
	}
	// 反射率校正
	// Reflectivity correction
	for (int f = 0; f < leng1; f++) // 帧数(Number of frames)
	{
		for (int p = 0; p < leng2; p++) // 空间(space)
		{
			for (int w = 0; w < leng3; w++) // 光谱(spectrum)
			{
				reflectivitys[f][p][w] = datas[f][p][w] == 0 ? 0 : datas[f][p][w] * standard[w] / scale[w];
			}
		}
	}
	return reflectivitys;
}
#if defined(_WIN32)
/// <summary>
/// 获取程序运行路径(Get the program running path)
/// </summary>
/// <returns></returns>
std::string GetProgramPath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, sizeof(path));
	return std::string(path);
}
std::string extractPath(const std::string& filePath)
{
	size_t found = filePath.find_last_of("\\/");
	if (found != std::string::npos)
	{
		return filePath.substr(0, found + 1);
	}
	return "";
}
#else
std::string extractPath()
{
	char szSelfName[PATH_MAX] = { 0 };
	ssize_t len = readlink("/proc/self/exe", szSelfName, PATH_MAX);
	if (len == -1)
	{
		// 步骤 1：获取错误码
		int err = errno;

		// 步骤 2：根据错误类型处理
		switch (err)
		{
		case EACCES:
			throw std::runtime_error("权限不足：无法访问 /proc/self/exe");
		case EINVAL:
			throw std::runtime_error("路径非符号链接");
		case ENOENT:
			throw std::runtime_error("/proc/self/exe 不存在");
		case EFAULT:
			throw std::runtime_error("缓冲区地址无效");
		case EINTR:
			// 信号中断，可重试
			len = readlink("/proc/self/exe", szSelfName, PATH_MAX);
			if (len == -1)
				throw std::runtime_error("重试后仍失败");
			break;
		default:
			throw std::runtime_error(std::string("未知错误: ") + std::strerror(err));
		}
	}
	szSelfName[len] = '\0';
	string fullPath(szSelfName);
	size_t pos = fullPath.find_last_of("/");
	fullPath = fullPath.substr(0, pos);
	return fullPath;
}

#endif
/// <summary>
/// 将高光谱并保存成spe格式高光谱文件(Save hyperspectral data in SPE format as a hyperspectral file)
/// </summary>
string SaveToFile(Camera* camera, vector<GrabData*> grabDataList)
{
#if defined(_WIN32)
	string directory = extractPath(GetProgramPath());
	string path = directory + "\\figspec";
#else
	string directory = extractPath();
	string path = directory + "/figspec";
#endif
	string hdr_path = path + ".hdr";
	string spe_path = path + ".spe";

	std::ofstream fsWrite(spe_path, std::ios::trunc | std::ios::binary);
	for (auto grabData : grabDataList)
	{
		if (grabData->PixelFormat == EnumPixelFormat::Mono8)
		{
			fsWrite.write(grabData->Values, grabData->Bands * grabData->Samples);
		}
		else
		{
			fsWrite.write(grabData->Values, grabData->Bands * grabData->Samples * 2);
		}
	}

	std::ofstream sw(hdr_path, std::ios::trunc);
	std::stringstream stringBuilder;
	stringBuilder << "ENVI" << std::endl;
	stringBuilder << "description = {File Imported into ENVI.}" << std::endl;
	stringBuilder << "samples = " << camera->GetSamples()->getValue() << std::endl;
	stringBuilder << "lines = " << grabDataList.size() << std::endl;
	stringBuilder << "bands = " << camera->GetBands()->getValue() << std::endl;
	stringBuilder << "header offset = " << 0 << std::endl;
	stringBuilder << "file type = ENVI Standard" << std::endl;
	stringBuilder << "data type = " << (camera->GetPixelFormat()->getValue() == EnumPixelFormat::Mono8 ? 1 : 12) << std::endl;
	stringBuilder << "interleave = bil" << std::endl;
	stringBuilder << "sensor type = Unknown" << std::endl;
	stringBuilder << "byte order = " << 0 << std::endl;
	stringBuilder << "x start = " << 0 << std::endl;
	stringBuilder << "y start = " << 0 << std::endl;
	stringBuilder << "default bands = {86,66,22}" << std::endl;
	stringBuilder << "wavelength units = Nanometers" << std::endl;
	stringBuilder << "wavelength = {";
	auto wavel = *camera->GetWavelength()->getValue();
	for (int i = 0; i < wavel.size(); ++i)
	{
		if (i + 1 < wavel.size())
		{
			stringBuilder << wavel[i] << ", ";
		}
		else
		{
			stringBuilder << wavel[i];
		}
		std::cout << wavel[i] << ", ";
	}
	stringBuilder << '}' << std::endl;
	stringBuilder << "fx model = " << camera->Info->DisplayModel << std::endl;
	stringBuilder << "sn = " << camera->Info->InstrumentSN << std::endl;
	stringBuilder << "exposure time =" << camera->GetExposureTime()->getValue() << std::endl;
	sw << stringBuilder.str();
	return directory;
}

