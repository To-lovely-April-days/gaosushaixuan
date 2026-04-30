// ============================================================================
// 文件名：AIModel.h
// 作用  ：完整的 AI 分选模型类
//         包含三类信息：
//           1) 相机基本信息（序列号、型号）
//           2) 相机配置参数（曝光、增益、ROI 等）
//           3) 算法参数（扣底范围、预处理列表、分类模型列表）
//         支持 JSON 序列化/反序列化（用 nlohmann/json 库）
// ============================================================================
#pragma once
#include <vector>
#include "PlsAI.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

class AIModel
{
public:
    // ============== 相机标识信息 ==============
    string Sn;        // 相机序列号
    string FxModel;   // 相机型号

public:
    // ============== 相机参数 ==============
    int ExpTime;          // 曝光时间（微秒）
    int Gain;             // 增益档位（0=高 1=中 2=低）
    int FrameRate;        // 帧率
    int StartBandIndex;   // 模型起始波段（绝对索引）
    int EndBandIndex;     // 模型结束波段（绝对索引）

    // ROI 模式下的索引偏移
    // 训练时设置的起始位置和实际相机能设置的位置可能不一致（受相机硬件 inc 限制）
    // 这个参数记录两者的差，运行时用来校正波段索引
    int RoiBandStartIndex;

public:
    // ============== 限制范围（扣底）==============
    int   LimitScopeFlag; // 0=不启用扣底  1=启用扣底
    float LowestValue;    // 反射率累积下限
    float HighestValue;   // 反射率累积上限

public:
    // ============== 预处理 ==============
    // Preprocessings 中每个元素是一个预处理类型：
    //   0 = 滑动平滑    1 = 一阶求导
    //   2 = 二阶求导    3 = 最大最小归一化
    // 按顺序执行
    std::vector<int> Preprocessings;
    int FilterStrength;   // 滑动平滑窗口大小

public:
    // ============== 分类模型列表 ==============
    // 每个 PlsAI 是一个独立的二分类器（PLS-DA）
    // 多个分类器组合可以做多目标分选
    std::vector<PlsAI> CoreList;

    // 用 nlohmann/json 库的宏定义快速生成序列化代码
    // 这一行让 AIModel 自动支持 to_json 和 from_json
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AIModel, Sn, FxModel, ExpTime, FrameRate, Gain,
        StartBandIndex, EndBandIndex, LimitScopeFlag, LowestValue, HighestValue,
        Preprocessings, FilterStrength, CoreList)

public:
    // ============== 序列化（对象 → JSON）==============
    // 注意：相比 NLOHMANN_DEFINE_TYPE_INTRUSIVE，这里多了 RoiBandStartIndex 字段
    void to_json(json& j) const {
        j = json{
            {"Sn", Sn},
            {"FxModel", FxModel},
            {"ExpTime", ExpTime},
            {"FrameRate", FrameRate},
            {"Gain", Gain},
            {"StartBandIndex", StartBandIndex},
            {"EndBandIndex", EndBandIndex},
            {"RoiBandStartIndex", RoiBandStartIndex},
            {"LimitScope", LimitScopeFlag},   // 注意 key 名不同（LimitScope vs LimitScopeFlag）
            {"LowestValue", LowestValue},
            {"HighestValue", HighestValue},
            {"Preprocessings", Preprocessings},
            {"FilterStrength", FilterStrength},
            {"CoreList", CoreList}
        };
    }

    // ============== 反序列化（JSON → 对象）==============
    void from_json(const json& j) {
        j.at("Sn").get_to(Sn);
        j.at("FxModel").get_to(FxModel);
        j.at("ExpTime").get_to(ExpTime);
        j.at("FrameRate").get_to(FrameRate);
        j.at("Gain").get_to(Gain);
        j.at("StartBandIndex").get_to(StartBandIndex);
        j.at("EndBandIndex").get_to(EndBandIndex);
        j.at("RoiBandStartIndex").get_to(RoiBandStartIndex);
        j.at("LimitScopeFlag").get_to(LimitScopeFlag);   // 注意 key 名（与 to_json 不一致，疑似 bug）
        j.at("LowestValue").get_to(LowestValue);
        j.at("HighestValue").get_to(HighestValue);
        j.at("Preprocessings").get_to(Preprocessings);
        j.at("FilterStrength").get_to(FilterStrength);
        j.at("CoreList").get_to(CoreList);
    }

    // 占位函数：未实现
    bool loadData()    { /* 未实现 */ return false; }
    bool exportData(const std::string& filename) { /* 未实现 */ return false; }
};
