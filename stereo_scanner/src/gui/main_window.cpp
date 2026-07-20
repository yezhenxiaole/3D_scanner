#define _USE_MATH_DEFINES
#include "main_window.h"
#include "theme.h"
#include "pointcloud_widget.h"
#include "stereo_preview_widget.h"
#include "right_panel.h"
#include "scan_controller.h"

#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QSlider>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <cmath>
#include <random>
#include <chrono>

namespace ui {

// ============================================================================
// 构造 / 析构
// ============================================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      pcw_(nullptr), preview_(nullptr), right_(nullptr), controller_(nullptr),
      brandName_(nullptr), brandModel_(nullptr),
      camPosVal_(nullptr), zoomVal_(nullptr), renderFpsVal_(nullptr),
      clockLabel_(nullptr), gpuVal_(nullptr), vramVal_(nullptr),
      tempVal_(nullptr), trtVal_(nullptr),
      navScan_(nullptr), navCali_(nullptr), navExport_(nullptr),
      navLogs_(nullptr), navSettings_(nullptr), navAbout_(nullptr),
      botFpsVal_(nullptr), botFramesVal_(nullptr), botLoopsVal_(nullptr),
      botPtsVal_(nullptr), botProgress_(nullptr), botProgressPct_(nullptr)
{
    setObjectName("MainWindow");
    setWindowTitle("Stereo Scanner - Fast-FoundationStereo + RTAB-Map");
    resize(1440, 900);
    setMinimumSize(1200, 720);

    // ---- 根容器: 5-region 网格 (top/side/main/right/bottom) ----
    auto* central = new QWidget(this);
    central->setObjectName("MainWindow");
    setCentralWidget(central);

    auto* grid = new QGridLayout(central);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    // 主网格
    //   row 0: top 56px
    //   row 1: main (1fr 分配)
    //   row 2: bottom 48px
    //   col 0: side 64px
    //   col 1: main 1fr
    //   col 2: right 336px
    grid->setRowMinimumHeight(0, 56);
    grid->setRowMinimumHeight(2, 48);
    grid->setRowStretch(0, 0);
    grid->setRowStretch(1, 1);
    grid->setRowStretch(2, 0);
    grid->setColumnMinimumWidth(0, 64);
    grid->setColumnMinimumWidth(2, 336);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 0);

    // ---- top bar ----
    auto* topHost = new QWidget;
    topHost->setObjectName("TopBar");
    buildTopBar(topHost);
    grid->addWidget(topHost, 0, 0, 1, 3);

    // ---- side bar ----
    auto* sideHost = new QWidget;
    sideHost->setObjectName("SideBar");
    buildSidebar(sideHost);
    grid->addWidget(sideHost, 1, 0);

    // ---- main area ----
    auto* mainHost = new QWidget;
    mainHost->setObjectName("MainArea");
    buildMainArea(mainHost);
    grid->addWidget(mainHost, 1, 1);

    // ---- right panel ----
    right_ = new RightPanel;
    right_->setMinimumWidth(336);   // hard guarantee 336px (defensive)
    // FIX: QOpenGLWidget 在 Qt5/Win 下会创建 native 子窗口覆盖兄弟 widget。
    // 将 right_ 设为 native window 并 raise() 以保证它绘制在 GL widget 之上。
    right_->setAttribute(Qt::WA_NativeWindow);
    grid->addWidget(right_, 1, 2);
    right_->raise();

    // ---- bottom bar ----
    auto* botHost = new QWidget;
    botHost->setObjectName("BottomBar");
    buildBottomBar(botHost);
    grid->addWidget(botHost, 2, 0, 1, 3);

    // ---- 控制器 ----
    controller_ = new ScanController(this);
    wireSignals();

    // ---- 启动时载入示例点云 ----
    QTimer::singleShot(300, this, [this]() {
        loadDemoPointCloud();
    });

    // 确保 right_ 在窗口显示后仍保持在 GL widget 之上 (防御性)
    // GL widget 使用 native container 裁剪, 但 raise() 作为双重保险
    QTimer::singleShot(500, this, [this]() {
        if (right_) { right_->raise(); right_->update(); }
    });

    // 时钟 & HUD 更新
    auto* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this]() {
        if (clockLabel_) {
            clockLabel_->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd  hh:mm:ss"));
        }
    });
    clockTimer->start(1000);

    // GPU 状态模拟 (实际可由 NVML 接入, 此处只做静态展示)
    if (gpuVal_) gpuVal_->setText("RTX 5060");
    if (vramVal_) vramVal_->setText("5.8/16 GB");
    if (tempVal_) tempVal_->setText("52C");
    if (trtVal_) trtVal_->setText("10.13");
}

