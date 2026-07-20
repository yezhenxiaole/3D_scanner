#pragma once
#include <QMainWindow>
#include <QString>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <memory>

namespace ui {

class PointCloudWidget;
class StereoPreviewStrip;
class RightPanel;
class ScanController;

struct ScanStats;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 主动加载配置 (从命令行传入或默认)
    void loadConfig(const QString& configFile);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    // ---- 布局构建 ----
    void buildTopBar(QWidget* host);
    void buildSidebar(QWidget* host);
    void buildMainArea(QWidget* host);
    void buildBottomBar(QWidget* host);

    // ---- 信号连接 ----
    void wireSignals();

    // ---- 启动时加载示例点云 (无相机也能预览) ----
    void loadDemoPointCloud();

    // ---- 控件指针 ----
    PointCloudWidget*    pcw_;
    StereoPreviewStrip*  preview_;
    RightPanel*          right_;
    ScanController*      controller_;

    // ---- 顶栏 HUD 元素 ----
    QLabel*    brandName_;
    QLabel*    brandModel_;
    QLabel*    camPosVal_;
    QLabel*    zoomVal_;
    QLabel*    renderFpsVal_;
    QLabel*    clockLabel_;
    QLabel*    gpuVal_;
    QLabel*    vramVal_;
    QLabel*    tempVal_;
    QLabel*    trtVal_;

    // ---- 侧栏导航 ----
    QPushButton* navScan_;
    QPushButton* navCali_;
    QPushButton* navExport_;
    QPushButton* navLogs_;
    QPushButton* navSettings_;
    QPushButton* navAbout_;

    // ---- 工作流导航 (顶栏下沿) ----
    QLabel*    wfLabel_[6];

    // ---- 底栏统计 ----
    QLabel*    botFpsVal_;
    QLabel*    botFramesVal_;
    QLabel*    botLoopsVal_;
    QLabel*    botPtsVal_;
    QSlider*   botProgress_;
    QLabel*    botProgressPct_;

    // ---- 配置文件路径 ----
    QString configPath_;
};

} // namespace ui
