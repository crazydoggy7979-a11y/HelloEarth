#include "EarthViewWidget.h"
#include "MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTreeWidget>
#include <QWidget>
#include <QString>
#include <QTreeWidgetItem>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent){
    resize(1200, 800);
    setWindowTitle("HelloEarth Desktop");

    // 创建菜单栏。
    auto* fileMenu = menuBar()->addMenu("File");

    auto* openImageryAction =
        fileMenu->addAction("Open Imagery...");

    auto* openElevationAction =
        fileMenu->addAction("Open DEM...");

    fileMenu->addSeparator();

    auto* exitAction =
        fileMenu->addAction("Exit");

    connect(
        exitAction,
        &QAction::triggered,
        this,
        &QWidget::close
    );

    // 创建中央 OpenGL 三维显示控件。
    //
    // EarthViewWidget 本身属于 QWidget 体系，
    // 因此可以直接安装为 MainWindow 的中央控件。
    earthViewWidget_ = new EarthViewWidget(this);

    // 允许用户点击三维区域后，将键盘焦点交给它。
    // 后续键盘和快捷键事件才能传入三维视图。
    earthViewWidget_->setFocusPolicy(
        Qt::StrongFocus
    );

    // MainWindow 接管 EarthViewWidget 的界面布局和生命周期。
    setCentralWidget(earthViewWidget_);

    // 创建左侧图层面板。
    auto* layerDock = new QDockWidget("Layers", this);
    layerDock->setObjectName("LayerDock");

    layerTree_ = new QTreeWidget(layerDock);
    layerTree_->setHeaderLabel("Layers");

    layerDock->setWidget(layerTree_);

    // layerDock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    addDockWidget(
        Qt::LeftDockWidgetArea,
        layerDock
    );

    // 创建状态栏。
    statusBar()->showMessage("Ready");
}