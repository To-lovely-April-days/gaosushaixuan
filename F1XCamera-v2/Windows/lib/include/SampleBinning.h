#pragma once
#include "IBaseParam.h"
#include "IEnumeration.h"
#include "EnumParamType.h"
#include <vector>
#include "EnumBinning.h"

namespace BaseCamera
{
    // SampleBinning 类实现 IEnumeration<EnumBinning>
    class SampleBinning : public IEnumeration<EnumBinning>
    {
    public:
        SampleBinning(std::vector<EnumBinning> *items, EnumBinning value)
            : IEnumeration<EnumBinning>(items, value)
        {
        }

        // 默认构造函数
        SampleBinning() : IEnumeration<EnumBinning>(new std::vector<EnumBinning>(), EnumBinning()) {}

        EnumParamType getParamType() const override
        {
            return EnumParamType::SampleBinning;
        }
        void setItems(std::vector<EnumBinning> *newItems)
        {
            items_ = newItems;
        }
        void setValue(EnumBinning newValue)
        {
            value_ = newValue;
        }
    };
}