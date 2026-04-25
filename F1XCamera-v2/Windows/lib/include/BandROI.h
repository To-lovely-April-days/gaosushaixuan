#pragma once
#include "IBaseParam.h"
#include "EnumParamType.h"
#include <vector>

namespace BaseCamera
{
    // Integer 类：模拟 C# 的 Integer 类
    class Integer
    {
    private:
        int Max;
        int Min;
        int Inc;
        int Value;

    public:
        // 带参构造函数
        Integer(int max, int min, int inc, int value)
            : Max(max), Min(min), Inc(inc), Value(value) {}

        // 默认构造函数
        Integer() : Max(0), Min(0), Inc(0), Value(0) {}

        // 获取步进值
        int getInc() const { return Inc; }
        void setInc(int value) { Inc = value; }

        // 获取最大值
        int getMax() const { return Max; }
        void setMax(int value) { Max = value; }

        // 获取最小值
        int getMin() const { return Min; }
        void setMin(int value) { Min = value; }

        // 获取值
        int getValue() const { return Value; }
        void setValue(int value) { this->Value = value; }
    };

    // BandZone 类：模拟 C# 的 BandZone 类
    class BandZone
    {
    private:
        int Index;
        bool Enable;
        Integer *Offset;
        Integer *Size;

    public:
        // 带参构造函数
        BandZone(int index, bool enable, Integer *offset, Integer *size)
            : Index(index), Enable(enable), Offset(offset), Size(size) {}

        // 默认构造函数
        BandZone() : Index(0), Enable(false), Offset(new Integer()), Size(new Integer()) {}

        // 获取索引
        int getIndex() const { return Index; }
        void setIndex(int value) { Index = value; }

        // 获取启用状态
        bool getEnable() const { return Enable; }
        void setEnable(bool value) { Enable = value; }

        // 获取 Offset
        Integer *getOffset() const { return Offset; }
        void setOffset(Integer *value) { Offset = value; }

        // 获取 Size
        Integer *getSize() const { return Size; }
        void setSize(Integer *value) { Size = value; }
    };

    // BandROI 类：模拟 C# 的 BandROI 类
    class BandROI : public IBaseParam
    {
    private:
        std::vector<BandZone *> *BandZones;

    public:
        // 带参构造函数
        BandROI(std::vector<BandZone *> *bandZones)
            : BandZones(bandZones) {}

        // 默认构造函数
        BandROI() {}

        // 获取 ParamType
        EnumParamType getParamType() const override
        {
            return EnumParamType::BandROI;
        }

        // 获取 BandZones
        std::vector<BandZone *> *getBandZones() { return BandZones; }
        void setBandZones(std::vector<BandZone *> *zones) { BandZones = zones; }
    };

}