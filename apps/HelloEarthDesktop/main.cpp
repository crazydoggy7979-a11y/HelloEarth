#include "MainWindow.h"

#include <QApplication>
#include <osgEarth/Common>

#include <gdal_priv.h>

int main(int argc, char* argv[])
{
    // 注册 GDAL 提供的栅格数据驱动。
    //
    // 后续栅格预处理模块和 osgEarth::GDALImageLayer
    // 才能识别并打开 GeoTIFF 等数据。
    GDALAllRegister();

    // 初始化 osgEarth 的全局运行环境。
    //
    // 整个程序只需要执行一次，并且应当在创建地图对象之前执行。
    osgEarth::initialize();
    QApplication application(argc, argv);

    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}