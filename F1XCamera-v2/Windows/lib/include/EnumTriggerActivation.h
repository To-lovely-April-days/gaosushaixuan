#pragma once
namespace BaseCamera
{
    /// <summary>
    /// 触发激活
    /// </summary>
    enum class EnumTriggerActivation : int
    {
        /// <summary>
        /// 上升沿
        /// </summary>
        RisingEdge,
        /// <summary>
        /// 下降沿
        /// </summary>
        FallingEdge,
        /// <summary>
        /// 高电平
        /// </summary>
        LevelHigh,
        /// <summary>
        /// 低电平
        /// </summary>
        LevelLow,
        /// <summary>
        /// 上升或下降沿
        /// </summary>
        AnyEdge
    };
}