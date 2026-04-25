#pragma once
#include "IBaseParam.h"
namespace BaseCamera
{
    class IUInteger : IBaseParam
    {
    public:
        // 虚析构函数
        virtual ~IUInteger() = default;

        // 获取步进值
        virtual uint32_t getInc() const = 0;

        // 获取最大值
        virtual uint32_t getMax() const = 0;

        // 获取最小值
        virtual uint32_t getMin() const = 0;

        // 获取当前值
        virtual uint32_t getValue() const = 0;
    };
}
