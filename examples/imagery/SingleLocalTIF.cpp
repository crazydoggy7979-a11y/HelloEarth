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

#include <iostream>
#include <algorithm>
#include <gdal_priv.h>

#include <HelloEarth/raster/RasterPreprocessor.h>
#include <chrono>

int main(int argc, char** argv)
{
    osgEarth::initialize();
    GDALAllRegister();
    std::cout
        << "GDAL version: "
        << GDALVersionInfo("RELEASE_NAME")
        << std::endl;

    osg::ArgumentParser args(&argc, argv);
    osgViewer::Viewer viewer(args);

    // viewer.setThreadingModel(
    //     osgViewer::ViewerBase::SingleThreaded
    // );

    const std::string imagePath ="D:/download/Lianyungang/1_transparent_mosaic_group1.tif";
    std::string preparedImagePath;
    // if (!checkSingleImage(imagePath, preparedImagePath))
    // {
    //     return -1;
    // }

    if (!HelloEarth::Raster::prepareRasterForLoading(imagePath, preparedImagePath))
    {
        return -1;
    }


    // 为现代 OpenGL 环境启用顶点属性别名和矩阵 Uniform。
    viewer.setRealizeOperation(
        new osgEarth::GL3RealizeOperation()
    );
    // 读取tif影像前需要先声明GDALImageLayer类型，setURL告诉其影像路径，之后将其添加到MapNode中，最后设置场景数据为MapNode。
    auto imagery = new osgEarth::GDALImageLayer();
    imagery->setURL(
        preparedImagePath
    );
    // imagery->setMinLevel(8);
    // imagery->setSingleThreaded(true);
    // imagery->setAsyncLoading(true);

    const std::string globalImagePath ="D:/work/projects/HelloEarthWorkspace/testdata/NE1_HR_LC_SR_W/NE1_HR_LC_SR_W.tif";
    std::string preparedGlobalImagePath;
    // if (!checkSingleImage(globalImagePath, preparedGlobalImagePath))
    // {
    //     return -1;
    // }

    if (!HelloEarth::Raster::prepareRasterForLoading(globalImagePath, preparedGlobalImagePath))
    {
        return -1;
    }

    // 读取tif影像前需要先声明GDALImageLayer类型，setURL告诉其影像路径，之后将其添加到MapNode中，最后设置场景数据为MapNode。
    auto globalImagery = new osgEarth::GDALImageLayer();
    globalImagery->setURL(
        preparedGlobalImagePath
    );

    auto imageryTMS = new osgEarth::TMSImageLayer();
    imageryTMS->setURL("https://readymap.org/readymap/tiles/1.0.0/7/");


    auto mapNode = new osgEarth::MapNode();

    mapNode->getMap()->addLayer(imageryTMS);
    // addLayer步骤执行时，会让imagery去打开影像，读取tif影像的信息，之后可以通过imagery->getStatus().isError()来判断是否成功打开影像。
    mapNode->getMap()->addLayer(imagery);

    if (imagery->getStatus().isError()){
        std::cerr
            << "Failed to open image: "
            << imagery->getStatus().toString()
            << std::endl;

        return -1;
    }

    // getDataExtentsUnion用于获取layer中的四至范围并集
    const osgEarth::DataExtent& imageExtent = imagery->getDataExtentsUnion();
    // isValid()用于最低限度的判断extend中的四至范围是否有效，若无效则说明影像没有有效数据范围。
    if (!imageExtent.isValid()){
        std::cerr
            << "The image has no valid data extent."
            << std::endl;

        return -1;
    }
    else{
        std::cout
            << "Image extent:" << std::endl
            << "  X min: " << imageExtent.xMin() << std::endl
            << "  Y min: " << imageExtent.yMin() << std::endl
            << "  X max: " << imageExtent.xMax() << std::endl
            << "  Y max: " << imageExtent.yMax() << std::endl;
    }

    // getCentroid()用于获取extend的中心点坐标，返回值为GeoPoint类型，包含了空间参考系和三维坐标信息。
    const osgEarth::GeoPoint imageCenter=imageExtent.getCentroid();
    std::cout
        << "Image center:" << std::endl
        << "  X: " << imageCenter.x() << std::endl
        << "  Y: " << imageCenter.y() << std::endl
        << "  Z: " << imageCenter.z() << std::endl
        << "  SRS: " << imageCenter.getSRS()->getName()
        << std::endl;

    //因为osgearth的相机范围单位是米，所以相机的范围需要将影像的四至范围转换为米单位，osgEarth提供了Units类来进行单位转换，
    //使用imageExtent.width(osgEarth::Units::METERS)和imageExtent.height(osgEarth::Units::METERS)来获取影像的宽度和高度，单位为米。
    const double widthMeters = imageExtent.width(osgEarth::Units::METERS);
    const double heightMeters = imageExtent.height(osgEarth::Units::METERS);
    std::cout
        << "Image size on the ground:" << std::endl
        << "  Width: " << widthMeters << " meters" << std::endl
        << "  Height: " << heightMeters << " meters"
        << std::endl;
    
    // maxSpanMeters用于获取影像的最大跨度，方便后续设置相机的视野范围。
    const double maxSpanMeters = std::max(widthMeters, heightMeters);
    // cameraRangeMeters用于设置相机的范围，通常设置为最大跨度的两倍，以确保相机能够看到整个影像。
    const double cameraRangeMeters = maxSpanMeters * 2.0;
    std::cout
        << "Estimated camera range: "
        << cameraRangeMeters
        << " meters"
        << std::endl;


    osgEarth::Viewpoint globalViewpoint;

    globalViewpoint.setFocalPoint(
        imageCenter
    );

    globalViewpoint.setHeading(
        osgEarth::Angle(
            0.0,
            osgEarth::Units::DEGREES
        )
    );

    globalViewpoint.setPitch(
        osgEarth::Angle(
            -90.0,
            osgEarth::Units::DEGREES
        )
    );

    globalViewpoint.setRange(
        osgEarth::Distance(
            2500000.0,
            osgEarth::Units::METERS
        )
    );

    // 默认构造出来的是一个空的 Viewpoint，还没有实际观察目标。
    osgEarth::Viewpoint initialViewpoint;
    // initialViewpoint.setFocalPoint(imageCenter);
    initialViewpoint.setFocalPoint(imageCenter);
    // heading 表示相机围绕观察中心的水平方向。这里设置为 0°。垂直俯视时，它主要影响影像在屏幕上的旋转方向。
    initialViewpoint.setHeading(
        osgEarth::Angle(0.0, osgEarth::Units::DEGREES)
    );
    // 设置俯仰角，pitch 表示相机围绕观察中心的垂直方向。这里设置为 -90°，表示相机垂直向下俯视影像。
    initialViewpoint.setPitch(
        osgEarth::Angle(-90.0, osgEarth::Units::DEGREES)
    );
    // 设置观察距离，range 表示相机与观察中心的距离。这里设置为影像最大跨度的两倍，以确保相机能够看到整个影像。
    initialViewpoint.setRange(
        osgEarth::Distance(cameraRangeMeters, osgEarth::Units::METERS)
    );
    // 输出初始视点信息，方便调试和验证设置是否正确。
    std::cout
        << "Initial viewpoint: "
        << initialViewpoint.toString()
        << std::endl;
    
    // 检查初始视点是否有效，如果无效则输出错误信息并退出程序。
    if (!initialViewpoint.isValid())
    {
        std::cerr
            << "Initial viewpoint is invalid."
            << std::endl;

        return -1;
    }
    
    // mapNode->getMap()->removeLayer(imagery);   

    viewer.setSceneData(mapNode);

    auto manipulator =
        new osgEarth::EarthManipulator(args);

    viewer.setCameraManipulator(manipulator);

    manipulator->setHomeViewpoint(
        initialViewpoint,
        0.0
    );

    return viewer.run();

    // // 设置相机操纵器为 EarthManipulator，并将初始视点应用到相机上。
    // // 先给viewer设置一个EarthManipulator，然后使用setViewpoint方法将初始视点应用到相机上，确保程序启动时相机能够正确地观察到影像。
    // auto manipulator = new osgEarth::EarthManipulator(args);
    // viewer.setCameraManipulator(manipulator);
    // manipulator->setHomeViewpoint(
    //     initialViewpoint,
    //     0.0
    // );

    // // 创建窗口和OpenGL环境。
    // // viewer.run()原本也会在内部执行realize。
    // viewer.realize();

    // // Viewer已经准备完成后，先立即进入全球视点。
    // manipulator->setViewpoint(
    //     globalViewpoint,
    //     0.0
    // );

    // // 使用steady_clock记录全球视点开始显示的时间。
    // // steady_clock不会受到系统时间调整的影响。
    // const auto globalViewStartTime =
    //     std::chrono::steady_clock::now();

    // bool localFlightStarted = false;

    // while (!viewer.done())
    // {
    //     if (!localFlightStarted)
    //     {
    //         const auto currentTime =
    //             std::chrono::steady_clock::now();

    //         const double elapsedSeconds =
    //             std::chrono::duration<double>(
    //                 currentTime - globalViewStartTime
    //             ).count();

    //         // 全球视点显示2秒后，只触发一次飞行动画。
    //         if (elapsedSeconds >= 2.0)
    //         {
    //             manipulator->setViewpoint(
    //                 initialViewpoint,
    //                 5.0
    //             );

    //             localFlightStarted = true;
    //         }
    //     }

    //     // 处理事件、更新场景并渲染一帧。
    //     viewer.frame();
    // }

    // return 0;
}