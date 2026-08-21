#pragma once

#include <osgEarth/TileLayer>
#include <osgEarth/Viewpoint>
#include <osgEarth/GeoData>

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
        double pitchDegrees = -40.0;

        // 图层最大地面跨度与相机观察距离之间的比例。
        double rangeScale = 2.0;

        // 相机允许使用的最小观察距离，单位为米。
        double minimumRangeMeters = 100.0;

        // 观察中心的高程，单位为米。
        double focalAltitudeMeters = 0.0;
    };

    // 根据一个有效的地理范围计算初始视点。
    //
    // 这是视点计算功能的基础版本。
    // 它不关心范围来自影像、DEM、矢量还是其他空间数据，
    // 只要调用方能够提供有效的 GeoExtent，就可以计算 Viewpoint。
    std::optional<osgEarth::Viewpoint> calculateInitialViewpoint(
        const osgEarth::GeoExtent& extent,
        const InitialViewpointOptions& options = {}
    );

    // 根据已经打开的 TileLayer 计算初始视点。
    //
    // 这个重载主要服务于 GDALImageLayer、GDALElevationLayer、
    // FeatureImageLayer 等 TileLayer 类型。
    //
    // 函数内部会提取图层的数据范围，
    // 然后调用上面的 GeoExtent 基础版本完成实际计算。
    std::optional<osgEarth::Viewpoint> calculateInitialViewpoint(
        const osgEarth::TileLayer& layer,
        const InitialViewpointOptions& options = {}
    );
}