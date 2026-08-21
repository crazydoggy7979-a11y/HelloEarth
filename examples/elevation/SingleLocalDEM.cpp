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

    const std::string demPath =
        "D:/download/rasters_COP30/output_hh.tif";

    auto elevation =
        new osgEarth::GDALElevationLayer();

    elevation->setURL(demPath);


    auto mapNode =
        new osgEarth::MapNode();

    mapNode->getMap()->addLayer(elevation);

    if (elevation->getStatus().isError())
    {
        std::cerr
            << "Failed to open DEM: "
            << elevation->getStatus().toString()
            << std::endl;

        return -1;
    }

    std::cout
        << "DEM opened successfully."
        << std::endl;

    // const std::string globalImagePath =
    //     "D:/work/projects/HelloEarthWorkspace/testdata/"
    //     "NE1_HR_LC_SR_W/NE1_HR_LC_SR_W.tif";

    // std::string prepareGloballobalImagePath;

    // if (!HelloEarth::Raster::prepareRasterForLoading(globalImagePath, prepareGloballobalImagePath))
    // {
    //     return -1;
    // }

    // auto globalImagery =
    //     new osgEarth::GDALImageLayer();

    // globalImagery->setURL(prepareGloballobalImagePath);
    auto globalImagery = new osgEarth::TMSImageLayer();
    globalImagery->setURL("https://readymap.org/readymap/tiles/1.0.0/7/");
    
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

    const std::string imagePath ="D:/work/projects/HelloEarthWorkspace/testdata/ref.tif";
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

    // mapNode->getMap()->addLayer(imagery);

    // if (imagery->getStatus().isError()){
    //     std::cerr
    //         << "Failed to open image: "
    //         << imagery->getStatus().toString()
    //         << std::endl;

    //     return -1;
    // }

    HelloEarth::Navigation::InitialViewpointOptions viewpointOptions;

    // DEM 使用倾斜视角。
    viewpointOptions.pitchDegrees = -45.0;

    auto initialViewpoint = HelloEarth::Navigation::calculateInitialViewpoint(*elevation, viewpointOptions);
    
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