#pragma once
#include "pch.h"
#include "ICameraInfo.h"

namespace BaseCamera
{
	class Enumerate
	{
	public:
		virtual std::vector<ICameraInfo*>* GetCameras(int com =-1) {
			return  NULL;;
		}
	};

}