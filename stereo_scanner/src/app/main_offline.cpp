/**
 * @file main_offline.cpp
 * @brief 离线批处理 SLAM 主程序
 *
 * 完整流水线: 加载标定数据 → 加载图像序列 → 极线校正 → FFS推理 → 深度转换 → RTAB-Map SLAM → 导出结果
 *
 * 使用方式:
 *   stereo_offline --config <default.yaml> --cali <cali.yml> --dir <image_dir>
 *   stereo_offline --dir <image_dir>   (使用默认配置)
 */

#include "common/config.h"
#include "common/types.h"
#include "calibration/stereo_calibrator.h"
#include "stereo/ffs_inference.h"
#include "slam/scanner_slam.h"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cstdio>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

// ============================================================================
// 命令行参数
// ============================================================================
struct OfflineArgs {
    std::string configFile = "config/default.yaml";
    std::string caliFile   = "config/cali.yml";
    std::string imageDir;         // 图像序列目录
    std::string outputDir  = "output";
    std::string engineDir  = "models/ffs_engine";
    bool visualize         = true;  // 是否实时显示中间结果
    bool saveIntermediate  = false; // 是否保存中间结果(视差图/深度图)
    int startFrame         = 0;     // 起始帧编号
    int maxFrames          = -1;    // 最大处理帧数 (-1=全部)
};

static OfflineArgs parseArgs(int argc, char** argv) {
    OfflineArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 < argc) args.configFile = argv[++i];
        } else if (arg == "--cali") {
            if (i + 1 < argc) args.caliFile = argv[++i];
        } else if (arg == "--dir" || arg == "-d") {
            if (i + 1 < argc) args.imageDir = argv[++i];
        } else if (arg == "--output" || arg == "-o") {
            if (i + 1 < argc) args.outputDir = argv[++i];
        } else if (arg == "--engine") {
            if (i + 1 < argc) args.engineDir = argv[++i];
        } else if (arg == "--no-vis") {
            args.visualize = false;
        } else if (arg == "--save-inter") {
            args.saveIntermediate = true;
        } else if (arg == "--start") {
            if (i + 1 < argc) args.startFrame = std::stoi(argv[++i]);
        } else if (arg == "--max-frames") {
            if (i + 1 < argc) args.maxFrames = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage:\n"
                      << "  stereo_offline --dir <image_dir> [--cali <cali.yml>]\n"
                      << "\nOptions:\n"
                      << "  --config <f>     配置文件 (默认 config/default.yaml)\n"
                      << "  --cali <f>       标定文件 (默认 config/cali.yml)\n"
                      << "  --dir <path>     图像序列目录\n"
                      << "  --output <path>  输出目录 (默认 output)\n"
                      << "  --engine <path>  TRT引擎目录\n"
                      << "  --no-vis         不显示可视化\n"
                      << "  --save-inter     保存中间结果\n"
                      << "  --start <n>      起始帧编号\n"
                      << "  --max-frames <n> 最大处理帧数\n";
            exit(0);
        }
    }
    return args;
}

// ============================================================================
// 图像序列加载器
// ============================================================================
struct ImageSequence {
    std::vector<std::string> leftPaths;
    std::vector<std::string> rightPaths;
    std::vector<double> timestamps;

    bool empty() const { return leftPaths.empty(); }
    int size() const { return static_cast<int>(leftPaths.size()); }
};

static ImageSequence loadImageSequence(const std::string& dir, const std::string& format) {
    ImageSequence seq;

    // 尝试多种命名格式
    // 格式1: left_000.png / right_000.png (编号格式)
    for (int i = 0; i < 10000; ++i) {
        std::stringstream ss;
        ss << std::setw(3) << std::setfill('0') << i;

        std::string leftPath  = dir + "/left_" + ss.str() + "." + format;
        std::string rightPath = dir + "/right_" + ss.str() + "." + format;

        if (!fs::exists(leftPath) || !fs::exists(rightPath)) {
            // 也试试无前导零的格式
            leftPath  = dir + "/left_" + std::to_string(i) + "." + format;
            rightPath = dir + "/right_" + std::to_string(i) + "." + format;
        }

        if (!fs::exists(leftPath) || !fs::exists(rightPath)) break;

        seq.leftPaths.push_back(leftPath);
        seq.rightPaths.push_back(rightPath);
        seq.timestamps.push_back(i * 0.1);  // 假设 0.1 秒间隔
    }

    // 如果编号格式没找到, 扫描目录文件
    if (seq.empty() && fs::exists(dir)) {
        std::vector<std::string> allLeft, allRight;
        for (auto& entry : fs::directory_iterator(dir)) {
            std::string name = entry.path().filename().string();
            std::string ext  = entry.path().extension().string();
            // 去掉前面的 "."
            ext = ext.empty() ? "" : ext.substr(1);

            if (ext != format && format != "*") continue;

            if (name.find("left") != std::string::npos) {
                allLeft.push_back(entry.path().string());
            } else if (name.find("right") != std::string::npos) {
                allRight.push_back(entry.path().string());
            }
        }

        std::sort(allLeft.begin(), allLeft.end());
        std::sort(allRight.begin(), allRight.end());

        int n = static_cast<int>(std::min(allLeft.size(), allRight.size()));
        for (int i = 0; i < n; ++i) {
            seq.leftPaths.push_back(allLeft[i]);
            seq.rightPaths.push_back(allRight[i]);
            seq.timestamps.push_back(i * 0.1);
        }
    }

    return seq;
}

