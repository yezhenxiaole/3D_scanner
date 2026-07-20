#pragma once
#include <QThread>
#include <QImage>
#include <QMutex>
#include <QString>
#include <vector>
#include <memory>
#include <atomic>

#include "common/types.h"
#include "common/config.h"
#include "pointcloud_widget.h"

// 前置声明以降低头耦合
namespace stereo_scanner {
    class StereoCalibrator;
    class FFSInference;
    class ScannerSLAM;
}

// OpenCV 前置声明
namespace cv { class VideoCapture; }

namespace ui {

// ============================================================================
// 单帧统计
// ============================================================================
struct ScanStats {
    int    frameId        = 0;
    int    totalFrames    = 0;
    int    loopClosures   = 0;
    int    failedFrames   = 0;
    double fps            = 0.0;
    double grabMs         = 0.0;
    double rectifyMs      = 0.0;
    double inferMs        = 0.0;
    double slamMs         = 0.0;
    int    totalPoints    = 0;
};

// ============================================================================
// 扫描控制器 (后台线程)
// 在独立 QThread 上跑: 双相机采集 → 极线校正 → FFS 推理 → SLAM 处理
// 通过信号向 UI 线程推送: 预览图 / 点云增量 / 统计 / 状态
// ============================================================================
class ScanController : public QThread {
    Q_OBJECT

public:
    explicit ScanController(QObject* parent = nullptr);
    ~ScanController() override;

    // 配置 (在 start 前调用)
    void setCalibrationFile(const QString& path);
    void setEngineDir(const QString& dir);
    void setCameraIds(int left, int right, int width = 640, int height = 480);
    void setConfig(const stereo_scanner::ScannerConfig& config);
    void setOutputDir(const QString& dir);

    // 参数实时调整 (锁内生效下一帧)
    Q_SLOT void setMaxDisp(int v);
    Q_SLOT void setValidIters(int v);
    Q_SLOT void setMaxDepth(int v);
    Q_SLOT void setVoxelSize(int v);
    Q_SLOT void setDepthVisualMode(int mode);

    // 控制
    Q_SLOT void startScan();
    Q_SLOT void pauseScan();
    Q_SLOT void resumeScan();
    Q_SLOT void stopScan();
    Q_SLOT void resetSLAM();
    Q_SLOT void saveSnapshot();
    Q_SLOT void exportPLY();

    // 启动线程 (会持有所有资源)
    void startThread();

    enum class State { Uninitialized, Idle, Running, Paused, Stopped, Error };
    State state() const { return state_.load(); }

signals:
    // 帧推进
    void frameReady(const QImage& left, const QImage& right, const QImage& depth,
                    const ScanStats& stats);
    void pointCloudReady(const std::vector<ui::GlPoint>& cloud, bool replace);
    void statsUpdated(const ScanStats& stats);
    void stateChanged(int state);  // State
    void logMessage(const QString& msg);
    void error(const QString& msg);

protected:
    void run() override;

private:
    // 内部辅助
    bool initBackend();
    void releaseBackend();
    bool processOneFrame();           // 处理一帧, 失败返回 false

    // 配置 (主线程设置, 子线程读取)
    QString caliFile_        = "config/cali.yml";
    QString engineDir_       = "ffs_engine";
    QString outputDir_       = "output";
    int     leftCamId_       = 0;
    int     rightCamId_      = 1;
    int     capWidth_        = 640;
    int     capHeight_       = 480;
    stereo_scanner::ScannerConfig config_;

    // 运行参数 (子线程运行时受 mutex 保护)
    QMutex  paramMutex_;
    int     maxDisp_         = 192;
    int     validIters_      = 8;
    int     maxDepth_        = 5;
    int     voxelSize_       = 5;     // mm
    int     depthVisMode_    = 0;     // 0 TURBO 1 JET 2 GRAY

    // 后端对象 (子线程持有, 通过 unique_ptr 在 run() 内构造)
    std::unique_ptr<stereo_scanner::StereoCalibrator> calibrator_;
    std::unique_ptr<stereo_scanner::FFSInference>     ffs_;
    std::unique_ptr<stereo_scanner::ScannerSLAM>      slam_;
    std::unique_ptr<cv::VideoCapture>                 capL_;
    std::unique_ptr<cv::VideoCapture>                 capR_;
    bool camsOpened_ = false;

    // 帧计数
    int frameCounter_  = 0;
    int totalFailed_   = 0;
    int totalLoops_    = 0;

    // 控制位
    std::atomic<State>   state_{State::Uninitialized};
    std::atomic<bool>    wantStop_{false};
    std::atomic<bool>    wantPause_{false};
    std::atomic<bool>    wantReset_{false};
    std::atomic<bool>    wantSave_{false};
    std::atomic<bool>    wantExport_{false};

    // 计时
    qint64 startTimeNs_ = 0;
};

} // namespace ui
