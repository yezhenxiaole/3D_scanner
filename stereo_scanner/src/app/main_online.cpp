/**
 * @file main_online.cpp
 * @brief 实时双目 SLAM 主程序
 *
 * 流水线: 双相机采集 → 极线校正 → FFS 推理 → 深度转换 → RTAB-Map SLAM → 实时可视化
 *
 * 单线程架构：抓取 → 校正 → 推理 → SLAM → 显示 串行执行。
 * 在 RTX 5060 + FFS-Large FP16 上预计 10~15 FPS，足以实时预览。
 * 后续如需更高吞吐可拆为 capture / infer / slam 三线程流水线。
 *
 * 使用方式:
 *   stereo_online --cali <cali.yml> --engine <ffs_engine> [--left-cam 0 --right-cam 1]
 *   stereo_online --config <default.yaml>
 *
 * 交互键:
 *   q / Q  退出并保存结果
 *   s      手动导出点云+轨迹
 *   r      重置 SLAM（重置里程计+地图）
 *   v      切换可视化（开/关显示窗口）
 *   p      暂停/继续（暂停时不处理新帧）
 *   d      切换显示深度/视差热图
 */

#include "common/config.h"
#include "common/types.h"
#include "calibration/stereo_calibrator.h"
#include "stereo/ffs_inference.h"
#include "slam/scanner_slam.h"

#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

#include <iostream>
#include <filesystem>
#include <chrono>
#include <deque>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <csignal>
#include <cstring>

namespace fs = std::filesystem;

// ============================================================================
// 全局信号处理（Ctrl-C 优雅退出）
// ============================================================================
static std::atomic<bool> g_shouldExit{false};

static void signalHandler(int sig) {
    (void)sig;
    g_shouldExit.store(true);
}

// ============================================================================
// 命令行参数
// ============================================================================
struct OnlineArgs {
    std::string configFile = "config/default.yaml";
    std::string caliFile   = "config/cali.yml";
    std::string engineDir  = "ffs_engine";   // 默认与 build 目录下的 ffs_engine 对齐
    std::string outputDir  = "output";
    int  leftCamId  = -1;   // -1 表示用配置文件里的值
    int  rightCamId = -1;
    int  width      = -1;   // -1 表示用配置文件里的值
    int  height     = -1;
    int  maxFrames  = -1;   // -1 表示不限
    bool visualize       = true;
    bool saveFrames      = false;
    bool skipSLAM        = false;   // 只测深度流，不入 SLAM
    bool verboseTiming   = false;   // 打印每帧各阶段耗时
};