MainWindow::~MainWindow() {
    if (controller_) {
        controller_->stopScan();
        controller_->wait(2000);
    }
}

// ============================================================================
// TopBar: 品牌 + 系统监控 + 时钟 + 工作流导航
// ============================================================================
void MainWindow::buildTopBar(QWidget* host) {
    auto* root = new QVBoxLayout(host);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 上半: brand | spacer | system monitor | clock ----
    auto* upper = new QWidget;
    upper->setMinimumHeight(40);
    upper->setMaximumHeight(40);
    auto* ul = new QHBoxLayout(upper);
    ul->setContentsMargins(16, 4, 16, 4);
    ul->setSpacing(16);

    // Brand 块
    auto* brandHost = new QWidget;
    auto* bl = new QHBoxLayout(brandHost);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(8);
    QLabel* logo = new QLabel(QString::fromUtf8("\xE2\x97\x86"));
    logo->setStyleSheet("color:#00D4FF; font-size:18px;");
    bl->addWidget(logo);
    brandName_ = new QLabel("StereoScanner");
    brandName_->setObjectName("BrandName");
    bl->addWidget(brandName_);
    brandModel_ = new QLabel("FFS-L  |  RTAB-Map  |  TRT-FP16");
    brandModel_->setObjectName("BrandModel");
    bl->addWidget(brandModel_);
    bl->addStretch();
    ul->addWidget(brandHost);

    ul->addStretch();

    // 系统监控 (GPU / VRAM / Temp / TRT)
    auto mkSys = [this](const QString& label, QLabel*& valOut) {
        auto* host = new QWidget;
        auto* l = new QHBoxLayout(host);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(4);
        auto* labL = new QLabel(label);
        labL->setObjectName("SysLabel");
        l->addWidget(labL);
        valOut = new QLabel("--");
        valOut->setObjectName("SysValue");
        valOut->setProperty("state", "green");
        l->addWidget(valOut);
        return host;
    };

    ul->addWidget(mkSys("GPU",    gpuVal_));
    ul->addWidget(mkSys("VRAM",   vramVal_));
    ul->addWidget(mkSys("TEMP",   tempVal_));
    ul->addWidget(mkSys("TRT",    trtVal_));

    ul->addSpacing(20);

    // 时钟
    clockLabel_ = new QLabel(QDateTime::currentDateTime().toString("yyyy-MM-dd  hh:mm:ss"));
    clockLabel_->setObjectName("Clock");
    ul->addWidget(clockLabel_);

    root->addWidget(upper);
}

// ============================================================================
// SideBar: 6 个 nav 按钮
// ============================================================================
static QPushButton* mkNavBtn(const QString& glyph, const QString& text) {
    auto* b = new QPushButton;
    b->setObjectName("NavBtn");
    b->setCheckable(true);
    b->setToolTip(text);
    auto* l = new QVBoxLayout(b);
    l->setContentsMargins(0, 6, 0, 6);
    l->setSpacing(2);
    auto* icon = new QLabel(glyph);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size:18px; color:#9BA5B1;");
    auto* lab = new QLabel(text);
    lab->setObjectName("NavLabel");
    lab->setAlignment(Qt::AlignCenter);
    l->addWidget(icon);
    l->addWidget(lab);
    return b;
}

void MainWindow::buildSidebar(QWidget* host) {
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(0, 12, 0, 12);
    v->setSpacing(4);
    v->setAlignment(Qt::AlignTop);

    navScan_     = mkNavBtn(QString::fromUtf8("\xE2\x97\x89"), "Scan");
    navCali_     = mkNavBtn(QString::fromUtf8("\xE2\x97\xBB"), "Cali");
    navExport_   = mkNavBtn(QString::fromUtf8("\xE2\x86\x93"), "Export");
    navLogs_     = mkNavBtn(QString::fromUtf8("\xE2\x89\xA1"), "Logs");
    navSettings_ = mkNavBtn(QString::fromUtf8("\xE2\x9A\x99"), "Setup");
    navAbout_    = mkNavBtn(QString::fromUtf8("\xE2\x84\xB9"), "About");

    navScan_->setChecked(true);

    for (auto* b : { navScan_, navCali_, navExport_, navLogs_, navSettings_, navAbout_ }) {
        v->addWidget(b);
    }

    v->addStretch();
}

