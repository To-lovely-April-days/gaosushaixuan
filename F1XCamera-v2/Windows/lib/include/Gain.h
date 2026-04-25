#pragma once
#include "pch.h"
#include "IBaseParam.h"
#include "EnumParamType.h"
#include <vector>
#include "IFloat.h"

namespace BaseCamera
{
    // Gain 类实现
    class Gain : public IFloat
    {
    private:
        float Min;

        float Max;

        float Value;

    public:
        // 带参数的构造函数
        Gain(float max, float min, float value) : Min(min), Max(max), Value(value)
        {
        }

        // 默认构造函数
        Gain() {}

        // 重写获取 ParamType 函数
        EnumParamType getParamType() const override
        {
            return EnumParamType::Gain;
        }

    public:
        /// <summary>
        /// 最小值
        /// </summary>
        float getMin() const override
        {
            return Min;
        }
        /// <summary>
        /// 最大值
        /// </summary>
        float getMax() const override
        {
            return Max;
        }
        /// <summary>
        /// 值内容
        /// </summary>
        float getValue() const override
        {
            return Value;
        }
        /// <summary>
        /// 最小值
        /// </summary>
        void setMin(float value)
        {
            Min = value;
        }
        /// <summary>
        /// 最大值
        /// </summary>
        void setMax(float value)
        {
            Max = value;
        }
        /// <summary>
        /// 值内容
        /// </summary>
        void setValue(float value)
        {
            Value = value;
        }
    };
}