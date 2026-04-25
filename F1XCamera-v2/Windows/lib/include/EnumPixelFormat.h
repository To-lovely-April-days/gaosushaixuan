#pragma once

namespace BaseCamera
{
    /// <summary>
    /// 像素格式
    /// </summary>
    enum class EnumPixelFormat : int
    {
        //[Description("RGB 8")]
        // RGB8,
        // [Description("Mono 8")]
        Mono8 = 8,
        // [Description("Mono 10")]
        Mono10 = 10,
        //[Description("Mono 10p")]
        // Mono10p,
        // [Description("Mono 12")]
        Mono12 = 12,
        //[Description("Mono 12p")]
        // Mono12p,
        // [Description("Mono 14")]
        Mono14 = 14,
        //[Description("Mono 14p")]
        // Mono14p=14,
        // [Description("Mono 16")]
        Mono16 = 16
    };
}