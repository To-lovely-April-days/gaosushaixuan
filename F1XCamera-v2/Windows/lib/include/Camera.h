#pragma once
#include "pch.h"
#include "CameraInfo.h"
#include "FigSpecInf.h"
#include "GrabData.h"
#include <iostream>
#include <chrono>
#include "KeyValueItem.h"
#include "EnumParamType.h"
#include <vector>
#include "IBaseParam.h"
#include "ExposureTime.h"
#include "AcquisitionFrameRate.h"
#include "TriggerMode.h"
#include "TriggerActivation.h"
#include "SampleBinning.h"
#include "BandBinning.h"
#include "PixelFormat.h"
#include "Samples.h"
#include "Bands.h"
#include "BandROI.h"
#include "Gain.h"
#include "ExposureMode.h"
#include "SampleDirection.h"
#include "BandDirection.h"
#include "EnumDirection.h"
#include "WaveLength.h"
namespace BaseCamera
{
	typedef void (*GrabedDataEvent)(GrabData *grabData);
	typedef void (*ConnectionLostEvent)();
	typedef void (*CameraOpenedEvent)();
	typedef void (*CameraClosedEvent)();
	typedef void (*GrabStartedEvent)();
	typedef void (*GrabStoppedEvent)();
	typedef void (*ParamChangedEvent)(vector<EnumParamType> paramType);

	class BASECAMERA_CLASS Camera
	{
	public:
		/// <summary>
		///
		/// </summary>
		/// <param name="cameraInfo"></param>
		/// <param name="inf"></param>
		Camera(FigSpecInf *inf);
		~Camera();

	public:
		CameraInfo *Info;
		FigSpecInf *Inf;
		/// <summary>
		/// 计时器
		/// </summary>
		static void restartWatch();
		static void resetWatch();
		static long getElapsedMilliseconds();

	private:
		// ��ʱ��
		static std::chrono::steady_clock::time_point begin;

	public:
		bool getOpen();

	protected:
		/// <summary>
		/// s the camera turned on
		/// </summary>
		bool IsOpen = false;
		/// <summary>
		/// Camera collection in progress
		/// </summary>
		bool IsGrab = false;
		/// <summary>
		/// 同步参数变动回调事件
		/// </summary>
		bool SyncParamChanged = true;

	public: // 相机回调事件
		//  -------------------------------------------
		/// <summary>
		/// Received image data
		/// </summary>
		static GrabedDataEvent CameraGrabedData;
		/// @brief 连接丢失事件
		static ConnectionLostEvent ConnectionLost;
		/// @brief 参数变动事件
		static ParamChangedEvent ParamChanged;
		/// <summary>
		/// The camera is turned on
		/// </summary>
		CameraOpenedEvent CameraOpened = nullptr;
		/// <summary>
		/// The camera is turned off)
		/// </summary>
		CameraClosedEvent CameraClosed = nullptr;
		/// <summary>
		/// (The camera has started capturing data)
		/// </summary>
		GrabStartedEvent GrabStarted = nullptr;
		/// <summary>
		/// (The camera has stop capturing data)
		/// </summary>
		GrabStoppedEvent GrabStopped = nullptr;
		//-------------------------------------------
	public: // 相机操作
		//  -------------------------------------------
		/// <summary>
		/// (Turn on the camera)
		/// </summary>
		virtual void OpenCamera(vector<IBaseParam *> *params);
		/// <summary>
		/// (Turn off the camera)
		/// </summary>
		virtual void CloseCamera();
		/// <summary>
		/// (Start collecting)
		/// </summary>
		virtual void StartGrab();
		/// <summary>
		/// ֹͣ�ɼ�(Stop collecting)
		/// </summary>
		virtual void StopGrab();
		virtual bool SoftwareTrigger()
		{
			return false;
		}

	public:
		/// <summary>
		/// 获取帧率
		/// </summary>
		/// <returns></returns>

		virtual AcquisitionFrameRate *GetAcquisitionFrameRate()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置帧频
		/// </summary>
		/// <param name="frameRate"></param>
		/// <returns></returns>

		virtual bool SetAcquisitionFrameRate(float frameRate)
		{
			return false;
		}
		/// <summary>
		/// 获取曝光时间
		/// </summary>
		/// <returns></returns>
		virtual ExposureTime *GetExposureTime()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置曝光时间
		/// </summary>
		/// <param name="exposureTime"></param>
		/// <returns></returns>
		virtual bool SetExposureTime(float exposureTime)
		{
			return false;
		}

