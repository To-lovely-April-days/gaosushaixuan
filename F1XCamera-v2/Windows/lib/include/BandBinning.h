#pragma once
#include "IBaseParam.h"
#include "IEnumeration.h"
#include "EnumParamType.h"
#include <vector>
#include "EnumBinning.h"
namespace BaseCamera
{
    // BandBinning 类实现 IEnumeration<EnumBinning>
    class BandBinning : public IEnumeration<EnumBinning>
    {
    public:
        // 构造函数
        BandBinning(std::vector<EnumBinning> *items, EnumBinning value)
            : IEnumeration<EnumBinning>(items, value) {}

        // 默认构造函数
        BandBinning() : IEnumeration<EnumBinning>(new std::vector<EnumBinning>(), EnumBinning()) {}

        // 获取 ParamType
        EnumParamType getParamType() const
        {
            return EnumParamType::BandBinning;
        }

        // 获取 Items

        // 设置 Items
        void setItems(std::vector<EnumBinning> *newItems)
        {
            items_ = newItems;
        }

        // 设置 Value
        void setValue(EnumBinning newValue)
        {
            value_ = newValue;
        }
    };
}