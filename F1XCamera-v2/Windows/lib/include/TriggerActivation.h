#pragma once
#include "IBaseParam.h"
#include "IEnumeration.h"
#include "EnumParamType.h"
#include <vector>
#include "EnumTriggerActivation.h"

namespace BaseCamera
{
    // TriggerActivation 类实现 IEnumeration<EnumTriggerActivation>
    class TriggerActivation : public IEnumeration<EnumTriggerActivation>
    {
    public:
        TriggerActivation(std::vector<EnumTriggerActivation> *items, EnumTriggerActivation value)
            : IEnumeration<EnumTriggerActivation>(items, value)
        {
        }

        EnumParamType getParamType() const override
        {
            return EnumParamType::TriggerActivation;
        }

        void setItems(std::vector<EnumTriggerActivation> *newItems)
        {
            items_ = newItems;
        }
        void setValue(EnumTriggerActivation newValue)
        {
            value_ = newValue;
        }
    };
} // namespace name