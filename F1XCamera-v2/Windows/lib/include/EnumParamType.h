#pragma once
#include "pch.h"

namespace BaseCamera
{
    /// <summary>
    /// 相机参数名称
    /// 该类型枚举值按照参数影响的顺序排列
    /// </summary>
    enum class EnumParamType : int
    {
        None,
        TriggerMode,
        TriggerActivation,

        PixelFormat,
        BandBinning,
        SampleBinning,
        BandROI,

        ExposureMode,
        ExposureTime,

        AcquisitionFrameRate,

        Gain,
        SampleDirection,
        BandDirection,

        Samples,
        Bands,
        Wavelength,
    };
}