#pragma once
#include "IBaseParam.h"
#include "IUInteger.h"

namespace BaseCamera
{
// Bands 类：模拟 C# 的 Bands 类
class Bands : public IUInteger {
    private:
        uint32_t Inc;
        uint32_t Max;
        uint32_t Min;
        uint32_t Value;
    
    public:
        // 带参构造函数
        Bands(uint32_t inc, uint32_t max, uint32_t min, uint32_t value)
            : Inc(inc), Max(max), Min(min), Value(value) {}
    
        // 默认构造函数
        Bands() : Inc(0), Max(0), Min(0), Value(0) {}
    
        // 获取 ParamType 值
        EnumParamType getParamType() const {
            return EnumParamType::Bands;
        }
    
        // 获取 Inc 值
        uint32_t getInc() const override {
            return Inc;
        }
    
        // 设置 Inc 值
        void setInc(uint32_t value) {
            Inc = value;
        }
    
        // 获取 Max 值
        uint32_t getMax() const override {
            return Max;
        }
    
        // 设置 Max 值
        void setMax(uint32_t value) {
            Max = value;
        }
    
        // 获取 Min 值
        uint32_t getMin() const override {
            return Min;
        }
    
        // 设置 Min 值
        void setMin(uint32_t value) {
            Min = value;
        }
    
        // 获取 Value 值
        uint32_t getValue() const override {
            return Value;
        }
    
        // 设置 Value 值
        void setValue(uint32_t value) {
            this->Value = value;
        }
    };
}