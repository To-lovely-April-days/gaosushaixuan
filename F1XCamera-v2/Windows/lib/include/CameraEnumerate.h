#pragma once
#include "pch.h"
#include <vector>
#include "Camera.h"
#include "CameraInfo.h"
#include "FigSpecInf.h"

using namespace BaseCamera;




namespace FactoryCameras
{
	class BASECAMERA_FACTORY_DLL CameraEnumerate
	{
	public:
		CameraEnumerate();
		~CameraEnumerate();
		// 对接中间程序
		static vector<Camera *> *GetCameras(vector<FigSpecInf *> *infs, int com = -1);
		// 升级后的接口
		// static vector<Camera *> *GetCameras(vector<string> infsPath, int com = -1);
	};
}
