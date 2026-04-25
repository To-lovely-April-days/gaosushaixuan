#pragma once
#include "pch.h"
#include "IFloat.h"
#include "EnumParamType.h"
namespace BaseCamera
{
    /// <summary>
    /// 曝光时间
    /// </summary>
    class ExposureTime : IFloat
    {
    private:
        float Min;

        float Max;

        float Value;

    public:
        ExposureTime(float max, float min, float value):Min(min), Max(max), Value(value)
        {
        }
        ExposureTime() {}
        EnumParamType getParamType() const override
        {
            return EnumParamType::ExposureTime;
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
