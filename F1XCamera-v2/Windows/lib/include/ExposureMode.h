#pragma once
#include "IBaseParam.h"
#include "IEnumeration.h"
#include "EnumExposureMode.h"
#include "EnumSensorType.h"
#include <vector>
namespace BaseCamera
{
    // ExposureMode 类，继承自 IEnumeration<EnumExposureMode>
    class ExposureMode : public IEnumeration<EnumExposureMode>
    {
    public:
        // 带参数的构造函数
        ExposureMode(std::vector<EnumExposureMode> *items, EnumExposureMode value)
            : IEnumeration<EnumExposureMode>(items, value) {}

        // 默认构造函数
        ExposureMode() : IEnumeration<EnumExposureMode>(new std::vector<EnumExposureMode>(), EnumExposureMode::Off) {}

        // 重写获取 ParamType 函数
        EnumParamType getParamType() const override
        {
            return EnumParamType::ExposureMode;
        }

        // Getter 和 Setter 方法
        void setItems(std::vector<EnumExposureMode> *newItems) { items_ = newItems; }

        void setValue(EnumExposureMode newValue) { value_ = newValue; }
    };
}