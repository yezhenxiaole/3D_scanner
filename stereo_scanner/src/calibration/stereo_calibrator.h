#pragma once
#include "common/types.h"
#include <vector>
#include <string>

namespace stereo_scanner {

class StereoCalibrator {
public:
    StereoCalibrator();

    // 添加标定图像对（实时采集或从文件加载）
    void addImagePair(const cv::Mat& left, const cv::Mat& right);

    // 执行双目标定
    // boardSize: 棋盘格内角点 (cols, rows)
    // squareSize: 方格物理尺寸(米)
    // 返回: 重投影误差(rms), <0 表示失败
    double calibrate(cv::Size boardSize, double squareSize);

    // 计算极线校正映射表（标定后调用）
    void computeRectifyMaps();

    // 校正一对图像
    void rectifyPair(const cv::Mat& leftRaw, const cv::Mat& rightRaw,
                     cv::Mat& leftRect, cv::Mat& rightRect) const;

    // 保存标定结果到 YAML
    bool save(const std::string& path) const;

    // 从 YAML 加载标定结果
    bool load(const std::string& path);

    // 生成 FFS 需要的 K.txt
    bool writeKTxt(const std::string& path) const;

    // 获取标定数据
    const CalibrationData& data() const { return caliData_; }
    CalibrationData& data() { return caliData_; }

    // 当前已添加的图像对数
    int pairCount() const { return static_cast<int>(leftImages_.size()); }

    // 获取标定图像（用于可视化）
    const std::vector<cv::Mat>& leftImages() const { return leftImages_; }
    const std::vector<cv::Mat>& rightImages() const { return rightImages_; }

private:
    std::vector<cv::Mat> leftImages_;
    std::vector<cv::Mat> rightImages_;
    CalibrationData caliData_;
    cv::Size boardSize_;     // 记住标定时的棋盘格尺寸
};

} // namespace stereo_scanner
