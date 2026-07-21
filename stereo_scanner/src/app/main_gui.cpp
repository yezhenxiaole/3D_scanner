/**
 * @file main.cpp
 * @brief stereo_gui (Qt GUI for StereoScanner) 入口
 *
 * 启动顺序:
 *   1) QApplication + QOpenGLContext (QSurfaceFormat 已在 PointCloudWidget 内设置)
 *   2) 应用全局 QSS 主题
 *   3) 创建 MainWindow, 默认加载 config/default.yaml
 *   4) 启动事件循环
 *
 * 命令行参数:
 *   --config <path>   配置文件 (默认 config/default.yaml)
 *   --cali   <path>   标定文件 (默认 config/cali.yml)
 *   --engine <dir>    TRT 引擎目录 (默认 ffs_engine)
 *   --left-cam <id>   左相机 (默认覆盖 config)
 *   --right-cam <id>  右相机
 *   --no-demo         启动时载入示例点云
 */

#include <QApplication>
#include <QSurfaceFormat>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QDebug>
#include <QTimer>

#include "gui/theme.h"
#include "gui/main_window.h"

namespace {
struct Args {
    QString configFile  = "config/default.yaml";
    QString caliFile    = "config/cali.yml";
    QString engineDir   = "ffs_engine";
    int     leftCam     = -1;
    int     rightCam    = -1;
    bool    noDemo      = false;
};
}  // namespace

static Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if      (arg == "--config")     { if (i+1 < argc) a.configFile = QString::fromLocal8Bit(argv[++i]); }
        else if (arg == "--cali")       { if (i+1 < argc) a.caliFile   = QString::fromLocal8Bit(argv[++i]); }
        else if (arg == "--engine")     { if (i+1 < argc) a.engineDir  = QString::fromLocal8Bit(argv[++i]); }
        else if (arg == "--left-cam")   { if (i+1 < argc) a.leftCam    = QString::fromLocal8Bit(argv[++i]).toInt(); }
        else if (arg == "--right-cam")  { if (i+1 < argc) a.rightCam   = QString::fromLocal8Bit(argv[++i]).toInt(); }
        else if (arg == "--no-demo")    { a.noDemo = true; }
        else if (arg == "-h" || arg == "--help") {
            qDebug().noquote() <<
                "stereo_gui [--config <f>] [--cali <f>] [--engine <d>] "
                "[--left-cam <id>] [--right-cam <id>] [--no-demo]";
            ::exit(0);
        }
    }
    return a;
}

int main(int argc, char** argv) {
    // Qt 高 DPI - Qt5 需显式启用；Qt6 默认开启且属性已废弃
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // 强制使用 Desktop OpenGL 而非 ANGLE (D3D)
    // ANGLE 有 QOpenGLWidget 与兄弟 widget 合成的已知问题 (GL 内容覆盖邻近面板)
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif
    // Qt6 说明: QOpenGLWidget 使用原生 OpenGL (不走 RHI); AA_UseDesktopOpenGL 已废弃,
    // Qt6 在 Windows 默认用 desktop GL, 不再 fallback 到 ANGLE, 故无需显式设置

    // 共享 OpenGL 上下文 (QOpenGLWidget 之间共享)
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("StereoScanner");
    app.setOrganizationName("TeleAgent");
    app.setStyleSheet(ui::globalQss());

    Args args = parseArgs(argc, argv);

    ui::MainWindow w;
    w.setStyleSheet(ui::globalQss());
    w.show();

    // 异步加载配置
    QTimer::singleShot(100, &w, [&w, &args]() {
        w.loadConfig(args.configFile);
    });

    return app.exec();
}
