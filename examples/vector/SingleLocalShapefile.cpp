#include <osgEarth/MapNode>
#include <osgEarth/TMS>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GLUtils>
#include <osgEarth/GeoData>
#include <osgEarth/Units>
#include <osgEarth/Viewpoint>
#include <osg/ArgumentParser>
#include <osgViewer/Viewer>
#include <osgEarth/GDAL>
#include <osgEarth/OGRFeatureSource>
#include <osgEarth/FeatureModelLayer>
#include <osgEarth/Style>
#include <osgEarth/StyleSheet>
#include <osgEarth/Units>
#include <osgEarth/Geometry>
#include <osgEarth/FeatureImageLayer>

#include <iostream>
#include <algorithm>
#include <gdal_priv.h>

#include <HelloEarth/raster/RasterPreprocessor.h>
#include <HelloEarth/navigation/ViewpointCalculator.h>
#include <chrono>


int main(int argc, char** argv)
{
    osgEarth::initialize();

    osg::ArgumentParser args(&argc, argv);
    osgViewer::Viewer viewer(args);

    viewer.setRealizeOperation(
        new osgEarth::GL3RealizeOperation()
    );

    // const std::string demPath =
    //     "D:/work/projects/HelloEarthWorkspace/testdata/dem.tif";

    // auto elevation =
    //     new osgEarth::GDALElevationLayer();

    // elevation->setURL(demPath);


    auto mapNode =
        new osgEarth::MapNode();

    // mapNode->getMap()->addLayer(elevation);

    // if (elevation->getStatus().isError())
    // {
    //     std::cerr
    //         << "Failed to open DEM: "
    //         << elevation->getStatus().toString()
    //         << std::endl;

    //     return -1;
    // }

    // std::cout
    //     << "DEM opened successfully."
    //     << std::endl;

    const std::string globalImagePath =
        "D:/work/projects/HelloEarthWorkspace/testdata/"
        "NE1_HR_LC_SR_W/NE1_HR_LC_SR_W.tif";

    std::string preparedGlobalImagePath;

    if (!HelloEarth::Raster::prepareRasterForLoading(globalImagePath, preparedGlobalImagePath))
    {
        return -1;
    }

    auto globalImagery =
        new osgEarth::GDALImageLayer();

    globalImagery->setURL(preparedGlobalImagePath);
    // auto globalImagery = new osgEarth::TMSImageLayer();
    // globalImagery->setURL("https://readymap.org/readymap/tiles/1.0.0/7/");
    
    mapNode->getMap()->addLayer(globalImagery);

    if (globalImagery->getStatus().isError())
    {
        std::cerr
            << "Failed to open global imagery: "
            << globalImagery->getStatus().toString()
            << std::endl;

        return -1;
    }

    std::cout
        << "Global imagery opened successfully."
        << std::endl;

    // 当前测试使用的 Shapefile 主文件路径。
    // OGR 会自动在同一目录查找同名的
    // .shx、.dbf、.prj 和 .cpg 文件。
    const std::string shapefilePath =
        "D:/work/projects/HelloEarthWorkspace/testdata/SHP/"
        "ne_110m_admin_0_countries.shp";

    // OGRFeatureSource 负责读取矢量要素和属性数据。
    // 它相当于矢量数据的“数据提供者”，目前还不负责显示。
    auto shapefileSource =
        new osgEarth::OGRFeatureSource();

    // 设置数据源在 osgEarth 中的名称。
    // 这个名称是程序内部的图层名称，不会改变磁盘上的文件名。
    shapefileSource->setName(
        "Natural Earth Countries"
    );

    // 告诉 OGRFeatureSource 应当打开哪个矢量数据集。
    shapefileSource->setURL(
        shapefilePath
    );

    // 将数据源加入 Map。
    // 和 GDALImageLayer 一样，addLayer() 会触发数据源的初始化和打开。
    // 但 FeatureSource 本身是数据层，不会直接生成可见图形。
    mapNode->getMap()->addLayer(
        shapefileSource
    );

    // addLayer() 已经触发打开操作，
    // 因此现在可以通过 Status 判断 Shapefile 是否成功读取。
    if (shapefileSource->getStatus().isError())
    {
        std::cerr
            << "Failed to open Shapefile: "
            << shapefileSource->getStatus().toString()
            << std::endl;

        return -1;
    }

    std::cout
        << "Shapefile opened successfully."
        << std::endl;

    // 输出 OGR 当前读取到的要素数量，
    // 用来进一步确认数据源不只是“文件打开了”，
    // 而且确实识别到了其中的矢量要素。
    std::cout
        << "Feature count: "
        << shapefileSource->getFeatureCount()
        << std::endl;

    // 获取当前矢量数据源声明的几何类型。
    //
    // getGeometryType() 返回 osgEarth::Geometry::Type 枚举，
    // 例如点、线、面或者未知类型。
    const osgEarth::Geometry::Type geometryType =
        shapefileSource->getGeometryType();

    // 将枚举转换成便于阅读的文本并输出。
    // 例如：Point、LineString、Polygon。
    std::cout
        << "Geometry type: "
        << osgEarth::Geometry::toString(geometryType)
        << std::endl;

    // 获取矢量数据源的特征描述信息。
    //
    // FeatureProfile 保存的是整个矢量数据源的空间元数据，
    // 其中包括空间参考、总体范围以及是否采用切片组织等信息。
    //
    // 返回值是指针，因为数据源可能打开失败，或者没有成功建立 Profile。
    // 这个对象由 shapefileSource 管理，我们只读取它，不需要手动释放。
    const osgEarth::FeatureProfile* featureProfile =
        shapefileSource->getFeatureProfile();

    if (featureProfile == nullptr)
    {
        std::cerr
            << "Failed to obtain the feature profile."
            << std::endl;

        return -1;
    }

    // 获取整个矢量数据源的总体地理范围。
    //
    // 这里得到的不是某一个国家的范围，
    // 而是当前 Shapefile 中全部要素合并后的总体包围范围。
    const osgEarth::GeoExtent& featureExtent =
        featureProfile->getExtent();

    if (!featureExtent.isValid())
    {
        std::cerr
            << "The feature extent is invalid."
            << std::endl;

        return -1;
    }

    // 获取该范围使用的空间参考系统。
    const osgEarth::SpatialReference* featureSRS =
        featureExtent.getSRS();

    if (featureSRS == nullptr)
    {
        std::cerr
            << "The feature spatial reference is unavailable."
            << std::endl;

        return -1;
    }

    std::cout
        << "Feature SRS: "
        << featureSRS->getName()
        << std::endl;

    std::cout
        << "Feature extent:"
        << std::endl
        << "  X min: " << featureExtent.xMin() << std::endl
        << "  Y min: " << featureExtent.yMin() << std::endl
        << "  X max: " << featureExtent.xMax() << std::endl
        << "  Y max: " << featureExtent.yMax() << std::endl;

    // 创建当前矢量数据使用的通用样式。
    // 不再把它命名为 countryStyle，
    // 因为数据源未来可能是点、线或者面。
    osgEarth::Style vectorStyle;

    vectorStyle.setName(
        "default"
    );

    // 当前数据是点类型时，为它创建点符号。
    if (geometryType == osgEarth::Geometry::TYPE_POINT)
    {
        auto pointSymbol =
            vectorStyle.getOrCreate<osgEarth::PointSymbol>();

        // 设置点的填充颜色。
        pointSymbol->fill().mutable_value().color() =
            osgEarth::Color::Yellow;

        // 设置点在屏幕上的显示尺寸，单位为像素。
        pointSymbol->size().mutable_value() =
            8.0F;

        // 开启平滑，使点的边缘更加圆润。
        pointSymbol->smooth().mutable_value() =
            true;
    }
    else if (
        geometryType ==
        osgEarth::Geometry::TYPE_LINESTRING
    )
    {
        // LineSymbol 负责控制线要素的颜色、宽度等显示属性。
        auto lineSymbol =
            vectorStyle.getOrCreate<osgEarth::LineSymbol>();

        // 设置河流线的颜色。
        lineSymbol->stroke().mutable_value().color() =
            osgEarth::Color(
                0.10F,
                0.65F,
                1.00F,
                1.00F
            );

        // 设置线在屏幕上的宽度。
        // 使用 PIXELS 后，无论相机距离如何变化，
        // 基础线宽都按屏幕像素理解。
        lineSymbol->stroke().mutable_value().width() =
            osgEarth::Distance(
                3.0,
                osgEarth::Units::PIXELS
            );

        // 将跨度很大的线段进一步细分，
        // 使其能够更自然地贴合地球曲面。
        lineSymbol->tessellationSize() =
            osgEarth::Distance(
                50.0,
                osgEarth::Units::KILOMETERS
            );
    }
    else if (
        geometryType ==
        osgEarth::Geometry::TYPE_POLYGON
    )
    {
        // PolygonSymbol 控制面要素内部的填充效果。
        auto polygonSymbol =
            vectorStyle.getOrCreate<osgEarth::PolygonSymbol>();

        // 使用半透明蓝色填充，
        // 让下面的全球影像仍然能够透过面要素显示出来。
        polygonSymbol->fill().mutable_value().color() =
            osgEarth::Color(
                0.15F,
                0.55F,
                0.95F,
                0.30F
            );

        // 面边界仍然使用 LineSymbol 绘制。
        auto outlineSymbol =
            vectorStyle.getOrCreate<osgEarth::LineSymbol>();

        outlineSymbol->stroke().mutable_value().color() =
            osgEarth::Color::Yellow;

        outlineSymbol->stroke().mutable_value().width() =
            osgEarth::Distance(
                1.5,
                osgEarth::Units::PIXELS
            );

        // 细分跨度较大的边界线，使其贴合地球曲面。
        outlineSymbol->tessellationSize() =
            osgEarth::Distance(
                40.0,
                osgEarth::Units::KILOMETERS
            );
    }

    // AltitudeSymbol 属于点、线、面都可能使用的公共设置，
    // 因此把它放在几何类型判断外面。
    auto altitudeSymbol =
        vectorStyle.getOrCreate<osgEarth::AltitudeSymbol>();

    // 将矢量要素贴合到地形表面。
    altitudeSymbol->clamping() =
        osgEarth::AltitudeSymbol::CLAMP_TO_TERRAIN;

    altitudeSymbol->technique() =
        osgEarth::AltitudeSymbol::TECHNIQUE_DRAPE;

    // StyleSheet 是样式集合。
    // 当前只有一个 default 样式，未来可以根据属性字段
    // 为不同国家、等级或类别选择不同样式。
    auto countryStyleSheet =
        new osgEarth::StyleSheet();

    countryStyleSheet->addStyle(
        vectorStyle
    );

    // FeatureModelLayer 负责把 FeatureSource 中的矢量要素
    // 转换成可以进入 OSG 场景并被 GPU 绘制的几何对象。
    auto countryLayer =
        new osgEarth::FeatureImageLayer();

    // 设置可见图层名称。
    // 这个名称属于真正负责显示的图层。
    countryLayer->setName(
        "Natural Earth Countries"
    );

    // 告诉可见图层应该从哪个数据源获取 Feature。
    countryLayer->setFeatureSource(
        shapefileSource
    );

    // 告诉可见图层应当使用哪套样式绘制 Feature。
    countryLayer->setStyleSheet(
        countryStyleSheet
    );

    // 将可见矢量图层加入 Map。
    // 这一步会触发 FeatureModelLayer 打开并构建可渲染几何。
    mapNode->getMap()->addLayer(
        countryLayer
    );

    // 检查可见矢量图层是否成功打开。
    // Shapefile 数据源打开成功，不代表样式解析和模型图层
    // 就一定成功，因此这里单独检查 FeatureModelLayer。
    if (countryLayer->getStatus().isError())
    {
        std::cerr
            << "Failed to create country feature layer: "
            << countryLayer->getStatus().toString()
            << std::endl;

        return -1;
    }

    std::cout
        << "Country feature layer created successfully."
        << std::endl;


    HelloEarth::Navigation::InitialViewpointOptions viewpointOptions;

    // 使用倾斜视角观察全球地球场景，
    // 方便后续观察覆盖在地表上的矢量数据。 
    viewpointOptions.pitchDegrees = -45.0;

    auto initialViewpoint = HelloEarth::Navigation::calculateInitialViewpoint(*globalImagery, viewpointOptions);
    
    // 检查初始视点是否有效，如果无效则输出错误信息并退出程序。
    // 检查 optional 里面是否成功产生了 Viewpoint。
    if (!initialViewpoint)
    {
        std::cerr
            << "Failed to calculate initial viewpoint."
            << std::endl;

        return -1;
    }
    
    // mapNode->getMap()->removeLayer(imagery);   

    viewer.setSceneData(mapNode);

    auto manipulator =
        new osgEarth::EarthManipulator(args);

    viewer.setCameraManipulator(manipulator);

    manipulator->setHomeViewpoint(
        *initialViewpoint,
        0.0
    );

    return viewer.run();
}