#pragma once
#include "IBaseParam.h"
#include "EnumDirection.h"
#include <vector>
#include "EnumParamType.h"
#include "IEnumeration.h"

namespace BaseCamera
{

    // BandDirection 类，继承自 IEnumeration<EnumDirection>
    class BandDirection : public IEnumeration<EnumDirection>
    {
    public:
        // 带参数的构造函数
        BandDirection(std::vector<EnumDirection> *items, EnumDirection value)
            : IEnumeration<EnumDirection>(items, value) {}

        // 重写获取 ParamType 函数
        EnumParamType getParamType() const override
        {
            return EnumParamType::BandDirection;
        }

        // Getter 和 Setter 方法
        void setItems(std::vector<EnumDirection> *newItems) { items_ = newItems; }

        void setValue(EnumDirection newValue) { value_ = newValue; }
    };

}