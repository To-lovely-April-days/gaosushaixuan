#pragma once
#include "BankZoneSize.h"
#include "BankZoneOffset.h"
#include "KeyValueItem.h"

namespace BaseCamera
{
	/// <summary>
	/// Bank空间大小
	/// </summary>
	class BASECAMERA_CLASS SpectralBankZone
	{
	public:
		/// <summary>
		/// 初始化类（开发者不能自己new该类，请从返回的 ParmInfo.BanklistCur中使用）
		/// </summary>
		/// <param name="index">bank数量索引，从0开始</param>
		/// <param name="enable">是否启用</param>
		/// <param name="wavelengths">所有波长</param>
		/// <param name="offset">波长偏移</param>
		/// <param name="size">波长数量</param>
		/// <exception cref="ArgumentNullException">所有波长数据为空</exception>
		/// <exception cref="ArgumentException">波长数量不能为0</exception>
		SpectralBankZone(int index, bool enable, std::vector<float> wavelengths, BankZoneOffset offset, BankZoneSize size);
		~SpectralBankZone();

		/// <summary>
		/// Bank索引值，从0开始
		/// </summary>
		int Index;
		/// <summary>
		/// 是否启用
		/// </summary>
		bool Enable;
		/// <summary>
		/// 所有的波长
		/// </summary>
		std::vector<float> Wavelengths;
		/// <summary>
		/// 可选择的开始波长
		/// </summary>
		std::vector < KeyValueItem*> OptionalStartingWavelengths;
		/// <summary>
		/// 可选择的结束波长
		/// </summary>
		std::vector < KeyValueItem*> OptionalEndWavelengths;
		/// <summary>
		/// 开始波长
		/// </summary>
		float StartWavelength;
		/// <summary>
		/// 结束波长
		/// </summary>
		float EndWavelength;
		/// <summary>
		/// 波长偏移大小
		/// </summary>
		BankZoneOffset Offset;
		/// <summary>
		/// 波长大小
		/// </summary>
		BankZoneSize Size;
		/// <summary>
		/// 启用Bank
		/// </summary>
		/// <param name="startWavelength">开始波长</param>
		/// <param name="endWavelength">结束波长</param>
		/// <exception cref="ArgumentNullException">所有波长数据为空</exception>
		/// <exception cref="ArgumentException">开始波长不能大于结束波长</exception>
		void EnableBank(float startWavelength, float endWavelength);
		/// <summary>
		/// 停用Bank
		/// </summary>
		/// <exception cref="ArgumentNullException">所有波长数据为空</exception>
		void DisableBank();

	};
}