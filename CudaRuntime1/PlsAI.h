// ============================================================================
// 文件名：PlsAI.h
// 作用  ：单个 PLS-DA（偏最小二乘判别分析）二分类模型
//         一个目标种类对应一个 PlsAI 实例
//         多个 PlsAI 组合在 AIModel.CoreList 中实现多分类
// ============================================================================
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
    std::string TargetName;     // 目标材料名称（如"PE"、"PET"）

public:
    // 模型选用的波段索引列表
    // 训练时通过特征选择保留的波段，运行时只用这些波段做预测
    std::vector<int> selectIndex;

public:
    int ModelType;              // 模型类型：0=PLS  1=SVM（当前只支持 PLS）

public:
    // ============== PLS-DA 模型参数 ==============
    int classid;                // 分类 ID，分选结果中标记此值（如 1=PE、2=PET）
    float threshold;            // 判定阈值；判别值超过则归为该类

    int Components;             // PLS 主成分数（建模时的超参数）
    int Features;               // 特征数 = selectIndex.size()

    std::vector<float> Intercept;  // 截距向量
    std::vector<float> Coef;       // 回归系数向量（按选用波段顺序）
    std::vector<float> StdX;       // 各波段标准差（用于标准化）
    std::vector<float> MeanX;      // 各波段均值（用于中心化）

    // 序列化宏（自动生成 to_json/from_json）
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(PlsAI, TargetName, selectIndex, ModelType,
        classid, threshold, Components, Features, Intercept, Coef, StdX, MeanX)

public:
    // 序列化（对象 → JSON）
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

    // 反序列化（JSON → 对象）
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
    // ============== 字符串处理工具函数（被 CalibrationInfo 等共用）==============

    // 去除字符串前后空白（空格、Tab）
    static std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t");
        size_t last  = str.find_last_not_of(" \t");
        if (first == std::string::npos || last == std::string::npos) return "";
        return str.substr(first, (last - first + 1));
    }

    // 按 '=' 分割一行，得到 prop=value，自动去除空白
    // 用于解析 "prop = value" 格式的配置文件
    static bool splitAndTrim(const std::string& line, std::string& prop, std::string& rawValue) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) return false;
        prop     = line.substr(0, pos);
        rawValue = line.substr(pos + 1);
        prop     = trim(prop);
        rawValue = trim(rawValue);
        return true;
    }

    // 解析 "[shape][items]" 格式的字符串
    // 形如："[3,4][1.0,2.0,3.0,4.0,...]" → 拆出 shape 和 items 两个数组
    // pair.first  = shape（int 数组）
    // pair.second = items（float 数组）
    std::pair<std::vector<int>, std::vector<float>> ParseFloatArray(const std::string& value) {
        std::regex rgx(R"(\]\[)");   // 用 "][" 作分隔
        std::vector<std::string> sv;
        std::sregex_token_iterator iter(value.begin(), value.end(), rgx, -1);
        std::sregex_token_iterator end;
        for (; iter != end; ++iter) sv.push_back(*iter);

        // 处理 shape 部分（去掉开头 '['）
        std::string shape_str = sv[0];
        shape_str.erase(0, 1);
        std::stringstream shape_stream(shape_str);
        std::string shape_item;
        std::vector<int> shape;
        while (std::getline(shape_stream, shape_item, ',')) {
            shape.push_back(std::stoi(shape_item));
        }

        // 处理 items 部分（去掉结尾 ']'）
        std::string items_str = sv[1];
        items_str.erase(items_str.size() - 1, 1);
        std::stringstream items_stream(items_str);
        std::string item;
        std::vector<float> items;
        while (std::getline(items_stream, item, ',')) {
            items.push_back(std::stof(item));
        }
        return { shape, items };
    }

    // ============== 旧式文件加载（已被 JSON 方式取代）==============
    // 解析 "prop = value" 行格式的模型文件
    // 当前版本主要走 JSON 路径，此方法保留作向后兼容
    bool loadModel(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::string line;
        std::getline(file, line);

        std::string prop, rawValue;
        if (!splitAndTrim(line, prop, rawValue)) return false;

        // 第一行应为 "model_type = ..."
        if (prop == "model_type") {
            try { ModelType = std::stoi(rawValue); }
            catch (const std::invalid_argument& e) { return false; }
        } else return false;

        // 第二行应为 "model_count = ..."
        std::getline(file, line);
        if (!splitAndTrim(line, prop, rawValue)) return false;
        int count = 0;
        if (prop == "model_count") {
            try { count = std::stoi(rawValue); }
            catch (const std::invalid_argument& e) { return false; }
        } else return false;

        // 接下来 7 行是第一个模型的参数（只读第一个模型）
        for (size_t i = 0; i < 7; i++)
        {
            std::getline(file, line);
            if (!splitAndTrim(line, prop, rawValue)) return false;

            if (prop == "classid")
                try { classid = std::stoi(rawValue); }
                catch (const std::invalid_argument& e) { return false; }
            else if (prop == "n_components")
                try { Components = std::stoi(rawValue); }
                catch (const std::invalid_argument& e) { return false; }
            else if (prop == "n_features_in")
                try { Features = std::stoi(rawValue); }
                catch (const std::invalid_argument& e) { return false; }
            else if (prop == "intercept") {
                auto result = ParseFloatArray(rawValue);
                Intercept = result.second;
            }
            else if (prop == "coef") {
                auto result = ParseFloatArray(rawValue);
                Coef = result.second;
            }
            else if (prop == "x_std") {
                auto result = ParseFloatArray(rawValue);
                StdX = result.second;
            }
            else if (prop == "x_mean") {
                auto result = ParseFloatArray(rawValue);
                MeanX = result.second;
            }
        }
        return true;   // 注意：原代码这里没有显式 return，可能落在末尾未定义
    }
};
