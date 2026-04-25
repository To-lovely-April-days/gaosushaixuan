#pragma once
#include <fstream>
#include <string>
#include "PlsAI.h"

class CalibrationInfo
{
public:
	std::vector<float> k_White;
	std::vector<float> White_ReadBytes;
	std::vector<float> k_Black;
	std::vector<float> Black_ReadBytes;
public:
	std::vector<float> ParseFloatArray( std::string& value) {
		// 去除方括号
		value = value.substr(1, value.size() - 2);  // 通过 substr 去除前后的 '[' 和 ']'

		// 创建一个字符串流
		std::istringstream ss(value);
		std::vector<float> numbers;
		std::string temp;

		// 使用 std::getline 按逗号分割字符串
		while (std::getline(ss, temp, ',')) {
			numbers.push_back(std::stof(temp)); // 将每个部分转换为浮点数并添加到容器中
		}
		return  numbers;
	}
	bool loadData(const std::string& filename)
	{
		std::ifstream file(filename);
		if (!file.is_open()) {
			return false; // 文件无法打开
		}

		std::string line;
		for (size_t i = 0; i < 4; i++)
		{
			std::getline(file, line); // 读取第一行
			std::string prop, rawValue;
			if (!PlsAI::splitAndTrim(line, prop, rawValue)) {
				return false; // 无法分割字符串或 '=' 未找到
			}
			if (prop == "k_White") {
				try {
					k_White = ParseFloatArray(rawValue); // 尝试将 rawValue 转换为整数
				}
				catch (const std::invalid_argument& e) {
					return false; // 转换失败
				}
			}
			else if(prop == "White_ReadBytes")
			{
				try {
					White_ReadBytes = ParseFloatArray(rawValue); // 尝试将 rawValue 转换为整数
				}
				catch (const std::invalid_argument& e) {
					return false; // 转换失败
				}
			}
			else if (prop == "k_Black")
			{
				try {
					k_Black = ParseFloatArray(rawValue); // 尝试将 rawValue 转换为整数
				}
				catch (const std::invalid_argument& e) {
					return false; // 转换失败
				}
			}
			else if (prop == "Black_ReadBytes")
			{
				try {
					Black_ReadBytes = ParseFloatArray(rawValue); // 尝试将 rawValue 转换为整数
				}
				catch (const std::invalid_argument& e) {
					return false; // 转换失败
				}
			}
		}
	}
};

