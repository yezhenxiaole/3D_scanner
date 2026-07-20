/**
 * @file main_calibrate.cpp
 * @brief 双目立体标定工具 — 采集棋盘格图像对 + 执行标定 + 保存结果
 *
 * 使用方式:
 *   1. 实时采集模式: stereo_calibrate --capture
 *      - 打开两个摄像头，按空格采集，按 q 结束并标定
 *   2. 离线标定模式: stereo_calibrate --dir <image_dir>
 *      - 从目录加载已有的 left_*.png / right_*.png 图像对
 *   3. 仅加载模式: stereo_calibrate --verify <cali.yml>
 *      - 加载已有标定文件，显示校正效果
 */

#include "common/config.h"
#include "common/types.h"
#include "calibration/stereo_calibrator.h"

#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;

// 前置声明
static void verifyRectification(const stereo_scanner::StereoCalibrator& calibrator);

// ============================================================================
// 命令行参数解析
// ============================================================================
struct CalibArgs {
    bool captureMode = false;      // 实时采集模式
    bool verifyMode = false;       // 仅验证校正效果
    std::string imageDir;          // 离线图像目录
    std::string caliFile;          // 标定文件路径 (输入/输出)
    std::string configFile;        // 配置文件路径
    int leftCamId = 0;             // 左相机 ID
    int rightCamId = 1;            // 右相机 ID
    cv::Size boardSize{9, 6};       // 棋盘格尺寸
    double squareSize = 0.025;     // 方格尺寸 (米)
    int minPairs = 20;             // 最少图像对数
    bool saveCorners = false;      // 是否保存角点可视化图
};

static CalibArgs parseArgs(int argc, char** argv) {
    CalibArgs args;
    args.caliFile = "config/cali.yml";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--capture" || arg == "-c") {
            args.captureMode = true;
        } else if (arg == "--verify" || arg == "-v") {
            args.verifyMode = true;
        } else if (arg == "--dir" || arg == "-d") {
            if (i + 1 < argc) args.imageDir = argv[++i];
        } else if (arg == "--cali" || arg == "-f") {
            if (i + 1 < argc) args.caliFile = argv[++i];
        } else if (arg == "--config") {
            if (i + 1 < argc) args.configFile = argv[++i];
        } else if (arg == "--left-cam") {
            if (i + 1 < argc) args.leftCamId = std::stoi(argv[++i]);
        } else if (arg == "--right-cam") {
            if (i + 1 < argc) args.rightCamId = std::stoi(argv[++i]);
        } else if (arg == "--board-cols") {
            if (i + 1 < argc) args.boardSize.width = std::stoi(argv[++i]);
        } else if (arg == "--board-rows") {
            if (i + 1 < argc) args.boardSize.height = std::stoi(argv[++i]);
        } else if (arg == "--square-size") {
            if (i + 1 < argc) args.squareSize = std::stod(argv[++i]);
        } else if (arg == "--min-pairs") {
            if (i + 1 < argc) args.minPairs = std::stoi(argv[++i]);
        } else if (arg == "--save-corners") {
            args.saveCorners = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage:\n"
                      << "  stereo_calibrate --capture [--cali file] [--left-cam 0] [--right-cam 1]\n"
                      << "  stereo_calibrate --dir <path> [--cali file]\n"
                      << "  stereo_calibrate --verify <cali.yml>\n"
                      << "\nOptions:\n"
                      << "  --capture       实时采集棋盘格图像对\n"
                      << "  --dir <path>    从目录加载已有图像对\n"
                      << "  --verify <f>    加载标定文件验证校正效果\n"
                      << "  --cali <f>      标定文件路径 (默认 config/cali.yml)\n"
                      << "  --config <f>    配置文件路径\n"
                      << "  --left-cam <n>  左相机 ID (默认 0)\n"
                      << "  --right-cam <n> 右相机 ID (默认 1)\n"
                      << "  --board-cols <n> 棋盘格列数 (默认 9)\n"
                      << "  --board-rows <n> 棋盘格行数 (默认 6)\n"
                      << "  --square-size <m> 方格尺寸(米) (默认 0.025)\n"
                      << "  --min-pairs <n> 最少采集数 (默认 20)\n"
                      << "  --save-corners  保存角点可视化图\n";
            exit(0);
        }
    }

    // 从配置文件补充参数
    if (!args.configFile.empty()) {
        stereo_scanner::ScannerConfig cfg =
            stereo_scanner::ScannerConfig::fromFile(args.configFile);
        args.leftCamId = cfg.leftCameraId;
        args.rightCamId = cfg.rightCameraId;
        args.boardSize = cfg.chessboardSize;
        args.squareSize = cfg.chessboardSquareSize;
        args.minPairs = cfg.minCalibrationPairs;
    }

    return args;
}

