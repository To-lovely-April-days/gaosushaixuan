#pragma once
#include "pch.h"
#include "EnumSensorType.h"

namespace BaseCamera
{
     class CameraInfo
     {
     public:
          /// <summary>
          /// 设备编码
          /// </summary>
          int DeviceCode;

          /// <summary>
          /// 型号
          /// </summary>
          string DisplayModel;

          /// <summary>
          /// 仪器序列号
          /// </summary>
          string InstrumentSN;

          /// <summary>
          /// 传感器序列号
          /// </summary>
          string SensorSN;

          EnumSensorType SensorType;
     };
}