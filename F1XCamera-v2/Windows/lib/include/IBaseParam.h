#pragma once
#include "EnumParamType.h"
namespace BaseCamera
{
    // IBaseParam 接口类
    class IBaseParam
    {
    public:
        // 纯虚函数，获取参数类型
        virtual EnumParamType getParamType() const = 0;
    };
}
