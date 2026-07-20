#include "stereo_calibrator.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include <numeric>

namespace stereo_scanner {

StereoCalibrator::StereoCalibrator() {}

void StereoCalibrator::addImagePair(const cv::Mat& left, const cv::Mat& right) {
    if (left.empty() || right.empty()) {
        std::cerr << "[Calibrator] Empty image pair, skipped.\n";
        return;
    }
    if (!leftImages_.empty() && left.size() != leftImages_[0].size()) {
        std::cerr << "[Calibrator] Image size mismatch, skipped.\n";
        return;
    }
    leftImages_.push_back(left.clone());
    rightImages_.push_back(right.clone());
}

double StereoCalibrator::calibrate(cv::Size boardSize, double squareSize) {
    if (leftImages_.size() < 10) {
        std::cerr << "[Calibrator] Need at least 10 image pairs, have "
                  << leftImages_.size() << ".\n";
        return -1.0;
    }

    boardSize_ = boardSize;

    // 1. 检测所有图像中的棋盘格角点
    std::vector<std::vector<cv::Point2f>> leftCorners, rightCorners;
    std::vector<cv::Mat> goodLeft, goodRight;
    cv::Size imageSize = leftImages_[0].size();

    for (size_t i = 0; i < leftImages_.size(); ++i) {
        std::vector<cv::Point2f> lPts, rPts;
        bool lFound = cv::findChessboardCorners(leftImages_[i], boardSize, lPts,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);
        bool rFound = cv::findChessboardCorners(rightImages_[i], boardSize, rPts,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);

        if (!lFound || !rFound) {
            std::cout << "[Calibrator] Pair " << i << ": chessboard not found, skipped.\n";
            continue;
        }

        // 亚像素精化
        cv::Mat leftGray, rightGray;
        cv::cvtColor(leftImages_[i], leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(rightImages_[i], rightGray, cv::COLOR_BGR2GRAY);
        cv::cornerSubPix(leftGray, lPts, cv::Size(11, 11), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.01));
        cv::cornerSubPix(rightGray, rPts, cv::Size(11, 11), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.01));

        leftCorners.push_back(lPts);
        rightCorners.push_back(rPts);
        goodLeft.push_back(leftImages_[i]);
        goodRight.push_back(rightImages_[i]);
    }

    if (leftCorners.size() < 8) {
        std::cerr << "[Calibrator] Not enough valid pairs: " << leftCorners.size() << ".\n";
        return -1.0;
    }

    std::cout << "[Calibrator] Valid pairs: " << leftCorners.size()
              << " / " << leftImages_.size() << "\n";

    // 2. 构建物理坐标点
    std::vector<cv::Point3f> objectPoints;
    for (int r = 0; r < boardSize.height; ++r) {
        for (int c = 0; c < boardSize.width; ++c) {
            objectPoints.push_back(cv::Point3f(c * squareSize, r * squareSize, 0));
        }
    }
    std::vector<std::vector<cv::Point3f>> objPtsList(leftCorners.size(), objectPoints);

    // 3. 单目标定（先粗估内参+畸变）
    std::vector<cv::Mat> rvecsL, tvecsL, rvecsR, tvecsR;
    double rmsL = cv::calibrateCamera(objPtsList, leftCorners, imageSize,
        caliData_.M1, caliData_.D1, rvecsL, tvecsL);
    double rmsR = cv::calibrateCamera(objPtsList, rightCorners, imageSize,
        caliData_.M2, caliData_.D2, rvecsR, tvecsR);

    std::cout << "[Calibrator] Left mono RMS:  " << rmsL << "\n";
    std::cout << "[Calibrator] Right mono RMS: " << rmsR << "\n";

    // 4. 双目标定（精估外参 R, T + 优化内参）
    double rms = cv::stereoCalibrate(objPtsList, leftCorners, rightCorners,
        caliData_.M1, caliData_.D1, caliData_.M2, caliData_.D2,
        imageSize, caliData_.R, caliData_.T, caliData_.E, caliData_.F,
        cv::CALIB_FIX_INTRINSIC,   // 锁定内参只优化外参
        cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 100, 1e-6));

    std::cout << "[Calibrator] Stereo RMS: " << rms << "\n";

    // 5. 计算极线校正参数
    cv::stereoRectify(caliData_.M1, caliData_.D1, caliData_.M2, caliData_.D2,
        imageSize, caliData_.R, caliData_.T,
        caliData_.R1, caliData_.P1, caliData_.R2, caliData_.P2, caliData_.Q,
        cv::CALIB_ZERO_DISPARITY, 0, imageSize);

    // 6. 计算基线距离
    // T是左→右的平移向量, baseline = |Tx|
    // stereoRectify 后 P1/P2 的 Tx 差值也可用来算
    caliData_.baseline = std::abs(caliData_.T.at<double>(0, 0));  // 米
    if (caliData_.baseline <= 0) {
        // fallback: 从 P1/P2 算
        double Tx1 = caliData_.P1.at<double>(0, 3);
        double Tx2 = caliData_.P2.at<double>(0, 3);
        double fxP = caliData_.P2.at<double>(0, 0);
        caliData_.baseline = std::abs(Tx2 - Tx1) / fxP;
    }

    caliData_.imageSize = imageSize;
    caliData_.extractIntrinsics();

    // 7. 计算校正映射表
    computeRectifyMaps();

    std::cout << "[Calibrator] Baseline: " << caliData_.baseline * 1000 << " mm\n";
    std::cout << "[Calibrator] fx=" << caliData_.fx << " fy=" << caliData_.fy
              << " cx=" << caliData_.cx << " cy=" << caliData_.cy << "\n";

    // 替换原始列表为有效列表
    leftImages_ = goodLeft;
    rightImages_ = goodRight;

    return rms;
}

