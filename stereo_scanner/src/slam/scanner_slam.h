#pragma once
#include "common/types.h"
#include "common/config.h"
#include "stereo/ffs_inference.h"
#include <string>
#include <map>

// RTAB-Map Transform (需要完整定义，用于 std::map)
#include <rtabmap/core/Transform.h>

// RTAB-Map 前置声明
namespace rtabmap {
    class Rtabmap;
    class Odometry;
    class OdometryInfo;
    class SensorData;
    class CameraModel;
    class Statistics;
}

namespace stereo_scanner {

// 扫描结果
struct ScanResult {
    std::map<int, rtabmap::Transform> optimizedPoses;
    int totalFrames;
    int loopClosureCount;
    bool success;
};

class ScannerSLAM {
public:
    ScannerSLAM();
    ~ScannerSLAM();

    // 初始化 RTAB-Map SLAM 系统
    bool init(const ScannerConfig& config, const CalibrationData& cali);

    // 处理一帧 RGB-D 数据
    // rgb: 校正后的左图 BGR CV_8UC3
    // depth: 米制深度 CV_32FC1
    // timestamp: 秒
    ScanResult processFrame(const cv::Mat& rgb, const cv::Mat& depth, double timestamp);

    // 从 StereoPair + FFS 推理结果处理一帧（完整流水线）
    // 自动完成: 校正 → FFS推理 → 深度转换 → SLAM处理
    ScanResult processStereoPair(const StereoPair& rawPair,
                                 const CalibrationData& cali,
                                 FFSInference& ffs);

    // 获取当前所有优化位姿
    ScanResult getCurrentResult();

    // 导出全局点云到 PLY 文件
    bool exportPointCloud(const std::string& path, float voxelSize);

    // 导出轨迹文件
    bool exportTrajectory(const std::string& path);

    // 检测到回环的次数
    int loopClosureCount() const;

    bool isInitialized() const { return initialized_; }

private:
    bool initialized_;
    ScannerConfig config_;
    CalibrationData caliData_;
    int frameId_;  // 递增的帧 ID

    // RTAB-Map 核心对象
    rtabmap::Rtabmap* rtabmap_;
    rtabmap::Odometry* odom_;

    // 相机模型（RTAB-Map 格式）
    rtabmap::CameraModel* cameraModel_;

    // 里程计位姿修正
    rtabmap::Transform* odomCorrection_;
};

} // namespace stereo_scanner