// ============================================================================
// MainArea: 中央 3D 点云 + 工作流导航 + 底部三窗格预览
// ============================================================================
void MainWindow::buildMainArea(QWidget* host) {
    auto* root = new QVBoxLayout(host);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ---- 工作流导航 (顶部) ----
    auto* wfHost = new QWidget;
    wfHost->setMaximumHeight(48);
    auto* wfl = new QHBoxLayout(wfHost);
    wfl->setContentsMargins(8, 4, 8, 4);
    wfl->setSpacing(8);

    const QStringList wfNames = {"CALIBRATE", "CONNECT", "PREVIEW",
                                 "SCAN",      "OPTIMIZE","EXPORT"};
    auto* wfGroup = new QWidget;
    auto* wg = new QHBoxLayout(wfGroup);
    wg->setContentsMargins(0, 0, 0, 0);
    wg->setSpacing(4);
    for (int i = 0; i < 6; ++i) {
        auto* num = new QLabel(QString::number(i + 1));
        num->setObjectName("WfNum");
        num->setAlignment(Qt::AlignCenter);
        num->setProperty("state", i == 0 ? "active" : "pending");
        wg->addWidget(num);

        auto* t = new QLabel(wfNames[i]);
        t->setObjectName("WfStepPending");
        if (i == 0) t->setObjectName("WfStepActive");
        wg->addWidget(t);
        wfLabel_[i] = t;

        if (i < 5) {
            auto* arr = new QLabel(QString::fromUtf8("\xE2\x86\x92"));
            arr->setObjectName("WfArrow");
            wg->addWidget(arr);
        }
    }
    wfl->addWidget(wfGroup);
    wfl->addStretch();

    // ---- HUD: cam pos / zoom / render fps ----
    auto mkHud = [](const QString& label, QLabel*& valOut) {
        auto* box = new QWidget;
        auto* l = new QVBoxLayout(box);
        l->setContentsMargins(2, 2, 2, 2);
        l->setSpacing(0);
        auto* labL = new QLabel(label);
        labL->setObjectName("HudLabel");
        labL->setAlignment(Qt::AlignCenter);
        valOut = new QLabel("--");
        valOut->setObjectName("HudValue");
        valOut->setAlignment(Qt::AlignCenter);
        l->addWidget(valOut);
        l->addWidget(labL);
        return box;
    };
    wfl->addWidget(mkHud("CAM POS",       camPosVal_));
    wfl->addWidget(mkHud("ZOOM",          zoomVal_));
    wfl->addWidget(mkHud("RENDER FPS",    renderFpsVal_));

    // 重置视角按钮
    auto* btnReset = new QPushButton("Reset View");
    btnReset->setMaximumWidth(96);

    root->addWidget(wfHost);

    // ---- 3D 点云区 (主视图) ----
    // FIX: QOpenGLWidget on Qt 5.12/Windows has a compositing bug where the
    // GL FBO content bleeds over sibling widgets (right panel gets overdrawn).
    // Root cause: QOpenGLWidget creates a native HWND for its GL context, but
    // when the widget is inside a non-native parent, the OS window manager
    // cannot clip the GL rendering to the widget's bounds.
    // Solution: Wrap the GL widget in a native QWidget container (WA_NativeWindow).
    // The container's HWND becomes a hard clip boundary for the GL child window.
    auto* glHost = new QWidget;
    glHost->setObjectName("GLHost");
    glHost->setAttribute(Qt::WA_NativeWindow);
    glHost->setStyleSheet("background: #0B0E13; border: none;");
    auto* glLayout = new QVBoxLayout(glHost);
    glLayout->setContentsMargins(0, 0, 0, 0);
    glLayout->setSpacing(0);
    pcw_ = new PointCloudWidget(glHost);
    pcw_->setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    glLayout->addWidget(pcw_);
    root->addWidget(glHost, 1);

    // ---- 三窗格预览 ----
    preview_ = new StereoPreviewStrip;
    root->addWidget(preview_);
    preview_->setMaximumHeight(220);

    connect(btnReset, &QPushButton::clicked, pcw_, &PointCloudWidget::resetView);
}

