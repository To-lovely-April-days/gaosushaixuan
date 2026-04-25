#pragma once
#include <string>
#include "EnumSensorType.h"
namespace BaseCamera
{
    // ICameraInfo 接口类
    class ICameraInfo
    {
    public:
        virtual ~ICameraInfo() = default;
        // 传感器类型
        virtual EnumSensorType getSensorType() const = 0;
        virtual void setSensorType(EnumSensorType sensorType) = 0;
        // 传感器序列号
        virtual std::string getSensorSN() const = 0;
        virtual void setSensorSN(const std::string sensorSN)  = 0;

    };
}