// ============================================================================
// 实时采集模式
// ============================================================================
static int captureAndCalibrate(const CalibArgs& args) {
    std::cout << "[Calibrate] Capture mode: left_cam=" << args.leftCamId
              << " right_cam=" << args.rightCamId << "\n";
    std::cout << "[Calibrate] Press SPACE to capture, Q to quit and calibrate.\n";

    cv::VideoCapture capL(args.leftCamId);
    cv::VideoCapture capR(args.rightCamId);

    if (!capL.isOpened()) {
        std::cerr << "[Calibrate] Cannot open left camera (id=" << args.leftCamId << ")\n";
        return 1;
    }
    if (!capR.isOpened()) {
        std::cerr << "[Calibrate] Cannot open right camera (id=" << args.rightCamId << ")\n";
        return 1;
    }

    // 设置分辨率（建议 <1000px 且 32 的倍数）
    capL.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    capL.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    capR.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    capR.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    stereo_scanner::StereoCalibrator calibrator;
    int capturedCount = 0;
    std::string saveDir = "calibration_capture";
    fs::create_directories(saveDir);

    while (true) {
        cv::Mat frameL, frameR;
        capL >> frameL;
        capR >> frameR;

        if (frameL.empty() || frameR.empty()) {
            std::cerr << "[Calibrate] Frame grab failed.\n";
            break;
        }

        // 检测棋盘格（实时预览）
        std::vector<cv::Point2f> ptsL, ptsR;
        bool foundL = cv::findChessboardCorners(frameL, args.boardSize, ptsL,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_FAST_CHECK);
        bool foundR = cv::findChessboardCorners(frameR, args.boardSize, ptsR,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_FAST_CHECK);

        // 显示预览
        cv::Mat displayL = frameL.clone();
        cv::Mat displayR = frameR.clone();
        if (foundL) cv::drawChessboardCorners(displayL, args.boardSize, ptsL, foundL);
        if (foundR) cv::drawChessboardCorners(displayR, args.boardSize, ptsR, foundR);

        // 状态叠加
        std::string status = "Captured: " + std::to_string(capturedCount)
                            + " / " + std::to_string(args.minPairs)
                            + " | Chessboard: " + (foundL && foundR ? "OK" : "NO");
        cv::putText(displayL, status, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    (foundL && foundR ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255)), 2);

        cv::hconcat(displayL, displayR, displayL);
        cv::imshow("Stereo Calibrate", displayL);

        int key = cv::waitKey(30);
        if (key == ' ' && foundL && foundR) {
            // 采集当前帧
            calibrator.addImagePair(frameL, frameR);
            capturedCount++;

            // 保存原始图到磁盘（便于后续复用）
            std::string idx = std::to_string(capturedCount);
            cv::imwrite(saveDir + "/left_" + idx + ".png", frameL);
            cv::imwrite(saveDir + "/right_" + idx + ".png", frameR);

            std::cout << "[Calibrate] Pair " << capturedCount << " captured.\n";

            if (capturedCount >= args.minPairs) {
                std::cout << "[Calibrate] Reached minimum pairs. Press Q to calibrate, "
                          << "or continue capturing for better accuracy.\n";
            }
        } else if (key == 'q' || key == 'Q') {
            break;
        }
    }

    capL.release();
    capR.release();
    cv::destroyAllWindows();

    if (capturedCount < args.minPairs) {
        std::cerr << "[Calibrate] Only " << capturedCount << " pairs captured, "
                  << "minimum " << args.minPairs << " required.\n";
        return 1;
    }

    // 执行标定
    std::cout << "[Calibrate] Running stereo calibration...\n";
    double rms = calibrator.calibrate(args.boardSize, args.squareSize);

    if (rms < 0) {
        std::cerr << "[Calibrate] Calibration failed.\n";
        return 1;
    }

    std::cout << "[Calibrate] RMS reprojection error: " << rms << "\n";

    // 保存标定结果
    if (calibrator.save(args.caliFile)) {
        std::cout << "[Calibrate] Calibration saved to: " << args.caliFile << "\n";
    }

    // 生成 FFS 需要的 K.txt
    std::string kTxtPath = args.caliFile.substr(0, args.caliFile.find_last_of('/')) + "/K.txt";
    if (args.caliFile.find('/') == std::string::npos) {
        kTxtPath = "config/K.txt";
    }
    calibrator.writeKTxt(kTxtPath);

    // 保存角点可视化
    if (args.saveCorners) {
        auto& caliData = calibrator.data();
        for (int i = 0; i < calibrator.pairCount(); ++i) {
            cv::Mat vis;
            cv::hconcat(calibrator.leftImages()[i], calibrator.rightImages()[i], vis);
            cv::imwrite(saveDir + "/cali_corners_" + std::to_string(i) + ".png", vis);
        }
    }

    // 验证校正效果
    std::cout << "[Calibrate] Verifying rectification...\n";
    verifyRectification(calibrator);

    return 0;
}

