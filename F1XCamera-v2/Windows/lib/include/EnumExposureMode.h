#pragma once

namespace BaseCamera
{
    /// <summary>
    /// 曝光模式
    /// </summary>
    enum class EnumExposureMode : int
    {
        /// <summary>
        /// 关闭
        /// </summary>
        Off,
        /// <summary>
        /// 一次
        /// </summary>
        Once,
        /// <summary>
        /// 连续
        /// </summary>
        Continuous,
    };
}