#pragma once
#include "pch.h"
#include "EnumSensorType.h"
#include "EnumPixelFormat.h"
namespace BaseCamera
{
	/// <summary>
	/// 一帧数据（One frame of data）
	/// </summary>
	class BASECAMERA_CLASS GrabData
	{
	public:
		GrabData(int _Samples,int _Bands, EnumPixelFormat _PixelFormat, long _Milliseconds);
		~GrabData();

		/// <summary>
		/// 空间通道数（Number of spatial channels）
		/// </summary>
		int Samples;

		/// <summary>
		/// 光谱通道数（Number of spectral channels）
		/// </summary>
		int Bands;

		/// <summary>
		/// 高光谱图像数据（Hyperspectral image data）
		/// </summary>
		char* Values = nullptr;

		/// <summary>
		/// 当前高光谱图像的数据格式（The current data format of hyperspectral images）
		/// </summary>
		EnumPixelFormat PixelFormat;

		/// <summary>
		/// 毫秒（millisecond）
		/// </summary>
		long Milliseconds;
	public:
		// 获取Values的字节个数
		int GetMemSize();
	};
}