// ============================================================================
// BottomBar: FPS / 帧数 / 回环数 / 点数 / 进度条
// ============================================================================
void MainWindow::buildBottomBar(QWidget* host) {
    auto* h = new QHBoxLayout(host);
    h->setContentsMargins(16, 8, 16, 8);
    h->setSpacing(24);

    auto mkBot = [](const QString& label, QLabel*& valOut) {
        auto* host = new QWidget;
        auto* l = new QVBoxLayout(host);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        auto* labL = new QLabel(label);
        labL->setObjectName("BotStatLabel");
        valOut = new QLabel("0");
        valOut->setObjectName("BotStatValue");
        valOut->setProperty("role", "accent");
        l->addWidget(valOut);
        l->addWidget(labL);
        return host;
    };
    h->addWidget(mkBot("FPS",    botFpsVal_));
    h->addWidget(mkBot("FRAMES", botFramesVal_));
    h->addWidget(mkBot("LOOPS",  botLoopsVal_));
    h->addWidget(mkBot("POINTS", botPtsVal_));

    h->addStretch();

    // 进度条
    auto* progHost = new QWidget;
    auto* pl = new QVBoxLayout(progHost);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(2);
    auto* plRow = new QHBoxLayout;
    auto* plL = new QLabel("TASK PROGRESS");
    plL->setObjectName("ProgressBarLabel");
    botProgressPct_ = new QLabel("0%");
    botProgressPct_->setObjectName("ProgressBarPct");
    botProgressPct_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    plRow->addWidget(plL);
    plRow->addStretch();
    plRow->addWidget(botProgressPct_);
    pl->addLayout(plRow);

    botProgress_ = new QSlider(Qt::Horizontal);
    botProgress_->setEnabled(false);
    botProgress_->setRange(0, 100);
    botProgress_->setMaximumWidth(320);
    pl->addWidget(botProgress_);

    h->addWidget(progHost);
}

// ============================================================================
// 信号连接
// ============================================================================
void MainWindow::wireSignals() {
    // Controller -> UI
    connect(controller_, &ScanController::frameReady,
            this, [this](const QImage& l, const QImage& r, const QImage& d,
                         const ScanStats& st) {
        preview_->leftPane()->setImage(l);
        preview_->rightPane()->setImage(r);
        preview_->depthPane()->setImage(d);

        if (botFpsVal_)    botFpsVal_->setText(QString::number(st.fps, 'f', 1));
        if (botFramesVal_) botFramesVal_->setText(QString::number(st.totalFrames));
        if (botLoopsVal_)  botLoopsVal_->setText(QString::number(st.loopClosures));
        if (right_) right_->setProgress(int((st.totalFrames % 100)));
    });
    connect(controller_, &ScanController::statsUpdated,
            this, [this](const ScanStats& st) {
        if (botFramesVal_) botFramesVal_->setText(QString::number(st.totalFrames));
    });
    connect(controller_, &ScanController::pointCloudReady,
            this, [this](const std::vector<GlPoint>& cloud, bool replace) {
        if (replace) pcw_->setPointCloud(cloud);
        else         pcw_->appendPoints(cloud);
        if (botPtsVal_) botPtsVal_->setText(QString::number(pcw_->pointCount()));
    });
    connect(controller_, &ScanController::logMessage,
            right_, &RightPanel::appendLog);
    connect(controller_, &ScanController::error,
            this, [this](const QString& msg) {
        right_->appendLog("[ERR] " + msg);
        right_->setScanState(RightPanel::ScanState::Error);
    });
    connect(controller_, &ScanController::stateChanged,
            this, [this](int s) {
        using SC = ScanController;
        switch (SC::State(s)) {
        case SC::State::Idle:        right_->setScanState(RightPanel::ScanState::Idle); break;
        case SC::State::Running:     right_->setScanState(RightPanel::ScanState::Running); break;
        case SC::State::Paused:      right_->setScanState(RightPanel::ScanState::Paused); break;
        case SC::State::Stopped:     right_->setScanState(RightPanel::ScanState::Idle); break;
        case SC::State::Error:       right_->setScanState(RightPanel::ScanState::Error); break;
        default: break;
        }
    });

    // UI -> Controller
    connect(right_, &RightPanel::startScanRequested,
            this, [this]() {
        controller_->startScan();
        right_->setScanState(RightPanel::ScanState::Running);
    });
    connect(right_, &RightPanel::pauseScanRequested,
            controller_, &ScanController::pauseScan);
    connect(right_, &RightPanel::stopScanRequested,
            controller_, &ScanController::stopScan);
    connect(right_, &RightPanel::resetSlamRequested,
            controller_, &ScanController::resetSLAM);
    connect(right_, &RightPanel::saveDataRequested,
            controller_, &ScanController::saveSnapshot);
    connect(right_, &RightPanel::exportRequested,
            controller_, &ScanController::exportPLY);

    connect(right_, &RightPanel::maxDispChanged,
            controller_, &ScanController::setMaxDisp);
    connect(right_, &RightPanel::validItersChanged,
            controller_, &ScanController::setValidIters);
    connect(right_, &RightPanel::maxDepthChanged,
            controller_, &ScanController::setMaxDepth);
    connect(right_, &RightPanel::voxelSizeChanged,
            controller_, &ScanController::setVoxelSize);
    connect(right_, &RightPanel::depthVisualModeChanged,
            controller_, &ScanController::setDepthVisualMode);

    // 点云 HUD 反馈 -> 顶栏
    connect(pcw_, &PointCloudWidget::viewChanged,
            this, [this](double yaw, double pitch, double dist,
                         float cx, float cy, float cz) {
        (void)yaw; (void)pitch;
        if (camPosVal_) {
            camPosVal_->setText(QString("%1, %2, %3")
                                .arg(cx, 0, 'f', 1)
                                .arg(cy, 0, 'f', 1)
                                .arg(cz, 0, 'f', 1));
        }
        if (zoomVal_) {
            zoomVal_->setText(QString::number(4.0 / std::max(0.1, dist), 'f', 2) + "x");
        }
    });
    connect(pcw_, &PointCloudWidget::renderFpsUpdated,
            this, [this](double fps) {
        if (renderFpsVal_) renderFpsVal_->setText(QString::number(fps, 'f', 0) + " FPS");
    });
    connect(pcw_, &PointCloudWidget::pointCountChanged,
            this, [this](int n) {
        if (botPtsVal_) botPtsVal_->setText(QString::number(n));
    });
}

