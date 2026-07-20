#pragma once
#include <opencv2/core.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

namespace stereo_scanner {

// 双目图像对
struct StereoPair {
    cv::Mat left;       // BGR uint8
    cv::Mat right;      // BGR uint8
    double timestamp;   // 秒

    StereoPair() : timestamp(0) {}
    StereoPair(const cv::Mat& l, const cv::Mat& r, double ts = 0)
        : left(l.clone()), right(r.clone()), timestamp(ts) {}

    bool valid() const { return !left.empty() && !right.empty(); }
};

// 单帧处理结果
struct FrameResult {
    cv::Mat rgb;            // BGR uint8, 校正后的左图
    cv::Mat disparity;      // CV_32FC1, 像素单位
    cv::Mat depth;          // CV_32FC1, 米制深度
    double timestamp;

    FrameResult() : timestamp(0) {}
    bool valid() const { return !depth.empty(); }
};

// 标定数据
struct CalibrationData {
    cv::Size imageSize;

    // 单目内参
    cv::Mat M1, D1;    // 左相机内参矩阵 3x3, 畸变 1x5
    cv::Mat M2, D2;    // 右相机内参矩阵 3x3, 畸变 1x5

    // 双目外参
    cv::Mat R, T;      // 左→右 旋转3x3, 平移3x1
    cv::Mat E, F;      // 本质矩阵, 基础矩阵

    // 校正映射
    cv::Mat R1, P1;    // 左校正旋转3x3, 投影3x4
    cv::Mat R2, P2;    // 右校正旋转3x3, 投影3x4
    cv::Mat Q;         // 视差→深度映射矩阵 4x4

    // 预计算的校正映射表（运行时直接用）
    cv::Mat mapLX, mapLY;  // 左图 remap 表
    cv::Mat mapRX, mapRY;  // 右图 remap 表

    // 派生参数
    double baseline;    // 基线距离(米)
    double fx, fy;      // 焦距(像素)
    double cx, cy;      // 主点(像素)

    CalibrationData()
        : baseline(0), fx(0), fy(0), cx(0), cy(0) {}

    bool valid() const {
        return !M1.empty() && !M2.empty() &&
               !R1.empty() && !mapLX.empty() && baseline > 0;
    }

    // 从内参矩阵提取焦距和主点
    void extractIntrinsics() {
        fx = M1.at<double>(0, 0);
        fy = M1.at<double>(1, 1);
        cx = M1.at<double>(0, 2);
        cy = M1.at<double>(1, 2);
    }
};

} // namespace stereo_scanner
