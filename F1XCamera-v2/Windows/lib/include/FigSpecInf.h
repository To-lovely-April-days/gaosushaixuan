#pragma once
#include "pch.h"
#include "EnumSensorType.h"
namespace BaseCamera
{
	// <summary>
	/// Hyperspectral configuration information
	/// </summary>
	class BASECAMERA_CLASS FigSpecInf
	{
	public:
		/// <summary>
		/// 设备编码(Equipment code)
		/// </summary>
		int DeviceCode;

		/// <summary>
		/// 显示型号(Display model)
		/// </summary>
		string DisplayModel;

		/// <summary>
		/// 内部序列号(Internal serial number)
		/// </summary>
		string InternalSN;

		/// <summary>
		/// 仪器序列号(Instrument serial number)
		/// </summary>
		string InstrumentSN;

		/// <summary>
		/// 传感器类型
		/// </summary>
		EnumSensorType SensorType;
		/// <summary>
		/// 传感器序列号(Sensor serial number)
		/// </summary>
		string SensorSN;

		/// <summary>
		/// 拟合的系数(Fitting coefficient)
		/// </summary>
		std::vector<float> Coefficients;

		/// <summary>
		/// 拟合时候光谱维度合并个数(Number of spectral dimensions merged during fitting)
		/// </summary>
		char Binning;

		/// <summary>
		/// 光谱维度长度  ex：300 (Spectral dimension length)
		/// </summary>
		unsigned int Bands;

	public: // 波长范围限制
		/// <summary>
		/// 波段起始序号 （Band starting number）
		/// </summary>
		int bandStart;

		int getBandStart();

		void setBandStart(int value);

		/// <summary>
		/// 波段可用数量（Number of available bands）
		/// </summary>
		int bandNumber;

		int getBandNumber();

		void setBandNumber(int value);

		// 获取波段开始波段
		int GetBandStart(int binning = 1) const;

		// 获取波段可用数量
		int GetBandNumber(int binning = 1) const;

		/// <summary>
		/// 固定波长(Fixed wavelength)
		/// </summary>
		vector<float> FixedWavelengths;
		// 获取固定数量的波长
		int GetFixedCount() const;

		/// <summary>
		/// 像素大小(pixel size)
		/// </summary>
		float PixelSize = 5.86f;

		/// <summary>
		/// 标准波长(Standard wavelength)
		/// </summary>
		float Wavelengths[30];

		/// <summary>
		/// 波长通道位置（像元位置）(Wavelength channel position (pixel position))
		/// </summary>
		float Pixels[30];

		/// <summary>
		/// 软件版本号(Software version number)
		/// </summary>
		string SoftwareVersion;

		/// <summary>
		/// 硬件版本号(Hardware Version)
		/// </summary>
		string HardwareVersion;

		/// <summary>
		/// 是否样机(Is it a prototype)
		/// </summary>
		bool IsPrototype;

		/// <summary>
		/// 是否中性(Is it neutral)
		/// </summary>
		bool IsNeutral;

		/// <summary>
		/// 描述(describe)
		/// </summary>
		string Description;
		/// <summary>
		/// 水平翻转
		/// </summary>
		bool ReverseX;

		/// <summary>
		/// 垂直翻转
		/// </summary>
		bool ReverseY;

		/// <summary>
		/// 创建时间(create time)
		/// </summary>
		string CreateTime;

		/// <summary>
		/// 未知的字段(Unknown field)
		/// </summary>
		std::vector<std::string> UnknownFields;

		static FigSpecInf *FromText(string text);

		static FigSpecInf *FromFile(string fileName);

		virtual string ToString();

	private:
		static void Save(string fileName, FigSpecInf figSpecInf);
	};
}