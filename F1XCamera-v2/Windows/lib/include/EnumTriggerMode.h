#pragma once
namespace BaseCamera
{
    /// <summary>
    /// 触发源
    /// </summary>
    enum class EnumTriggerMode : int
    {
        /// <summary>
        /// 关闭
        /// </summary>
        Off,
        /// <summary>
        /// 软件触发
        /// </summary>
        Software,
        /// <summary>
        /// 线路
        /// </summary>
        Line1,
        /// <summary>
        /// 线路
        /// </summary>
        Line2,

        ///// <summary>
        ///// 计数器
        ///// </summary>
        // Counter1,
        /// <summary>
        /// 多路触发
        /// </summary>
        // Anyway,
    };
}
