#pragma once
#include "IBaseParam.h"
#include "EnumDirection.h"
#include <vector>
#include "IEnumeration.h"
#include "EnumParamType.h"

namespace BaseCamera
{
    // SampleDirection 类，继承自 IEnumeration<EnumDirection>
    class SampleDirection : public IEnumeration<EnumDirection>
    {
    public:
        SampleDirection(std::vector<EnumDirection> *items, EnumDirection value)
            : IEnumeration<EnumDirection>(items, value)
        {
        }

        EnumParamType getParamType() const override
        {
            return EnumParamType::SampleDirection;
        }

        void setItems(std::vector<EnumDirection> *newItems)
        {
            items_ = newItems;
        }
        void setValue(EnumDirection newValue)
        {
            value_ = newValue;
        }
    };
}