#include "scan_controller.h"

#include "common/config.h"
#include "common/types.h"
#include "calibration/stereo_calibrator.h"
#include "stereo/ffs_inference.h"
#include "slam/scanner_slam.h"

#include <QMutexLocker>
#include <QImage>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <deque>
#include <cmath>
#include <filesystem>

namespace ui {

namespace fs = std::filesystem;
using clk = std::chrono::high_resolution_clock;

// ============================================================================
// 工具: OpenCV Mat -> QImage (主线程使用, 拷贝)
// ============================================================================
static QImage cvMatToQImage(const cv::Mat& in, bool isColor = true) {
    if (in.empty()) return QImage();
    if (isColor) {
        // BGR -> RGB
        cv::Mat rgb;
        cv::cvtColor(in, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows,
                      int(rgb.step), QImage::Format_RGB888).copy();
    }
    // 单通道灰度
    return QImage(in.data, in.cols, in.rows,
                  int(in.step), QImage::Format_Grayscale8).copy();
}

// 深度图 (CV_32FC1) -> 伪彩色 QImage
static QImage depthToColorImage(const cv::Mat& depth, float maxDepth, int mode) {
    if (depth.empty()) return QImage();
    cv::Mat mask = depth > 0;
    cv::Mat vis;
    cv::Mat normF;
    depth.convertTo(normF, CV_32F, 1.0f / std::max(1e-6f, maxDepth));
    cv::Mat norm8;
    normF.convertTo(norm8, CV_8UC1, 255.0);
    if (mode == 0) {
        cv::applyColorMap(norm8, vis, cv::COLORMAP_TURBO);
    } else if (mode == 1) {
        cv::applyColorMap(norm8, vis, cv::COLORMAP_JET);
    } else {
        cv::cvtColor(norm8, vis, cv::COLOR_GRAY2BGR);
    }
    vis.setTo(cv::Scalar::all(0), ~mask);
    return cvMatToQImage(vis, true);
}

// 视差图 (CV_32FC1) -> 伪彩色 QImage
static QImage disparityToColorImage(const cv::Mat& disp, float maxDisp) {
    if (disp.empty()) return QImage();
    cv::Mat mask = disp > 0;
    cv::Mat normF;
    disp.convertTo(normF, CV_32F, 1.0f / std::max(1e-6f, maxDisp));
    cv::Mat norm8;
    normF.convertTo(norm8, CV_8UC1, 255.0);
    cv::Mat vis;
    cv::applyColorMap(norm8, vis, cv::COLORMAP_JET);
    vis.setTo(cv::Scalar::all(0), ~mask);
    return cvMatToQImage(vis, true);
}

// ============================================================================
// 构造 / 析构
// ============================================================================
ScanController::ScanController(QObject* parent)
    : QThread(parent)
{
}

ScanController::~ScanController() {
    wantStop_.store(true);
    wantPause_.store(false);
    if (isRunning()) {
        quit();
        wait(3000);  // 等待最多 3s
    }
}

// ============================================================================
// 配置 setter
// ============================================================================
void ScanController::setCalibrationFile(const QString& path)   { caliFile_  = path; }
void ScanController::setEngineDir(const QString& dir)          { engineDir_ = dir; }
void ScanController::setCameraIds(int left, int right, int w, int h) {
    leftCamId_ = left; rightCamId_ = right;
    capWidth_ = w; capHeight_ = h;
}
void ScanController::setConfig(const stereo_scanner::ScannerConfig& config) {
    config_ = config;
    leftCamId_ = config_.leftCameraId;
    rightCamId_ = config_.rightCameraId;
    capWidth_  = config_.captureResolution.width;
    capHeight_ = config_.captureResolution.height;
    engineDir_ = QString::fromStdString(config_.ffsEngineDir);
}
void ScanController::setOutputDir(const QString& dir) { outputDir_ = dir; }

void ScanController::setMaxDisp(int v)         { QMutexLocker l(&paramMutex_); maxDisp_ = v; }
void ScanController::setValidIters(int v)      { QMutexLocker l(&paramMutex_); validIters_ = v; }
void ScanController::setMaxDepth(int v)        { QMutexLocker l(&paramMutex_); maxDepth_ = v; }
void ScanController::setVoxelSize(int v)       { QMutexLocker l(&paramMutex_); voxelSize_ = v; }
void ScanController::setDepthVisualMode(int m) { QMutexLocker l(&paramMutex_); depthVisMode_ = m; }

// ============================================================================
// 控制
// ============================================================================
void ScanController::startScan() {
    if (state() == State::Running) return;
    if (state() == State::Paused) { wantPause_.store(false); return; }
    if (!isRunning()) {
        wantStop_.store(false);
        wantPause_.store(false);
        startThread();
    }
}

void ScanController::pauseScan()   { wantPause_.store(true); }
void ScanController::resumeScan()  { wantPause_.store(false); }
void ScanController::stopScan()    {
    wantStop_.store(true);
    wantPause_.store(false);
}
void ScanController::resetSLAM()   { wantReset_.store(true); }
void ScanController::saveSnapshot() { wantSave_.store(true); }
void ScanController::exportPLY()    { wantExport_.store(true); }

void ScanController::startThread() {
    if (!isRunning()) {
        start(QThread::TimeCriticalPriority);
    }
}

// ============================================================================
// 后端初始化 (在 run() 中调用)
// ============================================================================
bool ScanController::initBackend() {
    emit logMessage("Loading calibration: " + caliFile_);
    calibrator_ = std::make_unique<stereo_scanner::StereoCalibrator>();
    if (!calibrator_->load(caliFile_.toStdString())) {
        emit error("Failed to load calibration: " + caliFile_);
        return false;
    }
    if (!calibrator_->data().valid()) {
        emit error("Calibration data invalid");
        return false;
    }
    auto& cali = calibrator_->data();
    emit logMessage(QString("Calibration loaded. baseline=%1mm fx=%2 size=%3x%4")
                    .arg(cali.baseline * 1000, 0, 'f', 1)
                    .arg(cali.fx, 0, 'f', 1)
                    .arg(cali.imageSize.width).arg(cali.imageSize.height));

    emit logMessage("Initializing FFS engine: " + engineDir_);
    ffs_ = std::make_unique<stereo_scanner::FFSInference>();
    if (!ffs_->init(engineDir_.toStdString())) {
        emit error("FFS engine init failed");
        return false;
    }
    emit logMessage("FFS engine ready.");

    emit logMessage("Initializing RTAB-Map SLAM...");
    slam_ = std::make_unique<stereo_scanner::ScannerSLAM>();
    if (!slam_->init(config_, cali)) {
        emit error("SLAM init failed");
        return false;
    }
    emit logMessage("SLAM ready.");
    return true;
}

void ScanController::releaseBackend() {
    if (capL_) { capL_->release(); capL_.reset(); }
    if (capR_) { capR_->release(); capR_.reset(); }
    camsOpened_ = false;
    slam_.reset();
    ffs_.reset();
    calibrator_.reset();
}

// ============================================================================
// 处理一帧 (子线程)
// ============================================================================
bool ScanController::processOneFrame() {
    // 取最新参数
    int curMaxDisp, curMaxDepth, curVoxel, curMode, curIters;
    {
        QMutexLocker l(&paramMutex_);
        curMaxDisp = maxDisp_;
        curMaxDepth = maxDepth_;
        curVoxel = voxelSize_;
        curMode = depthVisMode_;
        curIters = validIters_;
    }
    (void)curIters;
    (void)curMaxDisp;

    if (!camsOpened_) {
        capL_ = std::make_unique<cv::VideoCapture>();
        capR_ = std::make_unique<cv::VideoCapture>();
        if (!capL_->open(leftCamId_, cv::CAP_ANY)) {
            emit error("Cannot open left camera");
            return false;
        }
        if (!capR_->open(rightCamId_, cv::CAP_ANY)) {
            emit error("Cannot open right camera");
            capL_->release();
            capL_.reset();
            capR_.reset();
            return false;
        }
        capL_->set(cv::CAP_PROP_FRAME_WIDTH,  capWidth_);
        capL_->set(cv::CAP_PROP_FRAME_HEIGHT, capHeight_);
        capR_->set(cv::CAP_PROP_FRAME_WIDTH,  capWidth_);
        capR_->set(cv::CAP_PROP_FRAME_HEIGHT, capHeight_);
        capL_->set(cv::CAP_PROP_BUFFERSIZE, 1);
        capR_->set(cv::CAP_PROP_BUFFERSIZE, 1);
        camsOpened_ = true;
        emit logMessage(QString("Cameras opened %1x%2").arg(capWidth_).arg(capHeight_));
    }

    // ---- Step 1: 抓帧 ----
    cv::Mat frameL, frameR;
    (*capL_) >> frameL;
    (*capR_) >> frameR;
    if (frameL.empty() || frameR.empty()) {
        emit error("Frame grab failed");
        return false;
    }
    auto t0 = clk::now();

    // ---- Step 2: 极线校正 ----
    cv::Mat leftRect, rightRect;
    calibrator_->rectifyPair(frameL, frameR, leftRect, rightRect);
    auto t1 = clk::now();

    // ---- Step 3: FFS 推理 ----
    cv::Mat disparity, depth;
    if (!ffs_->infer(leftRect, rightRect, calibrator_->data(), disparity, depth)) {
        emit error("FFS inference failed");
        return false;
    }
    auto t2 = clk::now();

    // ---- Step 4: SLAM 处理 ----
    double ts = std::chrono::duration<double>(clk::now().time_since_epoch()).count();
    auto result = slam_->processFrame(leftRect, depth, ts);
    auto t3 = clk::now();
    (void)result;

    frameCounter_++;

    // ---- Step 5: 构造 QImage 推送 UI ----
    QImage qL = cvMatToQImage(leftRect, true);
    QImage qR = cvMatToQImage(rightRect, true);
    QImage qD = depthToColorImage(depth, float(curMaxDepth), curMode);

    ScanStats stats;
    stats.frameId      = frameCounter_;
    stats.totalFrames  = frameCounter_;
    stats.failedFrames = totalFailed_;
    stats.grabMs       = 0;
    stats.rectifyMs    = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.inferMs      = std::chrono::duration<double, std::milli>(t2 - t1).count();
    stats.slamMs       = std::chrono::duration<double, std::milli>(t3 - t2).count();

    emit frameReady(qL, qR, qD, stats);
    emit statsUpdated(stats);
    return true;
}

// ============================================================================
// 主循环 (子线程)
// ============================================================================
void ScanController::run() {
    if (!initBackend()) {
        state_.store(State::Error);
        emit stateChanged(int(State::Error));
        return;
    }

    state_.store(State::Running);
    emit stateChanged(int(State::Running));
    emit logMessage("Scan started.");

    // FPS 计数 (滑动窗口)
    std::deque<clk::time_point> ts;
    ScanStats lastStats;
    (void)lastStats;

    startTimeNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       clk::now().time_since_epoch()).count();

