#pragma once
#include "IBaseParam.h"
#include <vector>
#include "EnumParamType.h"
namespace BaseCamera
{
	// Wavelength 类，继承自 IBaseParam
	class Wavelength : public IBaseParam
	{
	private:
		std::vector<float> *items_; // 所有波长
		std::vector<float> *value_; // 当前波长
	public:
		// 带参数的构造函数
		Wavelength(std::vector<float> *items, std::vector<float> *value)
		{
			items_ = items;
			value_ = value;
		}

		// 重写获取 ParamType 函数
		EnumParamType getParamType() const
		{
			return EnumParamType::Wavelength;
		}

		// Getter 和 Setter 方法
		std::vector<float> *getItems()
		{
			return items_;
		}
		void setItems(std::vector<float> *newItems)
		{
			items_ = newItems;
		}
		std::vector<float> *getValue()
		{
			return value_;
		}
		void setValue(std::vector<float> *newValue)
		{
			value_ = newValue;
		}
	};

}