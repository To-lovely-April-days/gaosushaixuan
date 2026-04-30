// ============================================================================
// 文件名：CalibrationInfo.h
// 作用  ：黑白校准数据
//         分选前需要先做"白板校准"和"黑板校准"，得到每个像素+波段的修正系数
//         运行时用这些系数把原始 ADC 值转换为反射率
//
// 反射率公式（kernel.cu 中实现）：
//   reflectance = (raw - black) / (white - black) * k_white
// ============================================================================
#pragma once
#include <fstream>
#include <string>
#include "PlsAI.h"

class CalibrationInfo
{
public:
    // ============== 校准数据 ==============
    std::vector<float> k_White;          // 白板修正系数（每波段一个）
    std::vector<float> White_ReadBytes;  // 白板原始读数（每像素×每波段）
    std::vector<float> k_Black;          // 黑板修正系数（保留，当前未使用）
    std::vector<float> Black_ReadBytes;  // 黑板原始读数（每像素×每波段）

public:
    // 把字符串 "[1.0,2.0,3.0,...]" 解析为 float 数组
    std::vector<float> ParseFloatArray(std::string& value) {
        // 去除前后的方括号
        value = value.substr(1, value.size() - 2);

        // 按逗号分割并转换为 float
        std::istringstream ss(value);
        std::vector<float> numbers;
        std::string temp;
        while (std::getline(ss, temp, ',')) {
            numbers.push_back(std::stof(temp));
        }
        return numbers;
    }

    // ============== 加载校准数据文件 ==============
    // 文件格式（每行一项）：
    //   k_White         = [1.0,2.0,...]
    //   White_ReadBytes = [...]
    //   k_Black         = [...]
    //   Black_ReadBytes = [...]
    bool loadData(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        // 共读 4 行（4 个数据项）
        for (size_t i = 0; i < 4; i++)
        {
            std::getline(file, line);
            std::string prop, rawValue;
            // 利用 PlsAI 的工具函数按 '=' 分割并 trim
            if (!PlsAI::splitAndTrim(line, prop, rawValue)) {
                return false;
            }

            if (prop == "k_White") {
                try { k_White = ParseFloatArray(rawValue); }
                catch (const std::invalid_argument& e) { return false; }
            }
            else if (prop == "White_ReadBytes") {
                try { White_ReadBytes = ParseFloatArray(rawValue); }
                catch (const std::invalid_argument& e) { return false; }
            }
            else if (prop == "k_Black") {
                try { k_Black = ParseFloatArray(rawValue); }
                catch (const std::invalid_argument& e) { return false; }
            }
            else if (prop == "Black_ReadBytes") {
                try { Black_ReadBytes = ParseFloatArray(rawValue); }
                catch (const std::invalid_argument& e) { return false; }
            }
        }
        return true;   // 注意：原代码缺少此处 return，已补上
    }
};