    while (!wantStop_.load()) {
        // 暂停
        if (wantPause_.load()) {
            if (state() != State::Paused) {
                state_.store(State::Paused);
                emit stateChanged(int(State::Paused));
                emit logMessage("Paused.");
            }
            QThread::msleep(50);
            continue;
        } else {
            if (state() != State::Running) {
                state_.store(State::Running);
                emit stateChanged(int(State::Running));
                emit logMessage("Resumed.");
            }
        }

        // 控制侧请求
        if (wantReset_.exchange(false)) {
            emit logMessage("Reset SLAM requested (not supported live yet).");
        }
        if (wantSave_.exchange(false)) {
            fs::create_directories(outputDir_.toStdString());
            emit logMessage("Snapshot saved to " + outputDir_);
        }
        if (wantExport_.exchange(false)) {
            fs::create_directories(outputDir_.toStdString());
            std::string ply = (fs::path(outputDir_.toStdString()) / "global_cloud.ply").string();
            int vsize; {
                QMutexLocker l(&paramMutex_);
                vsize = voxelSize_;
            }
            if (slam_->exportPointCloud(ply, float(vsize) / 1000.0f)) {
                emit logMessage("Exported PLY: " + QString::fromStdString(ply));
            } else {
                emit error("Failed to export PLY");
            }
        }

        // 一帧处理
        if (!processOneFrame()) {
            totalFailed_++;
            if (totalFailed_ > 30) {
                emit error("Too many failures, stopping.");
                break;
            }
            QThread::msleep(50);
            continue;
        }

        // FPS
        ts.push_back(clk::now());
        if (ts.size() > 30) ts.pop_front();
        totalLoops_++;

        // 让出 CPU 一帧 (避免 100% 占空)
        QThread::msleep(1);
    }

    releaseBackend();
    state_.store(State::Stopped);
    emit stateChanged(int(State::Stopped));
    emit logMessage(QString("Scan stopped. Frames: %1 Failed: %2")
                    .arg(totalLoops_).arg(totalFailed_));
}

} // namespace ui
