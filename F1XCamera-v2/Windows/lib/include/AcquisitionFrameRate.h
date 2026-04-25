#pragma once
#include "pch.h"
#include "IBaseParam.h"
#include "IFloat.h"
namespace BaseCamera
{
    /// <summary>
    /// 采集帧率
    /// </summary>
    class AcquisitionFrameRate : IFloat
    {
    private:
        float Min;

        float Max;

        float Value;

    public:
        AcquisitionFrameRate(float max, float min, float value) : Min(min), Max(max), Value(value)
        {
        }
        AcquisitionFrameRate() {}
        // 纯虚函数，获取参数类型
        EnumParamType getParamType() const override
        {
            return EnumParamType::AcquisitionFrameRate;
        }

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