// ============================================================================
// 校正效果验证
// ============================================================================
static void verifyRectification(const stereo_scanner::StereoCalibrator& calibrator) {
    if (!calibrator.data().valid() || calibrator.pairCount() == 0) return;

    // 取第一对图像做校正演示
    cv::Mat leftRect, rightRect;
    calibrator.rectifyPair(calibrator.leftImages()[0], calibrator.rightImages()[0],
                           leftRect, rightRect);

    // 画极线验证: 同一水平线上对应点应该在同一行
    cv::Mat canvas;
    cv::hconcat(leftRect, rightRect, canvas);

    // 每隔 50 像素画一条水平线
    for (int y = 0; y < canvas.rows; y += 50) {
        cv::line(canvas, cv::Point(0, y), cv::Point(canvas.cols, y),
                 cv::Scalar(0, 255, 0), 1);
    }

    cv::imshow("Rectified + Epipolar Lines", canvas);
    std::cout << "[Calibrate] Press any key to close verification window.\n";
    cv::waitKey(0);
    cv::destroyAllWindows();
}

// ============================================================================
// 离线标定模式
// ============================================================================
static int calibrateFromDir(const CalibArgs& args) {
    if (args.imageDir.empty()) {
        std::cerr << "[Calibrate] --dir requires a directory path.\n";
        return 1;
    }

    std::cout << "[Calibrate] Offline mode: loading from " << args.imageDir << "\n";

    stereo_scanner::StereoCalibrator calibrator;
    int loadedCount = 0;

    // 搜索 left_*.png / right_*.png 格式的图像对
    // 也支持 left/ right/ 子目录格式
    for (int i = 0; i < 1000; ++i) {  // 最多 1000 对
        std::string idx = std::to_string(i);

        // 格式1: left_0.png / right_0.png
        std::string leftPath = args.imageDir + "/left_" + idx + ".png";
        std::string rightPath = args.imageDir + "/right_" + idx + ".png";

        // 格式2: left_0.jpg / right_0.jpg
        if (!fs::exists(leftPath)) {
            leftPath = args.imageDir + "/left_" + idx + ".jpg";
            rightPath = args.imageDir + "/right_" + idx + ".jpg";
        }

        // 格式3: left/0.png / right/0.png
        if (!fs::exists(leftPath)) {
            leftPath = args.imageDir + "/left/" + idx + ".png";
            rightPath = args.imageDir + "/right/" + idx + ".png";
        }

        if (!fs::exists(leftPath) || !fs::exists(rightPath)) {
            break;  // 不再连续编号, 停止搜索
        }

        cv::Mat leftImg = cv::imread(leftPath);
        cv::Mat rightImg = cv::imread(rightPath);

        if (leftImg.empty() || rightImg.empty()) continue;

        calibrator.addImagePair(leftImg, rightImg);
        loadedCount++;
    }

    // 如果没找到编号格式, 尝试扫描目录中所有图像
    if (loadedCount == 0 && fs::exists(args.imageDir)) {
        std::vector<std::string> leftFiles, rightFiles;
        for (auto& entry : fs::directory_iterator(args.imageDir)) {
            std::string name = entry.path().filename().string();
            std::string ext = entry.path().extension().string();
            bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".bmp");
            if (name.find("left") != std::string::npos && isImage) {
                leftFiles.push_back(entry.path().string());
            }
            if (name.find("right") != std::string::npos && isImage) {
                rightFiles.push_back(entry.path().string());
            }
        }
        std::sort(leftFiles.begin(), leftFiles.end());
        std::sort(rightFiles.begin(), rightFiles.end());

        for (size_t i = 0; i < std::min(leftFiles.size(), rightFiles.size()); ++i) {
            cv::Mat leftImg = cv::imread(leftFiles[i]);
            cv::Mat rightImg = cv::imread(rightFiles[i]);
            if (!leftImg.empty() && !rightImg.empty()) {
                calibrator.addImagePair(leftImg, rightImg);
                loadedCount++;
            }
        }
    }

    if (loadedCount < 10) {
        std::cerr << "[Calibrate] Only " << loadedCount << " pairs loaded, need at least 10.\n";
        return 1;
    }

    std::cout << "[Calibrate] Loaded " << loadedCount << " image pairs.\n";

    double rms = calibrator.calibrate(args.boardSize, args.squareSize);
    if (rms < 0) {
        std::cerr << "[Calibrate] Calibration failed.\n";
        return 1;
    }

    std::cout << "[Calibrate] RMS reprojection error: " << rms << "\n";

    if (calibrator.save(args.caliFile)) {
        std::cout << "[Calibrate] Saved to: " << args.caliFile << "\n";
    }

    std::string kTxtPath = "config/K.txt";
    calibrator.writeKTxt(kTxtPath);

    verifyRectification(calibrator);

    return 0;
}

