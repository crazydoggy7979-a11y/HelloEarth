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

    // 创建国家面要素使用的显示样式。
    // Style 本质上是一组 Symbol 的集合，
    // 每个 Symbol 分别控制填充、边线、贴地等显示属性。
    osgEarth::Style countryStyle;

    // 将样式命名为 default。
    // 当前所有国家要素都使用同一套默认样式。
    countryStyle.setName(
        "default"
    );

    // PolygonSymbol 控制面要素内部的填充方式。
    auto polygonSymbol =
        countryStyle.getOrCreate<osgEarth::PolygonSymbol>();

    // 设置国家面的填充颜色。
    // Color 的四个参数依次是 R、G、B、A，取值范围为 0～1。
    // 最后的 0.30 表示 30% 不透明，使底部全球影像仍然可见。
    polygonSymbol->fill().mutable_value().color() =
        osgEarth::Color(
            0.15F,
            0.55F,
            0.95F,
            0.30F
        );

    // LineSymbol 控制面要素的外轮廓。
    // PolygonSymbol 默认允许使用同一个 Style 中的 LineSymbol
    // 作为多边形边界线。
    auto lineSymbol =
        countryStyle.getOrCreate<osgEarth::LineSymbol>();

    // 设置国家边界线颜色。
    lineSymbol->stroke().mutable_value().color() =
        osgEarth::Color::Yellow;

    // 设置边界线在屏幕上的宽度。
    // PIXELS 表示无论相机距离怎样变化，
    // 基础线宽都按照屏幕像素理解。
    lineSymbol->stroke().mutable_value().width() =
        osgEarth::Distance(
            10,
            osgEarth::Units::PIXELS
        );

    // 将过长的边界线段进一步细分。
    // 因为地球表面是曲面，如果线段跨度非常大，
    // 细分后更容易沿着地球曲面正确显示。
    lineSymbol->tessellationSize() =
        osgEarth::Distance(
            100.0,
            osgEarth::Units::KILOMETERS
        );

    // AltitudeSymbol 控制矢量几何与地表之间的高度关系。
    auto altitudeSymbol =
        countryStyle.getOrCreate<osgEarth::AltitudeSymbol>();

    // 表示要素没有独立高程时，贴合当前地形表面。
    altitudeSymbol->clamping() =
        osgEarth::AltitudeSymbol::CLAMP_TO_TERRAIN;

    // DRAPE 表示将矢量图形投影、铺设到地形表面。
    // 对行政区面、道路、土地利用范围等地表数据很适合。
    altitudeSymbol->technique() =
        osgEarth::AltitudeSymbol::TECHNIQUE_DRAPE;

    // StyleSheet 是样式集合。
    // 当前只有一个 default 样式，未来可以根据属性字段
    // 为不同国家、等级或类别选择不同样式。
    auto countryStyleSheet =
        new osgEarth::StyleSheet();

    countryStyleSheet->addStyle(
        countryStyle
    );

    // FeatureModelLayer 负责把 FeatureSource 中的矢量要素
    // 转换成可以进入 OSG 场景并被 GPU 绘制的几何对象。
    auto countryLayer =
        new osgEarth::FeatureModelLayer();

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