#include <HelloEarth/navigation/ViewpointCalculator.h>

#include <osgEarth/GeoData>
#include <osgEarth/Units>

#include <algorithm>
#include <cmath>

namespace HelloEarth::Navigation
{
    std::optional<osgEarth::Viewpoint>
    calculateInitialViewpoint(
        const osgEarth::GeoExtent& extent,
        const InitialViewpointOptions& options
    )
    {
        // 首先检查外部传入的计算参数是否为有限数值。
        if (!std::isfinite(options.headingDegrees) ||
            !std::isfinite(options.pitchDegrees) ||
            !std::isfinite(options.rangeScale) ||
            !std::isfinite(options.minimumRangeMeters) ||
            !std::isfinite(options.focalAltitudeMeters))
        {
            return std::nullopt;
        }

        // rangeScale 必须为正数，否则无法计算合理观察距离。
        // minimumRangeMeters 也必须大于零。
        if (options.rangeScale <= 0.0 ||
            options.minimumRangeMeters <= 0.0)
        {
            return std::nullopt;
        }

        // 无论范围来自影像、DEM 还是矢量数据，
        // 都必须先确认它包含有效的空间参考和坐标范围。
        if (!extent.isValid())
        {
            return std::nullopt;
        }

        // 使用数据范围中心作为相机观察中心。
        osgEarth::GeoPoint focalPoint =
            extent.getCentroid();

        if (!focalPoint.isValid())
        {
            return std::nullopt;
        }

        // getCentroid 默认返回 z=0。
        // 这里允许调用方指定观察中心相对于地形的高度。
        focalPoint.z() =
            options.focalAltitudeMeters;

        // osgEarth 的 Viewpoint Range 使用米，因此将范围宽高换算成米。
        const double widthMeters =
            extent.width(osgEarth::Units::METERS);

        const double heightMeters =
            extent.height(osgEarth::Units::METERS);

        if (!std::isfinite(widthMeters) ||
            !std::isfinite(heightMeters) ||
            widthMeters < 0.0 ||
            heightMeters < 0.0)
        {
            return std::nullopt;
        }

        // 取宽高中较大的一个，确保相机能够覆盖图层的主要范围。
        const double maximumSpanMeters =
            std::max(widthMeters, heightMeters);

        const double calculatedRangeMeters =
            maximumSpanMeters * options.rangeScale;

        // 对非常小的图层应用最小观察距离限制。
        const double rangeMeters =
            std::max(
                calculatedRangeMeters,
                options.minimumRangeMeters
            );

        if (!std::isfinite(rangeMeters) ||
            rangeMeters <= 0.0)
        {
            return std::nullopt;
        }

        osgEarth::Viewpoint viewpoint;

        viewpoint.setFocalPoint(focalPoint);

        viewpoint.setHeading(
            osgEarth::Angle(
                options.headingDegrees,
                osgEarth::Units::DEGREES
            )
        );

        viewpoint.setPitch(
            osgEarth::Angle(
                options.pitchDegrees,
                osgEarth::Units::DEGREES
            )
        );

        viewpoint.setRange(
            osgEarth::Distance(
                rangeMeters,
                osgEarth::Units::METERS
            )
        );

        if (!viewpoint.isValid())
        {
            return std::nullopt;
        }

        return viewpoint;
    }

    std::optional<osgEarth::Viewpoint>
    calculateInitialViewpoint(
        const osgEarth::TileLayer& layer,
        const InitialViewpointOptions& options
    )
    {
        // TileLayer 可能由一个或多个数据范围组成。
        // getDataExtentsUnion() 会取得这些范围合并后的总体范围。
        const osgEarth::DataExtent& extent =
            layer.getDataExtentsUnion();

        // 实际的视点计算统一交给 GeoExtent 基础版本。
        //
        // DataExtent 在 GeoExtent 的基础上增加了层级等信息，
        // 因此可以作为 GeoExtent 传入基础计算函数。
        return calculateInitialViewpoint(
            extent,
            options
        );
    }
}