static OnlineArgs parseArgs(int argc, char** argv) {
    OnlineArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--config")        { if (i + 1 < argc) a.configFile = argv[++i]; }
        else if (arg == "--cali" || arg == "-f") { if (i + 1 < argc) a.caliFile = argv[++i]; }
        else if (arg == "--engine")        { if (i + 1 < argc) a.engineDir = argv[++i]; }
        else if (arg == "--output" || arg == "-o") { if (i + 1 < argc) a.outputDir = argv[++i]; }
        else if (arg == "--left-cam")      { if (i + 1 < argc) a.leftCamId = std::stoi(argv[++i]); }
        else if (arg == "--right-cam")     { if (i + 1 < argc) a.rightCamId = std::stoi(argv[++i]); }
        else if (arg == "--width")         { if (i + 1 < argc) a.width = std::stoi(argv[++i]); }
        else if (arg == "--height")        { if (i + 1 < argc) a.height = std::stoi(argv[++i]); }
        else if (arg == "--max-frames")    { if (i + 1 < argc) a.maxFrames = std::stoi(argv[++i]); }
        else if (arg == "--no-vis")        { a.visualize = false; }
        else if (arg == "--save-frames")   { a.saveFrames = true; }
        else if (arg == "--no-slam")       { a.skipSLAM = true; }
        else if (arg == "--verbose-time")  { a.verboseTiming = true; }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage:\n"
                      << "  stereo_online --cali <cali.yml> --engine <ffs_engine_dir>\n"
                      << "                [--left-cam 0 --right-cam 1] [--width 640 --height 480]\n"
                      << "                [--config <default.yaml>] [--no-vis] [--save-frames]\n"
                      << "                [--no-slam] [--max-frames N] [--verbose-time]\n"
                      << "\nOptions:\n"
                      << "  --config <f>       配置文件 (默认 config/default.yaml)\n"
                      << "  --cali <f>         标定文件 (默认 config/cali.yml)\n"
                      << "  --engine <d>       TRT 引擎目录 (默认 ffs_engine)\n"
                      << "  --output <d>       输出目录 (默认 output)\n"
                      << "  --left-cam <n>     左相机 ID (覆盖 config)\n"
                      << "  --right-cam <n>    右相机 ID (覆盖 config)\n"
                      << "  --width <n>        采集宽度 (覆盖 config)\n"
                      << "  --height <n>       采集高度 (覆盖 config)\n"
                      << "  --max-frames <n>   处理 N 帧后自动停止 (-1=无限)\n"
                      << "  --no-vis           不显示可视化窗口\n"
                      << "  --save-frames      保存左右原始帧到 output/frames/\n"
                      << "  --no-slam          只跑 FFS 深度估算，不入 SLAM\n"
                      << "  --verbose-time     打印每帧各阶段耗时\n"
                      << "\nHotkeys (可视化窗口内):\n"
                      << "  q/Q  退出并保存  s 导出  r 重置SLAM  v 切换显示  p 暂停  d 切换深度/视差\n";
            exit(0);
        } else {
            std::cerr << "[Online] Unknown argument: " << arg << " (ignored)\n";
        }
    }
    return a;
}

// ============================================================================
// 可视化辅助（与 main_offline.cpp 一致，单文件内独立实现以降低耦合）
// ============================================================================
static cv::Mat disparityToColorMap(const cv::Mat& disparity, float maxDisp) {
    cv::Mat dispNorm;
    disparity.convertTo(dispNorm, CV_8UC1, 255.0 / maxDisp);
    cv::Mat colorMap;
    cv::applyColorMap(dispNorm, colorMap, cv::COLORMAP_JET);
    for (int i = 0; i < disparity.rows; ++i) {
        for (int j = 0; j < disparity.cols; ++j) {
            if (disparity.at<float>(i, j) <= 0) {
                colorMap.at<cv::Vec3b>(i, j) = cv::Vec3b(0, 0, 0);
            }
        }
    }
    return colorMap;
}

static cv::Mat depthToColorMap(const cv::Mat& depth, float maxDepth) {
    cv::Mat depthNorm;
    depth.convertTo(depthNorm, CV_8UC1, 255.0 / maxDepth);
    cv::Mat colorMap;
    cv::applyColorMap(depthNorm, colorMap, cv::COLORMAP_TURBO);
    for (int i = 0; i < depth.rows; ++i) {
        for (int j = 0; j < depth.cols; ++j) {
            if (depth.at<float>(i, j) <= 0) {
                colorMap.at<cv::Vec3b>(i, j) = cv::Vec3b(0, 0, 0);
            }
        }
    }
    return colorMap;
}

// ============================================================================
// 滑动窗口 FPS（最近 N 帧的实时帧率）
// ============================================================================
class FPSCounter {
public:
    explicit FPSCounter(int window = 30) : window_(window) {}
    double tick() {
        auto now = std::chrono::high_resolution_clock::now();
        timestamps_.push_back(now);
        if (static_cast<int>(timestamps_.size()) > window_) {
            timestamps_.pop_front();
        }
        if (timestamps_.size() < 2) return 0.0;
        double dt = std::chrono::duration<double>(
            timestamps_.back() - timestamps_.front()).count();
        return dt > 0 ? (timestamps_.size() - 1) / dt : 0.0;
    }
    void reset() { timestamps_.clear(); }
private:
    std::deque<std::chrono::high_resolution_clock::time_point> timestamps_;
    int window_;
};