		/// <summary>
		/// 获取样品合并
		/// </summary>
		/// <returns></returns>
		virtual SampleBinning *GetSampleBinning()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置样品合并
		/// </summary>
		/// <param name="binning"></param>
		/// <returns></returns>
		virtual bool SetSampleBinning(EnumBinning binning)
		{
			return false;
		}

		/// <summary>
		/// 获取光谱波段合并
		/// </summary>
		/// <returns></returns>
		virtual BandBinning *GetBandBinning()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置光谱波段合并
		/// </summary>
		/// <param name="binning"></param>
		/// <returns></returns>
		virtual bool SetBandBinning(EnumBinning binning)
		{
			return false;
		}
		/// <summary>
		/// 获取像素格式
		/// </summary>
		/// <returns></returns>
		virtual PixelFormat *GetPixelFormat()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置像素格式
		/// </summary>
		/// <param name="pixelFormat"></param>
		/// <returns></returns>
		virtual bool SetPixelFormat(EnumPixelFormat pixelFormat)
		{
			return false;
		}

		/// <summary>
		/// 获取触发模式
		/// </summary>
		/// <returns></returns>
		virtual TriggerMode *GetTriggerMode()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置触发模式
		/// </summary>
		/// <param name="triggerMode"></param>
		/// <returns></returns>
		virtual bool SetTriggerMode(EnumTriggerMode triggerMode)
		{
			return false;
		}

		/// <summary>
		/// 获取触发源
		/// </summary>
		/// <returns></returns>
		virtual TriggerActivation *GetTriggerActivation()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置触发源
		/// </summary>
		/// <param name="triggerActivation"></param>
		/// <returns></returns>
		virtual bool SetTriggerActivation(EnumTriggerActivation triggerActivation)
		{
			return false;
		}

		/// <summary>
		/// 获取样品数量
		/// </summary>
		/// <returns></returns>
		virtual Samples *GetSamples()
		{
			return nullptr;
		}

		/// <summary>
		/// 获取光谱波段数量
		/// </summary>
		/// <returns></returns>
		virtual Bands *GetBands()
		{
			return nullptr;
		}

		/// <summary>
		/// 获取感兴趣区域（ROI）
		/// </summary>
		/// <returns></returns>
		virtual BandROI *GetBandROI()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置ROI
		/// </summary>
		/// <param name="bandZone"></param>
		/// <returns></returns>
		virtual bool SetBandROI(BandZone *bandZone)
		{
			return false;
		}

		/// <summary>
		/// 获取增益
		/// </summary>
		/// <returns></returns>
		virtual Gain *GetGain()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置增益
		/// </summary>
		/// <param name="gain"></param>
		/// <returns></returns>
		virtual bool SetGain(float gain)
		{
			return false;
		}

		/// <summary>
		/// 读取曝光模式
		/// </summary>
		/// <returns></returns>
		virtual ExposureMode *GetExposureMode()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置曝光模式
		/// </summary>
		/// <returns></returns>
		virtual bool SetExposureMode(EnumExposureMode exposureMode)
		{
			return false;
		}

		/// <summary>
		/// 获取样品方向
		/// </summary>
		/// <returns></returns>
		virtual SampleDirection *GetSampleDirection()
		{
			return nullptr;
		}

		/// <summary>
		/// 设置样品方向
		/// </summary>
		/// <returns></returns>
		virtual bool SetSampleDirection(EnumDirection direction)
		{
			return false;
		}

		/// <summary>
		/// 获取光谱波段方向
		/// </summary>
		/// <returns></returns>
		virtual BandDirection *GetBandDirection()
		{
			return nullptr;
		}

		///< summary>
		/// 设置光谱波段方向
		/// </summary>
		virtual bool SetBandDirection(EnumDirection direction)
		{
			return false;
		}

		vector<int> *FixedWaveIndexs;

		// 获取光谱波长
		virtual Wavelength *GetWavelength();


		// 获取版本
		virtual string getVersion() 
		{
			return "1.0.0";
		}
		//获取其他信息
		virtual vector<int> getOtherInfo()
		{
			return vector<int>();
		}

	};
}
