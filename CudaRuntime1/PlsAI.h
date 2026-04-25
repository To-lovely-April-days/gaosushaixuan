#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class PlsAI
{
public:
	std::string TargetName;//材料名称
public:
	std::vector<int> selectIndex;//设置完ROI之后对应建模波段的序号
public:
	/// 模型类型 0 PLS, 1 SVM
	int ModelType;
public:
	int classid;
	float threshold;
	int Components;
	int Features;
	std::vector<float> Intercept;
	std::vector<float> Coef;
	std::vector<float> StdX;
	std::vector<float> MeanX;

	// 定义PlsAI类的序列化规则
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlsAI, TargetName, selectIndex, ModelType, classid, threshold,
		Components, Features, Intercept, Coef, StdX, MeanX)
public:
	// 序列化成员函数
	void to_json(json& j) const {
		j = json{
			{"TargetName", TargetName},
			{"selectIndex", selectIndex},
			{"ModelType", ModelType},
			{"classid", classid},
			{"threshold", threshold},
			{"Components", Components},
			{"Features", Features},
			{"Intercept", Intercept},
			{"Coef", Coef},
			{"StdX", StdX},
			{"MeanX", MeanX}
		};
	}

	// 反序列化成员函数
	void from_json(const json& j) {
		j.at("TargetName").get_to(TargetName);
		j.at("selectIndex").get_to(selectIndex);
		j.at("ModelType").get_to(ModelType);
		j.at("classid").get_to(classid);
		j.at("threshold").get_to(threshold);
		j.at("Components").get_to(Components);
		j.at("Features").get_to(Features);
		j.at("Intercept").get_to(Intercept);
		j.at("Coef").get_to(Coef);
		j.at("StdX").get_to(StdX);
		j.at("MeanX").get_to(MeanX);
	}
public:
	// 去除前后空白字符的函数
	static	std::string trim(const std::string& str) {
		size_t first = str.find_first_not_of(" \t");
		size_t last = str.find_last_not_of(" \t");

		if (first == std::string::npos || last == std::string::npos) {
			return "";
		}

		return str.substr(first, (last - first + 1));
	}
	// 分割字符串并去除前后空白字符
	static	bool splitAndTrim(const std::string& line, std::string& prop, std::string& rawValue) {
		size_t pos = line.find('=');
		if (pos == std::string::npos) {
			return false; // '=' 未找到
		}

		prop = line.substr(0, pos);
		rawValue = line.substr(pos + 1);

		// 去除前后空白字符
		prop = trim(prop);
		rawValue = trim(rawValue);

		return true;
	}
	std::pair<std::vector<int>, std::vector<float>> ParseFloatArray(const std::string& value) {
		std::regex rgx(R"(\]\[)");
		std::vector<std::string> sv;
		std::sregex_token_iterator iter(value.begin(), value.end(), rgx, -1);
		std::sregex_token_iterator end;

		// 分割成两部分
		for (; iter != end; ++iter) {
			sv.push_back(*iter);
		}

		// 处理 shape 部分
		std::string shape_str = sv[0];
		shape_str.erase(0, 1);  // 去掉开头的 '['
		std::stringstream shape_stream(shape_str);
		std::string shape_item;
		std::vector<int> shape;
		while (std::getline(shape_stream, shape_item, ',')) {
			shape.push_back(std::stoi(shape_item));
		}

		// 处理 items 部分
		std::string items_str = sv[1];
		items_str.erase(items_str.size() - 1, 1);  // 去掉结尾的 ']'
		std::stringstream items_stream(items_str);
		std::string item;
		std::vector<float> items;
		while (std::getline(items_stream, item, ',')) {
			items.push_back(std::stof(item));
		}
		return { shape, items };
	}

	bool loadModel(const std::string& filename)
	{
		std::ifstream file(filename);
		if (!file.is_open()) {
			return false; // 文件无法打开
		}

		std::string line;
		std::getline(file, line); // 读取第一行

		std::string prop, rawValue;
		if (!splitAndTrim(line, prop, rawValue)) {
			return false; // 无法分割字符串或 '=' 未找到
		}

		if (prop == "model_type") {
			try {
				ModelType = std::stoi(rawValue); // 尝试将 rawValue 转换为整数
			}
			catch (const std::invalid_argument& e) {
				return false; // 转换失败
			}
		}
		else {
			return false; // 不匹配 "model_type"
		}

		std::getline(file, line); // 读取第一行

		if (!splitAndTrim(line, prop, rawValue)) {
			return false; // 无法分割字符串或 '=' 未找到
		}
		int count = 0;
		if (prop == "model_count") {
			try {
				count = std::stoi(rawValue); // 尝试将 rawValue 转换为整数
			}
			catch (const std::invalid_argument& e) {
				return false; // 转换失败
			}
		}
		else {
			return false; // 不匹配 "model_count"
		}
		//只读取第一个模型参数
		for (size_t i = 0; i < 7; i++)
		{
			std::getline(file, line); // 读取第一行

			if (!splitAndTrim(line, prop, rawValue)) {
				return false; // 无法分割字符串或 '=' 未找到
			}
			if (prop == "classid")
			{
				try {
					classid = std::stoi(rawValue); // 尝试将 rawValue 转换为整数
				}
				catch (const std::invalid_argument& e) {
					return false; // 转换失败
				}
			}
			else if (prop == "n_components")
			{
				try {
					Components = std::stoi(rawValue); // 尝试将 rawValue 转换为整数
				}
				catch (const std::invalid_argument& e) {
					return false; // 转换失败
				}
			}
			else if (prop == "n_features_in")
			{
				try {
					Features = std::stoi(rawValue); // 尝试将 rawValue 转换为整数
				}
				catch (const std::invalid_argument& e) {
					return false; // 转换失败
				}
			}
			else if (prop == "intercept")
			{
				std::pair<std::vector<int>, std::vector<float>> result = ParseFloatArray(rawValue);
				// 解构 std::pair
				std::vector<int>& shape = result.first;
				Intercept = result.second;
			}
			else if (prop == "coef")
			{
				std::pair<std::vector<int>, std::vector<float>> result = ParseFloatArray(rawValue);
				// 解构 std::pair
				std::vector<int>& shape = result.first;
				Coef = result.second;
			}
			else if (prop == "x_std")
			{
				std::pair<std::vector<int>, std::vector<float>> result = ParseFloatArray(rawValue);
				// 解构 std::pair
				std::vector<int>& shape = result.first;
				StdX = result.second;
			}
			else if (prop == "x_mean")
			{
				std::pair<std::vector<int>, std::vector<float>> result = ParseFloatArray(rawValue);
				// 解构 std::pair
				std::vector<int>& shape = result.first;
				MeanX = result.second;
			}
		}
	}


};

