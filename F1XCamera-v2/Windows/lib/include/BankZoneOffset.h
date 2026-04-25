#pragma once

namespace BaseCamera
{
	class BASECAMERA_CLASS BankZoneOffset
	{
	public:
		BankZoneOffset();
		BankZoneOffset(int max, int min, int inc, int value);
		/// <summary>
		/// 步进值
		/// </summary>
		int Inc;
		/// <summary>
		/// 最大值
		/// </summary>
		int Max;
		/// <summary>
		/// 最小值
		/// </summary>
		int Min;
		// int _value;
		/// <summary>
		/// 值内容
		/// </summary>
		int OffsetValue;

	public:
		int getOffsetValue();
		void setOffsetValue(int value);
	};
}