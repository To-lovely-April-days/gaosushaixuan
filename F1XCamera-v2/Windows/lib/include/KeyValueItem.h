#pragma once
#include <string>
namespace BaseCamera
{
	class  KeyValueItem
	{
	public:
		KeyValueItem(std::string key, float val):Text(key),Value(val)
		{
		}
		/// <summary>
		/// 标题（title)
		/// </summary>
		std::string Text;
		/// <summary>
		/// 值(value)
		/// </summary>
		float Value;
	};
}