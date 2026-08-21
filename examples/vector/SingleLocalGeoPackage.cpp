#include <osgEarth/MapNode>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GLUtils>
#include <osgEarth/GDAL>
#include <osgEarth/OGRFeatureSource>
#include <osgEarth/FeatureImageLayer>
#include <osgEarth/Style>
#include <osgEarth/StyleSheet>
#include <osgEarth/Units>

#include <osg/ArgumentParser>
#include <osgViewer/Viewer>

#include <HelloEarth/raster/RasterPreprocessor.h>
#include <HelloEarth/navigation/ViewpointCalculator.h>

#include <gdal_priv.h>
#include <cpl_error.h>
#include <ogrsf_frmts.h>

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    // 初始化 osgEarth 的全局运行环境。
    osgEarth::initialize();

    // 注册 GDAL 驱动。
    GDALAllRegister();

    // 解析传给程序的命令行参数。
    osg::ArgumentParser args(
        &argc,
        argv
    );

    // 创建独立的 OSG Viewer。
    osgViewer::Viewer viewer(
        args
    );

    // 在图形上下文创建时配置 osgEarth 所需的 GL3 环境。
    viewer.setRealizeOperation(
        new osgEarth::GL3RealizeOperation()
    );

    // 当前测试使用的本地 GeoPackage 文件路径。
    const std::string geoPackagePath =
        "D:/work/projects/HelloEarthWorkspace/testdata/SHP/"
        "gadm41_AFG.gpkg";

    // 以“只读矢量数据源”的方式打开 GeoPackage。
    //
    // GDAL_OF_VECTOR：
    //     告诉 GDAL 当前只需要打开矢量内容。
    //
    // GDAL_OF_READONLY：
    //     以只读模式打开，避免当前学习阶段意外修改测试数据。
    //
    // 后面的三个 nullptr 分别表示：
    //     不限制允许使用的驱动；
    //     不设置额外打开选项；
    //     不传递兄弟文件列表。
    GDALDataset* geoPackageDataset =
        static_cast<GDALDataset*>(
            GDALOpenEx(
                geoPackagePath.c_str(),
                GDAL_OF_VECTOR | GDAL_OF_READONLY,
                nullptr,
                nullptr,
                nullptr
            )
        );

    // GDALOpenEx() 打开失败时返回 nullptr。
    if (geoPackageDataset == nullptr)
    {
        std::cerr
            << "Failed to open GeoPackage: "
            << geoPackagePath
            << std::endl
            << "GDAL error: "
            << CPLGetLastErrorMsg()
            << std::endl;

        return -1;
    }

    std::cout
        << "GeoPackage opened successfully."
        << std::endl;

    // 获取实际负责打开该数据集的 GDAL 驱动。
    //
    // 对于正确的 GeoPackage，驱动描述通常应当是 GPKG。
    GDALDriver* geoPackageDriver =
        geoPackageDataset->GetDriver();

    if (geoPackageDriver == nullptr)
    {
        std::cerr
            << "Failed to obtain the GeoPackage driver."
            << std::endl;

        GDALClose(geoPackageDataset);
        return -1;
    }

    std::cout
        << "Driver: "
        << geoPackageDriver->GetDescription()
        << std::endl;

    // 获取 GeoPackage 中 GDAL 能够识别的图层数量。
    //
    // 这里返回的是数据库内部图层的数量，
    // 不是磁盘上的文件数量，也不是要素数量。
    const int layerCount =
        geoPackageDataset->GetLayerCount();

    std::cout
        << "Layer count: "
        << layerCount
        << std::endl;

    if (layerCount <= 0)
    {
        std::cerr
            << "The GeoPackage contains no readable vector layers."
            << std::endl;

        GDALClose(geoPackageDataset);
        return -1;
    }

    // 按照 GeoPackage 数据集中的图层索引逐个读取图层。
    //
    // 图层索引从 0 开始。
    // 如果 layerCount 等于 3，有效索引就是 0、1、2。
    for (
        int layerIndex = 0;
        layerIndex < layerCount;
        ++layerIndex
    )
    {
        // 根据索引获取一个内部图层。
        //
        // GetLayer() 返回的 OGRLayer 对象仍然由 GDALDataset 管理，
        // 当前代码只借用这个指针，不需要也不能手动 delete。
        OGRLayer* layer =
            geoPackageDataset->GetLayer(layerIndex);

        if (layer == nullptr)
        {
            std::cerr
                << "Failed to obtain layer at index "
                << layerIndex
                << "."
                << std::endl;

            continue;
        }

        // 获取 GeoPackage 内部的真实图层名称。
        //
        // 这个名称后面会传给：
        // OGRFeatureSource::setLayer(layerName)
        const char* layerName =
            layer->GetName();

        if (layerName == nullptr)
        {
            std::cerr
                << "Layer at index "
                << layerIndex
                << " has no valid name."
                << std::endl;

            continue;
        }

        // 获取该图层的结构定义。
        //
        // OGRFeatureDefn 描述的是整个图层的“结构”，例如：
        // - 几何类型；
        // - 属性字段数量；
        // - 每个属性字段的名称和类型。
        //
        // 它不是某一个具体 Feature，而是所有 Feature 共同遵守的结构。
        OGRFeatureDefn* layerDefinition =
            layer->GetLayerDefn();

        if (layerDefinition == nullptr)
        {
            std::cerr
                << "Failed to obtain the definition of layer: "
                << layerName
                << std::endl;

            continue;
        }

        // 获取该图层声明的几何类型。
        //
        // 返回值属于 OGRwkbGeometryType 枚举，
        // 例如 wkbPoint、wkbLineString、wkbPolygon。
        const OGRwkbGeometryType geometryType =
            layerDefinition->GetGeomType();

        // 将几何类型枚举转换成便于阅读的文字。
        const char* geometryTypeName =
            OGRGeometryTypeToName(
                geometryType
            );

        // 获取图层中的要素数量。
        //
        // true 表示如果驱动无法直接从元数据中得到数量，
        // 允许 GDAL 执行必要的统计。
        // GeoPackage 通常可以比较高效地取得这个数值。
        const GIntBig featureCount =
            layer->GetFeatureCount(
                true
            );

        std::cout
            << "Layer "
            << layerIndex
            << ":"
            << std::endl
            << "  Name: "
            << layerName
            << std::endl
            << "  Geometry type: "
            << (
                geometryTypeName != nullptr
                    ? geometryTypeName
                    : "Unknown"
            )
            << std::endl
            << "  Feature count: "
            << featureCount
            << std::endl;
    }

    // 指定当前准备继续研究的目标图层名称。
    //
    // 这里选择 ADM_ADM_1，也就是一级行政区图层。
    const std::string targetLayerName =
        "ADM_ADM_1";

    // 使用名称从 GeoPackage 中精确查找目标图层。
    //
    // 相比 GetLayer(1)，按名称查找不依赖图层排列顺序，
    // 更适合未来保存项目和重新加载数据。
    OGRLayer* targetLayer =
        geoPackageDataset->GetLayerByName(
            targetLayerName.c_str()
        );

    if (targetLayer == nullptr)
    {
        std::cerr
            << "Failed to find the target layer: "
            << targetLayerName
            << std::endl;

        GDALClose(geoPackageDataset);
        return -1;
    }

    std::cout
        << "Target layer selected successfully: "
        << targetLayerName
        << std::endl;

    // 获取目标图层声明的空间参考系统。
    //
    // 这个对象仍然由 GDAL 管理，
    // 我们只读取它，不需要手动 delete。
    const OGRSpatialReference* spatialReference =
        targetLayer->GetSpatialRef();

    if (spatialReference == nullptr)
    {
        std::cerr
            << "The target layer has no spatial reference."
            << std::endl;

        GDALClose(geoPackageDataset);
        return -1;
    }

    // 获取空间参考的可读名称。
    const char* spatialReferenceName =
        spatialReference->GetName();

    // 尝试获取空间参考的权威机构和编号。
    //
    // 常见结果是：
    // Authority: EPSG
    // Code: 4326
    const char* authorityName =
        spatialReference->GetAuthorityName(
            nullptr
        );

    const char* authorityCode =
        spatialReference->GetAuthorityCode(
            nullptr
        );

    std::cout
        << "Target layer spatial reference:"
        << std::endl
        << "  Name: "
        << (
            spatialReferenceName != nullptr
                ? spatialReferenceName
                : "Unknown"
        )
        << std::endl
        << "  Authority: "
        << (
            authorityName != nullptr
                ? authorityName
                : "Unknown"
        )
        << std::endl
        << "  Code: "
        << (
            authorityCode != nullptr
                ? authorityCode
                : "Unknown"
        )
        << std::endl;

    // OGREnvelope 用于保存二维包围范围。
    //
    // 它只记录最小和最大 X/Y，
    // 不保存复杂的行政区边界形状。
    OGREnvelope targetExtent;

    // 读取整个目标图层的总体范围。
    //
    // true 表示如果没有可直接使用的范围元数据，
    // 允许 GDAL 必要时遍历要素计算范围。
    const OGRErr extentResult =
        targetLayer->GetExtent(
            &targetExtent,
            true
        );

    if (extentResult != OGRERR_NONE)
    {
        std::cerr
            << "Failed to calculate the target layer extent."
            << std::endl;

        GDALClose(geoPackageDataset);
        return -1;
    }

    std::cout
        << "Target layer extent:"
        << std::endl
        << "  X min: "
        << targetExtent.MinX
        << std::endl
        << "  Y min: "
        << targetExtent.MinY
        << std::endl
        << "  X max: "
        << targetExtent.MaxX
        << std::endl
        << "  Y max: "
        << targetExtent.MaxY
        << std::endl;

    // 关闭 GeoPackage 数据集并释放 GDAL 持有的文件资源。
    //
    // GDALOpenEx() 成功返回的对象，
    // 最终必须使用 GDALClose() 关闭。
    GDALClose(geoPackageDataset);

    geoPackageDataset = nullptr;

    // 创建当前场景使用的 MapNode。
    //
    // MapNode 是 osgEarth 地图内容进入 OSG 场景图的根节点。
    auto mapNode =
        new osgEarth::MapNode();

    // 本地全球底图路径。
    const std::string globalImagePath =
        "D:/work/projects/HelloEarthWorkspace/testdata/"
        "NE1_HR_LC_SR_W/NE1_HR_LC_SR_W.tif";

    // prepareRasterForLoading() 会执行现有的栅格预检查流程，
    // 并根据金字塔和 VRT 情况返回最终应该交给 osgEarth 的路径。
    std::string preparedGlobalImagePath;

    if (!HelloEarth::Raster::prepareRasterForLoading(
            globalImagePath,
            preparedGlobalImagePath
        ))
    {
        std::cerr
            << "Failed to prepare global imagery."
            << std::endl;

        return -1;
    }

    // 创建 GDAL 影像图层。
    auto globalImagery =
        new osgEarth::GDALImageLayer();

    // 设置经过预处理后真正需要加载的影像路径。
    globalImagery->setURL(
        preparedGlobalImagePath
    );

    // 将全球影像加入 Map。
    //
    // addLayer() 会触发图层初始化和数据源打开。
    mapNode->getMap()->addLayer(
        globalImagery
    );

    // 检查 osgEarth 是否成功打开全球影像。
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

    // 使用公共导航模块计算适合观察全球底图的视点。
    HelloEarth::Navigation::InitialViewpointOptions
        viewpointOptions;

    // 使用倾斜视角观察地球。
    viewpointOptions.pitchDegrees =
        -45.0;

    // 创建用于读取 GeoPackage 矢量图层的数据源。
    //
    // OGRFeatureSource 可以通过 GDAL/OGR 读取多种矢量格式，
    // 包括 Shapefile、GeoJSON 和 GeoPackage。
    //
    // 它当前只负责提供 Feature，尚不负责可视化渲染。
    auto geoPackageFeatureSource =
        new osgEarth::OGRFeatureSource();

    // 设置 osgEarth 内部的数据源名称。
    //
    // 这个名称用于程序内部识别，
    // 不会修改 GeoPackage 数据库中的真实图层名称。
    geoPackageFeatureSource->setName(
        targetLayerName
    );

    // URL 指向 GeoPackage 数据库文件。
    geoPackageFeatureSource->setURL(
        geoPackagePath
    );

    // Layer 指定 GeoPackage 内部需要打开的图层。
    //
    // 当前选择的是 ADM_ADM_1，即一级行政区图层。
    geoPackageFeatureSource->setLayer(
        targetLayerName
    );

    // 将 FeatureSource 加入 Map。
    //
    // 这一步会触发 OGRFeatureSource 初始化，
    // 并根据 URL 和 Layer 打开目标矢量图层。
    //
    // FeatureSource 本身不会生成可见的点、线或面，
    // 所以加入以后 Viewer 中暂时仍然只显示全球底图。
    mapNode->getMap()->addLayer(
        geoPackageFeatureSource
    );

    // 检查 osgEarth 是否成功打开指定的内部图层。
    if (geoPackageFeatureSource->getStatus().isError())
    {
        std::cerr
            << "Failed to open GeoPackage feature layer: "
            << geoPackageFeatureSource->getStatus().toString()
            << std::endl;

        return -1;
    }

    std::cout
        << "GeoPackage feature source opened successfully."
        << std::endl;

    // 再通过 osgEarth 输出一次要素数量。
    //
    // 前面 GDAL 直接读取时得到的数量是34。
    // 如果这里同样得到34，说明 osgEarth 的 OGRFeatureSource
    // 确实打开了相同的 ADM_ADM_1 图层。
    std::cout
        << "osgEarth feature count: "
        << geoPackageFeatureSource->getFeatureCount()
        << std::endl;

    // 创建当前矢量图层独立使用的默认样式。
    //
    // 这套样式会同时包含点、线、面和贴地参数。
    // osgEarth 会根据实际几何类型使用其中相关的 Symbol。
    osgEarth::Style defaultVectorStyle;

    defaultVectorStyle.setName(
        "default"
    );

    // 当前先使用固定的基础颜色验证渲染流程。
    //
    // 未来 App 中可以根据图层唯一标识生成稳定的随机颜色。
    const osgEarth::Color baseColor(
        0.85F,
        0.25F,
        0.35F,
        1.00F
    );

    // // 创建点要素的默认显示参数。
    // auto pointSymbol =
    //     defaultVectorStyle.getOrCreate<
    //         osgEarth::PointSymbol
    //     >();

    // pointSymbol->fill().mutable_value().color() =
    //     baseColor;

    // pointSymbol->size().mutable_value() =
    //     8.0F;

    // pointSymbol->smooth().mutable_value() =
    //     true;

    // 创建线要素的默认显示参数。
    //
    // 对于面图层，LineSymbol 也会用于绘制面的轮廓线。
    auto lineSymbol =
        defaultVectorStyle.getOrCreate<
            osgEarth::LineSymbol
        >();

    lineSymbol->stroke().mutable_value().color() =
        baseColor;

    lineSymbol->stroke().mutable_value().width() =
        osgEarth::Distance(
            2.0,
            osgEarth::Units::PIXELS
        );

    // 将跨度较大的线段进行细分，
    // 使其更自然地贴合地球曲面。
    lineSymbol->tessellationSize() =
        osgEarth::Distance(
            25.0,
            osgEarth::Units::KILOMETERS
        );

    // 创建面要素的默认填充参数。
    auto polygonSymbol =
        defaultVectorStyle.getOrCreate<
            osgEarth::PolygonSymbol
        >();

    // 面填充继续使用基础颜色的 RGB，
    // 但将透明度降低为35%，以便看到下面的全球影像。
    polygonSymbol->fill().mutable_value().color() =
        osgEarth::Color(
            baseColor.r(),
            baseColor.g(),
            baseColor.b(),
            0.35F
        );

    // 创建点、线、面共同使用的高度规则。
    auto altitudeSymbol =
        defaultVectorStyle.getOrCreate<
            osgEarth::AltitudeSymbol
        >();

    // 将矢量几何贴合到地形表面。
    altitudeSymbol->clamping() =
        osgEarth::AltitudeSymbol::CLAMP_TO_TERRAIN;

    // 使用铺设方式将矢量内容覆盖到地表。
    altitudeSymbol->technique() =
        osgEarth::AltitudeSymbol::TECHNIQUE_DRAPE;

    // 每个可见矢量图层都创建自己的 StyleSheet。
    //
    // 未来用户修改当前图层样式时，
    // 不会影响其他矢量图层。
    auto defaultStyleSheet =
        new osgEarth::StyleSheet();

    defaultStyleSheet->addStyle(
        defaultVectorStyle
    );

    // 创建真正负责可视化显示的矢量图层。
    //
    // FeatureImageLayer 会根据 FeatureSource 和 StyleSheet，
    // 将矢量内容栅格化成地表图像进行显示。
    auto geoPackageImageLayer =
        new osgEarth::FeatureImageLayer();

    // 设置可见图层名称。
    geoPackageImageLayer->setName(
        targetLayerName
    );

    // 指定图层的数据来源。
    geoPackageImageLayer->setFeatureSource(
        geoPackageFeatureSource
    );

    // 指定图层的默认显示样式。
    geoPackageImageLayer->setStyleSheet(
        defaultStyleSheet
    );

    // 将可见图层加入 Map。
    mapNode->getMap()->addLayer(
        geoPackageImageLayer
    );

    // 检查可见图层是否成功建立。
    if (geoPackageImageLayer->getStatus().isError())
    {
        std::cerr
            << "Failed to create GeoPackage image layer: "
            << geoPackageImageLayer->getStatus().toString()
            << std::endl;

        return -1;
    }

    std::cout
        << "GeoPackage FeatureImageLayer created successfully."
        << std::endl;

    auto initialViewpoint =
        HelloEarth::Navigation::calculateInitialViewpoint(
            *geoPackageImageLayer,
            viewpointOptions
        );

    if (!initialViewpoint)
    {
        std::cerr
            << "Failed to calculate the global viewpoint."
            << std::endl;

        return -1;
    }

    // 将 MapNode 设置为 Viewer 的场景根节点。
    viewer.setSceneData(
        mapNode
    );

    // 创建 EarthManipulator，提供地球场景的鼠标交互。
    auto manipulator =
        new osgEarth::EarthManipulator(
            args
        );

    viewer.setCameraManipulator(
        manipulator
    );

    // 将全球底图视点设置为 Home 视点。
    // Viewer 启动时会以这个视角观察地球。
    manipulator->setHomeViewpoint(
        *initialViewpoint,
        0.0
    );

    // 进入 Viewer 渲染循环。
    return viewer.run();
}