// ============================================================================
// 视差图可视化（彩色热力图）
// ============================================================================
static cv::Mat disparityToColorMap(const cv::Mat& disparity, float maxDisp = 192.0f) {
    cv::Mat dispNorm;
    disparity.convertTo(dispNorm, CV_8UC1, 255.0 / maxDisp);
    cv::Mat colorMap;
    cv::applyColorMap(dispNorm, colorMap, cv::COLORMAP_JET);
    // 无效区域设为黑色
    for (int i = 0; i < disparity.rows; ++i) {
        for (int j = 0; j < disparity.cols; ++j) {
            if (disparity.at<float>(i, j) <= 0) {
                colorMap.at<cv::Vec3b>(i, j) = cv::Vec3b(0, 0, 0);
            }
        }
    }
    return colorMap;
}

// ============================================================================
// 深度图可视化
// ============================================================================
static cv::Mat depthToColorMap(const cv::Mat& depth, float maxDepth = 5.0f) {
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
// 主流程
// ============================================================================
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // Disable stdout buffering for reliable log capture

    OfflineArgs args = parseArgs(argc, argv);

    std::cout << "========================================\n"
              << "  Stereo Scanner - Offline Processing\n"
              << "========================================\n\n";

    // ---- 1. 加载配置 ----
    stereo_scanner::ScannerConfig config;
    if (fs::exists(args.configFile)) {
        config = stereo_scanner::ScannerConfig::fromFile(args.configFile);
    } else {
        std::cout << "[Offline] Config file not found, using defaults.\n";
    }

    // 命令行参数覆盖
    if (!args.imageDir.empty())   config.imageDir     = args.imageDir;
    if (!args.outputDir.empty())  config.outputDir    = args.outputDir;
    if (!args.engineDir.empty())  config.ffsEngineDir = args.engineDir;

    // ---- 2. 加载标定数据 ----
    std::cout << "[Offline] Loading calibration from: " << args.caliFile << "\n";
    stereo_scanner::StereoCalibrator calibrator;
    if (!calibrator.load(args.caliFile)) {
        std::cerr << "[Offline] Failed to load calibration. Run stereo_calibrate first.\n";
        return 1;
    }
    auto& caliData = calibrator.data();
    std::cout << "[Offline] Baseline: " << caliData.baseline * 1000 << " mm\n";
    std::cout << "[Offline] fx=" << caliData.fx << " fy=" << caliData.fy << "\n";

    // ---- 3. 加载图像序列 ----
    std::cout << "[Offline] Loading image sequence from: " << config.imageDir << "\n";
    ImageSequence seq = loadImageSequence(config.imageDir, config.imageFormat);
    if (seq.empty()) {
        std::cerr << "[Offline] No image pairs found in: " << config.imageDir << "\n";
        return 1;
    }

    // 截取帧范围
    int startIdx = args.startFrame;
    int endIdx   = args.maxFrames > 0
                    ? std::min(startIdx + args.maxFrames, seq.size())
                    : seq.size();
    int totalFrames = endIdx - startIdx;

    std::cout << "[Offline] Total frames: " << seq.size()
              << " | Processing: " << startIdx << " - " << endIdx - 1
              << " (" << totalFrames << " frames)\n";

    // ---- 4. 初始化 FFS 推理引擎 ----
    std::cout << "[Offline] Initializing FFS engine from: " << config.ffsEngineDir << "\n";
    stereo_scanner::FFSInference ffs;
    if (!ffs.init(config.ffsEngineDir)) {
        std::cerr << "[Offline] FFS engine init failed. Run scripts/export_ffs_engine.py first.\n";
        return 1;
    }

    // ---- 5. 初始化 RTAB-Map SLAM ----
    std::cout << "[Offline] Initializing RTAB-Map SLAM...\n";
    stereo_scanner::ScannerSLAM slam;
    if (!slam.init(config, caliData)) {
        std::cerr << "[Offline] SLAM init failed.\n";
        return 1;
    }

    // ---- 6. 创建输出目录 ----
    fs::create_directories(config.outputDir);
    if (args.saveIntermediate) {
        fs::create_directories(config.outputDir + "/disparity");
        fs::create_directories(config.outputDir + "/depth");
    }

    // ---- 7. 批处理流水线 ----
    std::cout << "\n[Offline] Starting batch processing...\n\n";

    int processedFrames = 0;
    int loopClosures = 0;
    int failedFrames = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = startIdx; i < endIdx; ++i) {
        // 加载图像
        cv::Mat leftRaw  = cv::imread(seq.leftPaths[i]);
        cv::Mat rightRaw = cv::imread(seq.rightPaths[i]);

        if (leftRaw.empty() || rightRaw.empty()) {
            std::cerr << "[Offline] Frame " << i << ": read failed, skipped.\n";
            failedFrames++;
            continue;
        }

        double ts = seq.timestamps[i];

        // Step 1: 极线校正
        cv::Mat leftRect, rightRect;
        calibrator.rectifyPair(leftRaw, rightRaw, leftRect, rightRect);

        // Step 2: FFS 推理
        cv::Mat disparity, depth;
        bool inferOK = ffs.infer(leftRect, rightRect, caliData, disparity, depth);
        if (!inferOK) {
            std::cerr << "[Offline] Frame " << i << ": FFS inference failed, skipped.\n";
            failedFrames++;
            continue;
        }

        // Step 3: RTAB-Map SLAM 处理
        stereo_scanner::ScanResult result = slam.processFrame(leftRect, depth, ts);
        if (result.success) {
            processedFrames++;
            loopClosures += result.loopClosureCount;
        } else {
            failedFrames++;
        }

        // 可视化
        if (args.visualize && i % 3 == 0) {  // 每3帧刷新一次显示, 减少GUI开销
            cv::Mat dispVis = disparityToColorMap(disparity, config.ffsMaxDisp);
            cv::Mat depthVis = depthToColorMap(depth, config.maxDepth);

            cv::Mat topRow;
            cv::hconcat(leftRect, rightRect, topRow);
            cv::Mat botRow;
            cv::hconcat(dispVis, depthVis, botRow);
            cv::Mat canvas;
            cv::vconcat(topRow, botRow, canvas);

            // 状态信息叠加
            std::string info = "Frame: " + std::to_string(i)
                              + " | Processed: " + std::to_string(processedFrames)
                              + " | Loops: " + std::to_string(loopClosures);
            cv::putText(canvas, info, cv::Point(10, 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

            cv::imshow("Offline Processing", canvas);
            int key = cv::waitKey(1);
            if (key == 'q' || key == 'Q') {
                std::cout << "[Offline] User interrupted at frame " << i << ".\n";
                break;
            }
        }

        // 保存中间结果
        if (args.saveIntermediate) {
            cv::imwrite(config.outputDir + "/disparity/disp_" + std::to_string(i) + ".png",
                        disparityToColorMap(disparity, config.ffsMaxDisp));
            // 深度图保存为原始 float (用 FileStorage)
            cv::FileStorage fsD(config.outputDir + "/depth/depth_" + std::to_string(i) + ".yml",
                                cv::FileStorage::WRITE);
            fsD << "depth" << depth;
            fsD.release();
        }

        // 进度报告（每100帧）
        if ((i - startIdx) % 100 == 0 && i > startIdx) {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(now - startTime).count();
            double fps = (i - startIdx) / elapsed;
            double eta = (endIdx - i) / fps;
            std::cout << "[Offline] Progress: " << (i - startIdx) << "/" << totalFrames
                      << " | FPS: " << std::fixed << std::setprecision(1) << fps
                      << " | ETA: " << std::setprecision(0) << eta << "s\n";
        }
    }

    cv::destroyAllWindows();

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalTime = std::chrono::duration<double>(endTime - startTime).count();

    // ---- 8. 导出结果 ----
    std::cout << "\n[Offline] Processing complete.\n";
    std::cout << "  Frames processed: " << processedFrames << "\n";
    std::cout << "  Frames failed:    " << failedFrames << "\n";
    std::cout << "  Loop closures:    " << loopClosures << "\n";
    std::cout << "  Total time:       " << totalTime << "s\n";
    std::cout << "  Average FPS:      " << std::fixed << std::setprecision(2)
              << processedFrames / totalTime << "\n\n";

    if (config.exportPointCloud) {
        std::string cloudPath = config.outputDir + "/global_cloud.ply";
        std::cout << "[Offline] Exporting point cloud to: " << cloudPath << "\n";
        slam.exportPointCloud(cloudPath, config.voxelSize);
    }

    if (config.exportTrajectory) {
        std::string trajPath = config.outputDir + "/trajectory_kitti.txt";
        std::cout << "[Offline] Exporting trajectory to: " << trajPath << "\n";
        slam.exportTrajectory(trajPath);
    }

    // 导出 RTAB-Map 数据库（如果配置了）
    if (!config.rtabmapDatabase.empty()) {
        std::cout << "[Offline] RTAB-Map database saved to: " << config.rtabmapDatabase << "\n";
    }

    std::cout << "\n[Offline] All done.\n";
    return 0;
}
