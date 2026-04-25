#pragma once
#include <iostream>
#include <vector>
#include <type_traits> // For std::is_enum
#include "IBaseParam.h"
namespace BaseCamera
{
    // 模板类 IEnumeration 实现
    template <typename T>
    class IEnumeration : public IBaseParam
    {
    public:
        static_assert(std::is_enum<T>::value, "T must be an enum type");
        // 构造函数，传入枚举值
        IEnumeration(std::vector<T> *items, T value)
        {
            items_ = items;
            value_ = value;
        }

        // 键值列表
        virtual std::vector<T>* getItems()
        {
            return items_;
        }

        // 当前值
        virtual T getValue()
        {
            return value_;
        }

        virtual ~IEnumeration() {}

    protected:
        std::vector<T>* items_; // 存储枚举项
        T value_;              // 当前值
    };
}