#pragma once

#include <osgEarth/TileLayer>
#include <osgEarth/Viewpoint>

#include <optional>

namespace HelloEarth::Navigation
{
    // 控制初始视点计算方式的可调参数。
    struct InitialViewpointOptions
    {
        // 相机围绕观察中心的水平方向，单位为度。
        double headingDegrees = 0.0;

        // 相机俯仰角，单位为度。
        // -90 表示垂直俯视。
        double pitchDegrees = -90.0;

        // 图层最大地面跨度与相机观察距离之间的比例。
        double rangeScale = 2.0;

        // 相机允许使用的最小观察距离，单位为米。
        double minimumRangeMeters = 100.0;

        // 观察中心的高程，单位为米。
        double focalAltitudeMeters = 0.0;
    };

    // 根据已经打开的 TileLayer 数据范围计算初始视点。
    //
    // 成功时返回有效的 osgEarth::Viewpoint；
    // 图层没有有效范围或参数不合理时返回 std::nullopt。
    std::optional<osgEarth::Viewpoint> calculateInitialViewpoint(
        const osgEarth::TileLayer& layer,
        const InitialViewpointOptions& options = {}
    );
}