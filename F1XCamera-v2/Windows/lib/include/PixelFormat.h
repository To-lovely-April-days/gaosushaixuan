#pragma once
#include "IBaseParam.h"
#include "IEnumeration.h"
#include "EnumParamType.h"
#include <vector>
#include "EnumPixelFormat.h"
namespace BaseCamera
{
    // PixelFormat 类实现 IEnumeration<EnumPixelFormat>
    class PixelFormat : public IEnumeration<EnumPixelFormat>
    {
    public:
        // 构造函数
        PixelFormat(std::vector<EnumPixelFormat> *items, EnumPixelFormat value)
            : IEnumeration<EnumPixelFormat>(items, value) {}

        // 获取 ParamType
        EnumParamType getParamType() const override
        {
            return EnumParamType::PixelFormat;
        }

        // 设置 Value
        void setItem(std::vector<EnumPixelFormat> *newItem)
        {
            items_ = newItem;
        }
        // 设置 Value
        void setValue(EnumPixelFormat newValue)
        {
            value_ = newValue;
        }
    };
}