// ============================================================================
// 仅验证模式
// ============================================================================
static int verifyOnly(const CalibArgs& args) {
    stereo_scanner::StereoCalibrator calibrator;
    if (!calibrator.load(args.caliFile)) {
        std::cerr << "[Calibrate] Cannot load: " << args.caliFile << "\n";
        return 1;
    }

    auto& cali = calibrator.data();
    std::cout << "[Verify] Calibration loaded:\n";
    std::cout << "  Image size:  " << cali.imageSize << "\n";
    std::cout << "  Baseline:    " << cali.baseline * 1000 << " mm\n";
    std::cout << "  fx=" << cali.fx << " fy=" << cali.fy
              << " cx=" << cali.cx << " cy=" << cali.cy << "\n";

    // 尝试对几张图像做校正演示
    if (!args.imageDir.empty() && fs::exists(args.imageDir)) {
        // 找一对测试图
        cv::Mat left, right;
        for (auto& entry : fs::directory_iterator(args.imageDir)) {
            std::string name = entry.path().filename().string();
            if (name.find("left") != std::string::npos && left.empty()) {
                left = cv::imread(entry.path().string());
            }
            if (name.find("right") != std::string::npos && right.empty()) {
                right = cv::imread(entry.path().string());
            }
        }

        if (!left.empty() && !right.empty()) {
            cv::Mat leftRect, rightRect;
            calibrator.rectifyPair(left, right, leftRect, rightRect);

            cv::Mat canvas;
            cv::hconcat(leftRect, rightRect, canvas);
            for (int y = 0; y < canvas.rows; y += 50) {
                cv::line(canvas, cv::Point(0, y), cv::Point(canvas.cols, y),
                         cv::Scalar(0, 255, 0), 1);
            }
            cv::imshow("Rectification Verification", canvas);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    }

    return 0;
}

// ============================================================================
// 主入口
// ============================================================================
int main(int argc, char** argv) {
    CalibArgs args = parseArgs(argc, argv);

    std::cout << "========================================\n"
              << "  Stereo Scanner - Calibration Tool\n"
              << "========================================\n\n";

    if (args.verifyMode) {
        return verifyOnly(args);
    } else if (args.captureMode) {
        return captureAndCalibrate(args);
    } else if (!args.imageDir.empty()) {
        return calibrateFromDir(args);
    } else {
        std::cerr << "No mode specified. Use --capture, --dir, or --verify.\n"
                  << "Run with --help for usage.\n";
        return 1;
    }
}