// ============================================================================
// 微秒级计时器（用于打印每阶段耗时）
// ============================================================================
class StageTimer {
public:
    using clock = std::chrono::high_resolution_clock;
    void reset() { start_ = clock::now(); }
    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(clock::now() - start_).count();
    }
    double lapMs(const std::string& name, bool verbose) {
        if (!verbose) { reset(); return 0; }
        double ms = elapsedMs();
        std::cout << "    " << name << ": " << std::fixed << std::setprecision(1) << ms << " ms\n";
        reset();
        return ms;
    }
private:
    clock::time_point start_;
};

// ============================================================================
// 主程序
// ============================================================================
int main(int argc, char** argv) {
    OnlineArgs args = parseArgs(argc, argv);

    std::cout << "========================================\n"
              << "  Stereo Scanner - Online (Real-time)\n"
              << "========================================\n\n";

    // ---- 1. 加载配置 ----
    stereo_scanner::ScannerConfig config;
    if (fs::exists(args.configFile)) {
        config = stereo_scanner::ScannerConfig::fromFile(args.configFile);
    } else {
        std::cout << "[Online] Config file not found, using defaults.\n";
    }

    // 命令行覆盖
    if (args.leftCamId  >= 0) config.leftCameraId  = args.leftCamId;
    if (args.rightCamId >= 0) config.rightCameraId = args.rightCamId;
    if (args.width      >  0) config.captureResolution.width  = args.width;
    if (args.height     >  0) config.captureResolution.height = args.height;
    if (!args.outputDir.empty()) config.outputDir = args.outputDir;
    if (!args.engineDir.empty()) config.ffsEngineDir = args.engineDir;

    std::cout << "[Online] Left cam:  " << config.leftCameraId << "\n";
    std::cout << "[Online] Right cam: " << config.rightCameraId << "\n";
    std::cout << "[Online] Capture size: "
              << config.captureResolution.width << "x"
              << config.captureResolution.height << "\n";
    std::cout << "[Online] Engine dir: " << config.ffsEngineDir << "\n";
    std::cout << "[Online] SLAM: " << (args.skipSLAM ? "DISABLED" : "ENABLED") << "\n\n";

    // ---- 2. 加载标定 ----
    std::cout << "[Online] Loading calibration: " << args.caliFile << "\n";
    stereo_scanner::StereoCalibrator calibrator;
    if (!calibrator.load(args.caliFile)) {
        std::cerr << "[Online] Failed to load calibration. Run stereo_calibrate first.\n";
        return 1;
    }
    auto& caliData = calibrator.data();
    if (!caliData.valid()) {
        std::cerr << "[Online] Calibration data invalid.\n";
        return 1;
    }
    std::cout << "[Online] Baseline: " << caliData.baseline * 1000 << " mm"
              << " | fx=" << caliData.fx << " fy=" << caliData.fy
              << " | size=" << caliData.imageSize << "\n\n";

    // ---- 3. 打开双相机 ----
    // 注: 用 CAP_MSMF (Microsoft Media Foundation) 在 Windows 上最稳；如失败回退默认
    cv::VideoCapture capL, capR;
    bool camOpenedL = capL.open(config.leftCameraId, cv::CAP_ANY);
    bool camOpenedR = capR.open(config.rightCameraId, cv::CAP_ANY);

    if (!camOpenedL) {
        std::cerr << "[Online] Cannot open left camera (id=" << config.leftCameraId << ")\n";
        return 1;
    }
    if (!camOpenedR) {
        std::cerr << "[Online] Cannot open right camera (id=" << config.rightCameraId << ")\n";
        capL.release();
        return 1;
    }

    // 设置采集分辨率
    capL.set(cv::CAP_PROP_FRAME_WIDTH,  config.captureResolution.width);
    capL.set(cv::CAP_PROP_FRAME_HEIGHT, config.captureResolution.height);
    capR.set(cv::CAP_PROP_FRAME_WIDTH,  config.captureResolution.width);
    capR.set(cv::CAP_PROP_FRAME_HEIGHT, config.captureResolution.height);

    // 设置缓冲区为 1（最低延迟；FFS 推理速度跟不上时让旧帧丢失）
    capL.set(cv::CAP_PROP_BUFFERSIZE, 1);
    capR.set(cv::CAP_PROP_BUFFERSIZE, 1);

    int realW = static_cast<int>(capL.get(cv::CAP_PROP_FRAME_WIDTH));
    int realH = static_cast<int>(capL.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fpsCam = capL.get(cv::CAP_PROP_FPS);
    std::cout << "[Online] Cameras opened. Real capture: "
              << realW << "x" << realH << " @ " << fpsCam << " fps\n\n";

    // 校验标定尺寸 vs 采集尺寸
    if (caliData.imageSize.width > 0 && caliData.imageSize != cv::Size(realW, realH)) {
        std::cerr << "[Online] WARNING: capture size (" << realW << "x" << realH
                  << ") != calibration size (" << caliData.imageSize.width
                  << "x" << caliData.imageSize.height
                  << "). Rectification may produce wrong results.\n";
    }

    // ---- 4. 初始化 FFS 推理 ----
    std::cout << "[Online] Initializing FFS engine: " << config.ffsEngineDir << "\n";
    stereo_scanner::FFSInference ffs;
    if (!ffs.init(config.ffsEngineDir)) {
        std::cerr << "[Online] FFS engine init failed.\n";
        capL.release(); capR.release();
        return 1;
    }

    // ---- 5. 初始化 RTAB-Map SLAM ----
    std::unique_ptr<stereo_scanner::ScannerSLAM> slam;
    if (!args.skipSLAM) {
        slam.reset(new stereo_scanner::ScannerSLAM());
        if (!slam->init(config, caliData)) {
            std::cerr << "[Online] SLAM init failed.\n";
            return 1;
        }
    } else {
        std::cout << "[Online] SLAM skipped (--no-slam).\n";
    }

    // ---- 6. 输出目录 ----
    fs::create_directories(config.outputDir);
    if (args.saveFrames) {
        fs::create_directories(config.outputDir + "/frames");
    }

    // ---- 7. 主循环 ----
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "\n[Online] Starting real-time loop. Press Q to quit.\n\n";

    FPSCounter fpsCounter(30);
    StageTimer  stageTimer;

    int  processedFrames = 0;
    int  loopClosures    = 0;
    int  failedFrames    = 0;
    int  consecutiveGrabFail = 0;

    bool paused      = false;
    bool showVis     = args.visualize;
    bool showDepth   = true;  // true=显示深度热图; false=显示视差热图

    auto startTime = std::chrono::high_resolution_clock::now();

    while (!g_shouldExit.load()) {
        // 帧数上限
        if (args.maxFrames > 0 && processedFrames >= args.maxFrames) {
            std::cout << "[Online] Max frames reached (" << args.maxFrames << ").\n";
            break;
        }

        // 暂停态：只显示，不处理新帧
        if (paused) {
            if (showVis) {
                int key = cv::waitKey(30);
                if (key == 'q' || key == 'Q') break;
                if (key == 'p') { paused = false; std::cout << "[Online] Resumed.\n"; }
                if (key == 'v') { showVis = !showVis; }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            continue;
        }

        stageTimer.reset();

        // ---- Step 1: 抓帧 ----
        cv::Mat frameL, frameR;
        capL >> frameL;
        capR >> frameR;

        if (frameL.empty() || frameR.empty()) {
            consecutiveGrabFail++;
            std::cerr << "[Online] Frame grab failed ("
                      << consecutiveGrabFail << "/30).\n";
            if (consecutiveGrabFail >= 30) {
                std::cerr << "[Online] Too many consecutive grab failures. Exiting.\n";
                break;
            }
            continue;
        }
        consecutiveGrabFail = 0;
        double grabMs = stageTimer.lapMs("grab", args.verboseTiming);

        double timestamp = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - startTime).count();

        // 同步左右时间戳：理论上同时抓，但单线程串行可能有几毫秒差异。
        // 这里用同一时间戳（适用于静态相机或慢速移动）。

        // ---- Step 2: 极线校正 ----
        cv::Mat leftRect, rightRect;
        calibrator.rectifyPair(frameL, frameR, leftRect, rightRect);
        double rectMs = stageTimer.lapMs("rectify", args.verboseTiming);

        // ---- Step 3: FFS 推理 ----
        cv::Mat disparity, depth;
        bool inferOK;
        if (args.skipSLAM || !showDepth) {
            // 仅需视差时也走全推理路径（FFSInference::infer 会顺带出 depth，开销相同）
            inferOK = ffs.infer(leftRect, rightRect, caliData, disparity, depth);
        } else {
            // SLAM 路径只需 depth（inferDepth 是最快路径），但若要可视化的视差也用 infer 接口
            // 这里默认用 infer 一次性取两者，方便可视化切换
            inferOK = ffs.infer(leftRect, rightRect, caliData, disparity, depth);
        }
        double inferMs = stageTimer.lapMs("ffs_infer", args.verboseTiming);

        if (!inferOK) {
            std::cerr << "[Online] FFS inference failed at frame " << processedFrames << "\n";
            failedFrames++;
            continue;
        }

        // ---- Step 4: RTAB-Map SLAM ----
        double slamMs = 0;
        if (!args.skipSLAM) {
            auto result = slam->processFrame(leftRect, depth, timestamp);
            if (result.success) {
                processedFrames++;
                loopClosures += result.loopClosureCount;
            } else {
                failedFrames++;
            }
            slamMs = stageTimer.lapMs("slam", args.verboseTiming);
        } else {
            processedFrames++;
        }

        // ---- Step 5: FPS 更新 ----
        double fps = fpsCounter.tick();

        // ---- Step 6: 保存帧（可选）----
        if (args.saveFrames) {
            std::stringstream ss;
            ss << std::setw(6) << std::setfill('0') << processedFrames;
            cv::imwrite(config.outputDir + "/frames/left_"  + ss.str() + ".png", frameL);
            cv::imwrite(config.outputDir + "/frames/right_" + ss.str() + ".png", frameR);
        }

        // ---- Step 7: 可视化 ----
        if (showVis) {
            // 在校正图上画水平极线（每 60 像素一条），便于实时检查校正质量
            cv::Mat leftAnno  = leftRect.clone();
            cv::Mat rightAnno = rightRect.clone();
            cv::Mat stereoPair;
            cv::hconcat(leftAnno, rightAnno, stereoPair);
            for (int y = 0; y < stereoPair.rows; y += 60) {
                cv::line(stereoPair, cv::Point(0, y),
                         cv::Point(stereoPair.cols, y),
                         cv::Scalar(0, 255, 0), 1);
            }

            // 深度或视差热图（右下）
            cv::Mat depthVis;
            if (showDepth) {
                depthVis = depthToColorMap(depth, config.maxDepth);
            } else {
                depthVis = disparityToColorMap(disparity, config.ffsMaxDisp);
            }

            // 拼接 2x2 画布
            cv::Mat topRow = stereoPair;
            cv::Mat botRow;
            // 把右侧热图缩放到与上排左/右图一半同宽
            cv::Mat depthVisResized;
            cv::resize(depthVis, depthVisResized,
                       cv::Size(stereoPair.cols, stereoPair.rows));
            cv::vconcat(topRow, depthVisResized, botRow);

            // 状态信息叠加
            std::ostringstream oss;
            oss << "Frame: " << processedFrames
                << " | FPS: " << std::fixed << std::setprecision(1) << fps
                << " | Loops: " << loopClosures
                << " | Failed: " << failedFrames
                << " | " << (showDepth ? "DEPTH" : "DISP")
                << " | " << (paused ? "PAUSED" : "RUN");
            if (args.verboseTiming) {
                oss << " | grab=" << std::setprecision(0) << grabMs
                    << " rect=" << rectMs
                    << " infer=" << inferMs
                    << " slam=" << slamMs << "ms";
            }
            std::string info = oss.str();
            int baseLine = 0;
            auto textSize = cv::getTextSize(info, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            cv::rectangle(botRow,
                          cv::Point(0, 0),
                          cv::Point(textSize.width + 10, textSize.height + 10),
                          cv::Scalar(0, 0, 0), -1);
            cv::putText(botRow, info, cv::Point(5, textSize.height + 5),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(0, 255, 255), 1);

            // 控制提示（底部）
            std::string hint = "[Q]Quit  [S]Save  [R]ResetSLAM  [V]Vis  [P]Pause  [D]Depth/Disp";
            cv::putText(botRow, hint, cv::Point(5, botRow.rows - 8),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4,
                        cv::Scalar(200, 200, 200), 1);

            cv::imshow("Stereo Online - Real-time", botRow);

            int key = cv::waitKey(1) & 0xFF;
            if      (key == 'q' || key == 'Q') { std::cout << "[Online] Quit requested.\n"; break; }
            else if (key == 's') {
                std::cout << "[Online] Manual save requested.\n";
                if (slam) {
                    slam->exportPointCloud(config.outputDir + "/global_cloud.ply",
                                          config.voxelSize);
                    slam->exportTrajectory(config.outputDir + "/trajectory_kitti.txt");
                }
                cv::imwrite(config.outputDir + "/snapshot_left.png",  leftRect);
                cv::imwrite(config.outputDir + "/snapshot_right.png", rightRect);
                cv::imwrite(config.outputDir + "/snapshot_depth.png",
                            showDepth ? depthToColorMap(depth, config.maxDepth)
                                      : disparityToColorMap(disparity, config.ffsMaxDisp));
                std::cout << "[Online] Snapshot + cloud saved to " << config.outputDir << "\n";
            }
            else if (key == 'r') {
                if (slam) {
                    std::cout << "[Online] Reset SLAM requested (currently not supported live; "
                              << "restart the program to reset).\n";
                }
            }
            else if (key == 'v') { showVis = !showVis; }
            else if (key == 'p') { paused = !paused; std::cout << "[Online] "
                                  << (paused ? "Paused." : "Resumed.") << "\n"; }
            else if (key == 'd') { showDepth = !showDepth; std::cout << "[Online] Switched to "
                                  << (showDepth ? "depth" : "disparity") << " view.\n"; }
        } else {
            // 不显示时低频打印状态
            if (processedFrames % 30 == 0) {
                std::cout << "[Online] Frame " << processedFrames
                          << " | FPS " << std::fixed << std::setprecision(1) << fps
                          << " | Loops " << loopClosures
                          << " | Failed " << failedFrames << "\n";
            }
        }
    }

    // ---- 8. 收尾 ----
    capL.release();
    capR.release();
    cv::destroyAllWindows();

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalTime = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "\n[Online] Session ended.\n";
    std::cout << "  Frames processed: " << processedFrames << "\n";
    std::cout << "  Frames failed:    " << failedFrames << "\n";
    std::cout << "  Loop closures:    " << loopClosures << "\n";
    std::cout << "  Total time:       " << std::fixed << std::setprecision(2)
              << totalTime << "s\n";
    std::cout << "  Average FPS:      " << std::setprecision(2)
              << (totalTime > 0 ? processedFrames / totalTime : 0.0) << "\n\n";

    // 导出结果
    if (slam && processedFrames > 0) {
        if (config.exportPointCloud) {
            std::string cloudPath = config.outputDir + "/global_cloud.ply";
            std::cout << "[Online] Exporting point cloud: " << cloudPath << "\n";
            slam->exportPointCloud(cloudPath, config.voxelSize);
        }
        if (config.exportTrajectory) {
            std::string trajPath = config.outputDir + "/trajectory_kitti.txt";
            std::cout << "[Online] Exporting trajectory: " << trajPath << "\n";
            slam->exportTrajectory(trajPath);
        }
    }

    std::cout << "\n[Online] All done. Bye.\n";
    return 0;
}
