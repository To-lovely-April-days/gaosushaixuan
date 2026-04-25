#pragma once
#include "IBaseParam.h"
namespace BaseCamera
{
    class IFloat : public IBaseParam
    {
    public:
        /// <summary>
        /// 最小值
        /// </summary>
        virtual float getMin() const = 0;
        /// <summary>
        /// 最大值
        /// </summary>
        virtual float getMax() const = 0;
        /// <summary>
        /// 值内容
        /// </summary>
        virtual float getValue() const = 0;
    };
}
