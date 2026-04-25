#pragma once
#include "pch.h"
#include "IBaseParam.h"
#include "IEnumeration.h"
#include "EnumParamType.h"
#include <vector>
#include "EnumTriggerMode.h"
namespace BaseCamera
{
    // TriggerMode 类实现 IEnumeration<EnumTriggerMode>
    class TriggerMode : public IEnumeration<EnumTriggerMode>
    {
    public:
        TriggerMode(std::vector<EnumTriggerMode> *items, EnumTriggerMode value)
            : IEnumeration<EnumTriggerMode>(items, value)
        {
        }

        EnumParamType getParamType() const override
        {
            return EnumParamType::TriggerMode;
        }

        void setItems(std::vector<EnumTriggerMode> *newItems)
        {
            items_ = newItems;
        }

        void setValue(EnumTriggerMode newValue)
        {
            value_ = newValue;
        }
    };
}