void StereoCalibrator::computeRectifyMaps() {
    if (caliData_.M1.empty() || caliData_.imageSize.width == 0) {
        std::cerr << "[Calibrator] No calibration data, cannot compute maps.\n";
        return;
    }

    cv::initUndistortRectifyMap(caliData_.M1, caliData_.D1, caliData_.R1, caliData_.P1,
        caliData_.imageSize, CV_32FC1, caliData_.mapLX, caliData_.mapLY);
    cv::initUndistortRectifyMap(caliData_.M2, caliData_.D2, caliData_.R2, caliData_.P2,
        caliData_.imageSize, CV_32FC1, caliData_.mapRX, caliData_.mapRY);

    std::cout << "[Calibrator] Rectify maps computed.\n";
}

void StereoCalibrator::rectifyPair(const cv::Mat& leftRaw, const cv::Mat& rightRaw,
                                   cv::Mat& leftRect, cv::Mat& rightRect) const {
    cv::remap(leftRaw, leftRect, caliData_.mapLX, caliData_.mapLY, cv::INTER_LINEAR);
    cv::remap(rightRaw, rightRect, caliData_.mapRX, caliData_.mapRY, cv::INTER_LINEAR);
}

bool StereoCalibrator::save(const std::string& path) const {
    if (!caliData_.valid()) {
        std::cerr << "[Calibrator] No valid calibration data to save.\n";
        return false;
    }

    cv::FileStorage fs(path, cv::FileStorage::WRITE);

    fs << "image_width"  << caliData_.imageSize.width;
    fs << "image_height" << caliData_.imageSize.height;
    fs << "board_cols"   << boardSize_.width;
    fs << "board_rows"   << boardSize_.height;
    fs << "baseline_m"   << caliData_.baseline;

    fs << "M1" << caliData_.M1;
    fs << "D1" << caliData_.D1;
    fs << "M2" << caliData_.M2;
    fs << "D2" << caliData_.D2;
    fs << "R"  << caliData_.R;
    fs << "T"  << caliData_.T;
    fs << "E"  << caliData_.E;
    fs << "F"  << caliData_.F;
    fs << "R1" << caliData_.R1;
    fs << "P1" << caliData_.P1;
    fs << "R2" << caliData_.R2;
    fs << "P2" << caliData_.P2;
    fs << "Q"  << caliData_.Q;

    fs << "mapLX" << caliData_.mapLX;
    fs << "mapLY" << caliData_.mapLY;
    fs << "mapRX" << caliData_.mapRX;
    fs << "mapRY" << caliData_.mapRY;

    fs.release();
    std::cout << "[Calibrator] Saved to: " << path << "\n";
    return true;
}

bool StereoCalibrator::load(const std::string& path) {
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "[Calibrator] Cannot open: " << path << "\n";
        return false;
    }

    int w = fs["image_width"];
    int h = fs["image_height"];
    caliData_.imageSize = cv::Size(w, h);

    int bc = fs["board_cols"];
    int br = fs["board_rows"];
    boardSize_ = cv::Size(bc, br);

    fs["baseline_m"] >> caliData_.baseline;

    fs["M1"] >> caliData_.M1;
    fs["D1"] >> caliData_.D1;
    fs["M2"] >> caliData_.M2;
    fs["D2"] >> caliData_.D2;
    fs["R"]  >> caliData_.R;
    fs["T"]  >> caliData_.T;
    fs["E"]  >> caliData_.E;
    fs["F"]  >> caliData_.F;
    fs["R1"] >> caliData_.R1;
    fs["P1"] >> caliData_.P1;
    fs["R2"] >> caliData_.R2;
    fs["P2"] >> caliData_.P2;
    fs["Q"]  >> caliData_.Q;

    fs["mapLX"] >> caliData_.mapLX;
    fs["mapLY"] >> caliData_.mapLY;
    fs["mapRX"] >> caliData_.mapRX;
    fs["mapRY"] >> caliData_.mapRY;

    caliData_.extractIntrinsics();

    fs.release();

    if (!caliData_.valid()) {
        std::cerr << "[Calibrator] Loaded data is incomplete.\n";
        return false;
    }

    std::cout << "[Calibrator] Loaded from: " << path << "\n";
    std::cout << "[Calibrator] Baseline: " << caliData_.baseline * 1000 << " mm\n";
    std::cout << "[Calibrator] fx=" << caliData_.fx << " fy=" << caliData_.fy
              << " cx=" << caliData_.cx << " cy=" << caliData_.cy << "\n";
    return true;
}

bool StereoCalibrator::writeKTxt(const std::string& path) const {
    if (!caliData_.valid()) return false;

    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[Calibrator] Cannot write K.txt to: " << path << "\n";
        return false;
    }

    // 第1行: 3x3内参矩阵展平 (fx 0 cx 0 fy cy 0 0 1)
    f << caliData_.fx << " 0.0 " << caliData_.cx << " "
      << "0.0 " << caliData_.fy << " " << caliData_.cy << " "
      << "0.0 0.0 1.0\n";

    // 第2行: baseline (米)
    f << caliData_.baseline << "\n";

    f.close();
    std::cout << "[Calibrator] K.txt written to: " << path << "\n";
    return true;
}

} // namespace stereo_scanner
