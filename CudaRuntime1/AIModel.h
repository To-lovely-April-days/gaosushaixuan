#pragma once
#include <vector>
#include "PlsAI.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

class AIModel
{
public://相机信息
	string Sn;
	string FxModel;
public://相机设置
	int ExpTime;
	int Gain;
	int FrameRate;
	int StartBandIndex;
	int EndBandIndex;
	/// <summary>
	/// ROI模式下的索引，即在parminfo.SpectralChannelWavelength的开始索引
	/// 每次在设置ROI模式后更新
	/// 训练过程设置的起始位置和结束位置，与相机能够设置的不一致，这个参数用于记录这个偏差
	/// </summary>
	int RoiBandStartIndex;
public://限制范围（原先的扣底）
	int LimitScopeFlag;
	float LowestValue;
	float HighestValue;
public://预处理
	std::vector<int> Preprocessings;
	int FilterStrength;//滑动滤波长度
public://模型列表
	std::vector<PlsAI> CoreList;//前期只允许存在一个模型数据

	// 序列化AIModel对象为JSON
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(AIModel, Sn, FxModel, ExpTime, FrameRate, Gain, StartBandIndex, EndBandIndex, LimitScopeFlag, LowestValue, HighestValue, Preprocessings, FilterStrength, CoreList)

public:
	// 序列化成员函数
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
			{"LimitScope", LimitScopeFlag},
			{"LowestValue", LowestValue},
			{"HighestValue", HighestValue},
			{"Preprocessings", Preprocessings},
			{"FilterStrength", FilterStrength},
			{"CoreList", CoreList}
		};
	}

	// 反序列化成员函数
	void from_json(const json& j) {
		j.at("Sn").get_to(Sn);
		j.at("FxModel").get_to(FxModel);
		j.at("ExpTime").get_to(ExpTime);
		j.at("FrameRate").get_to(FrameRate);
		j.at("Gain").get_to(Gain);
		j.at("StartBandIndex").get_to(StartBandIndex);
		j.at("EndBandIndex").get_to(EndBandIndex);
		j.at("RoiBandStartIndex").get_to(RoiBandStartIndex);
		j.at("LimitScopeFlag").get_to(LimitScopeFlag);
		j.at("LowestValue").get_to(LowestValue);
		j.at("HighestValue").get_to(HighestValue);
		j.at("Preprocessings").get_to(Preprocessings);
		j.at("FilterStrength").get_to(FilterStrength);
		j.at("CoreList").get_to(CoreList);
	}

	bool loadData()
	{
		// 从文件读取 JSON 数据并反序列化为 AIModel 对象
		//std::ifstream inputFile("output.json");
		//json jRead;
		//inputFile >> jRead;

		//AIModel newModel;
		//newModel.from_json(jRead);
		//inputFile.close();  // 关闭文件
	}
	bool exportData(const std::string& filename)
	{
		//json j = model;
		//std::ofstream outFile("output.json");
		//outFile << j.dump(4);  // 美化输出
		//outFile.close();
	}
};