// ============================================================================
// 载入示例点云: 雕像/机械零件形态 (与 HTML 设计稿一致)
// ============================================================================
void MainWindow::loadDemoPointCloud() {
    std::vector<GlPoint> cloud;
    std::mt19937 rng(20260719);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_real_distribution<float> uAngle(0.0f, float(M_PI) * 2);

    auto heightColor = [](float t) -> std::tuple<uint8_t,uint8_t,uint8_t> {
        t = std::max(0.0f, std::min(1.0f, t));
        if (t < 0.25f) return {69, 0, 84};
        if (t < 0.50f) return {59, 82, 140};
        if (t < 0.75f) return {33, 145, 140};
        return {166, 235, 38};
    };

    // 1) 椭球主体
    for (int i = 0; i < 8000; ++i) {
        float theta = std::acos(2*u01(rng) - 1);
        float phi   = uAngle(rng);
        float r     = 0.95f + u01(rng) * 0.05f;
        float x = r * 0.85f * std::sin(theta) * std::cos(phi);
        float y = r * 0.55f * std::cos(theta) + 0.1f;
        float z = r * 0.85f * std::sin(theta) * std::sin(phi);
        auto [r_, g_, b_] = heightColor((y + 0.5f) / 1.0f);
        cloud.emplace_back(x, y, z, r_, g_, b_);
    }
    // 2) 底座平面
    for (int i = 0; i < 2500; ++i) {
        float rad = u01(rng) * 1.4f;
        float a   = uAngle(rng);
        cloud.emplace_back(rad * std::cos(a),
                           -0.45f + (u01(rng)-0.5f) * 0.01f,
                           rad * std::sin(a),
                           90, 107, 132);
    }
    // 3) 顶部圆柱凸起
    for (int i = 0; i < 2000; ++i) {
        float a = uAngle(rng);
        float h = u01(rng) * 0.35f;
        float r = 0.22f;
        float y = 0.55f + h;
        auto [r_, g_, b_] = heightColor(y / 1.0f);
        cloud.emplace_back(r * std::cos(a), y, r * std::sin(a), r_, g_, b_);
    }

    pcw_->setPointCloud(cloud);
    if (right_) right_->appendLog("Demo point cloud loaded.");
}

// ============================================================================
// 加载配置
// ============================================================================
void MainWindow::loadConfig(const QString& configFile) {
    configPath_ = configFile;
    if (QDir::isRelativePath(configFile)) {
        configPath_ = QDir::current().absoluteFilePath(configFile);
    }

    // 加载 ScannerConfig (内含 leftCam/rightCam/engineDir 等)
    auto cfg = stereo_scanner::ScannerConfig::fromFile(configPath_.toStdString());
    controller_->setConfig(cfg);

    // 同步 cali 文件路径给 controller (默认与 config 同目录下 cali.yml)
    QString caliPath = QFileInfo(configPath_).absolutePath() + "/cali.yml";
    controller_->setCalibrationFile(caliPath);

    if (right_) right_->appendLog("Config loaded: " + configPath_);
}

// ============================================================================
// 关闭事件: 安全停止线程
// ============================================================================
void MainWindow::closeEvent(QCloseEvent* e) {
    if (controller_ && controller_->isRunning()) {
        controller_->stopScan();
        controller_->wait(2000);
    }
    e->accept();
}

} // namespace ui
