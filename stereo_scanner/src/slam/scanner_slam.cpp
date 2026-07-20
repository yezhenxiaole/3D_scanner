#include "scanner_slam.h"
#include "stereo/ffs_inference.h"

// RTAB-Map 核心 API
#include <rtabmap/core/Rtabmap.h>
#include <rtabmap/core/Odometry.h>
#include <rtabmap/core/OdometryInfo.h>
#include <rtabmap/core/CameraModel.h>
#include <rtabmap/core/SensorData.h>
#include <rtabmap/core/Parameters.h>
#include <rtabmap/core/Statistics.h>
#include <rtabmap/core/util3d.h>
#include <rtabmap/core/util3d_filtering.h>
#include <rtabmap/core/util3d_transforms.h>
#include <rtabmap/core/Graph.h>

// PCL
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/filters/filter.h>

#include <iostream>

namespace stereo_scanner {

using namespace rtabmap;

ScannerSLAM::ScannerSLAM()
    : initialized_(false)
    , rtabmap_(nullptr)
    , odom_(nullptr)
    , cameraModel_(nullptr)
    , odomCorrection_(nullptr)
    , frameId_(0)
{}

ScannerSLAM::~ScannerSLAM() {
    if (rtabmap_) {
        rtabmap_->close(true);  // 保存数据库
        delete rtabmap_;
    }
    delete odom_;
    delete cameraModel_;
    delete odomCorrection_;
}

bool ScannerSLAM::init(const ScannerConfig& config, const CalibrationData& cali) {
    config_ = config;
    caliData_ = cali;

    if (!cali.valid()) {
        std::cerr << "[SLAM] Invalid calibration data.\n";
        return false;
    }

    // ========================================================
    // 1. 构造 RTAB-Map CameraModel
    // ========================================================
    // FFS 输出的是校正后左图的深度，所以用校正后的内参 (P1 矩阵)
    cameraModel_ = new CameraModel(
        cali.fx, cali.fy, cali.cx, cali.cy,
        CameraModel::opticalRotation(),  // 光学系→机器人系的标准旋转
        0.0,                              // Tx (RGB-D模式设0)
        cali.imageSize
    );

    std::cout << "[SLAM] CameraModel: fx=" << cali.fx << " fy=" << cali.fy
              << " cx=" << cali.cx << " cy=" << cali.cy
              << " size=" << cali.imageSize << "\n";

    // ========================================================
    // 2. 配置 RTAB-Map 参数
    // ========================================================
    ParametersMap params;

    // RGB-D SLAM 模式
    params.insert(ParametersPair(Parameters::kRGBDEnabled(), "true"));

    // 里程计策略
    params.insert(ParametersPair(Parameters::kOdomStrategy(),
        std::to_string(config.odomStrategy)));

    // 更新阈值
    params.insert(ParametersPair(Parameters::kRGBDLinearUpdate(),
        std::to_string(config.rtabmapLinearUpdate)));
    params.insert(ParametersPair(Parameters::kRGBDAngularUpdate(),
        std::to_string(config.rtabmapAngularUpdate)));

    // 回环检测
    params.insert(ParametersPair(Parameters::kRtabmapLoopThr(),
        std::to_string(config.rtabmapLoopThr)));
    params.insert(ParametersPair(Parameters::kRGBDOptimizeMaxError(), "3.0"));

    // 空间近邻检测（提高配准质量）
    params.insert(ParametersPair(Parameters::kRGBDProximityBySpace(), "true"));
    params.insert(ParametersPair(Parameters::kRGBDProximityMaxGraphDepth(), "50"));
    params.insert(ParametersPair(Parameters::kRGBDProximityPathMaxNeighbors(), "10"));

    // 图优化
    params.insert(ParametersPair(Parameters::kOptimizerStrategy(), "1"));  // g2o
    params.insert(ParametersPair(Parameters::kOptimizerIterations(), "20"));

    // 特征检测
    params.insert(ParametersPair(Parameters::kKpDetectorStrategy(), "8"));   // GFTT/ORB
    params.insert(ParametersPair(Parameters::kKpMaxFeatures(), "500"));

    // 对极几何验证回环
    params.insert(ParametersPair(Parameters::kVhEpEnabled(), "true"));
    params.insert(ParametersPair(Parameters::kVhEpMatchCountMin(), "8"));

    // 记忆管理: 工作内存最大节点数
    params.insert(ParametersPair(Parameters::kRtabmapMemoryThr(), "50"));

    // ========================================================
    // 3. 创建里程计
    // ========================================================
    odom_ = Odometry::create(params);
    if (!odom_) {
        std::cerr << "[SLAM] Failed to create Odometry.\n";
        return false;
    }

    // ========================================================
    // 4. 创建并初始化 Rtabmap
    // ========================================================
    rtabmap_ = new Rtabmap();
    rtabmap_->init(params, config.rtabmapDatabase);

    odomCorrection_ = new Transform(Transform::getIdentity());

    initialized_ = true;
    frameId_ = 0;
    std::cout << "[SLAM] RTAB-Map initialized.\n";
    std::cout << "[SLAM]   Loop threshold: " << config.rtabmapLoopThr << "\n";
    std::cout << "[SLAM]   Database: "
              << (config.rtabmapDatabase.empty() ? "(RAM)" : config.rtabmapDatabase) << "\n";

    return true;
}

ScanResult ScannerSLAM::processFrame(const cv::Mat& rgb, const cv::Mat& depth,
                                      double timestamp) {
    ScanResult result;
    result.totalFrames = 0;
    result.loopClosureCount = 0;
    result.success = false;

    if (!initialized_) {
        std::cerr << "[SLAM] Not initialized.\n";
        return result;
    }

    // ========================================================
    // 数据桥接: FFS 深度图 → RTAB-Map SensorData
    // ========================================================
    // FFS 输出: depth 是 CV_32FC1, 单位米
    // RTAB-Map 接受: CV_32FC1 (米) 或 CV_16UC1 (毫米)
    // 直接用 CV_32FC1 米制深度, 最精确

    // 过滤过远深度（使用 OpenCV 矩阵操作，避免逐像素循环）
    cv::Mat depthFiltered;
    if (config_.maxDepth > 0) {
        cv::Mat mask = (depth > 0) & (depth <= config_.maxDepth);
        depth.copyTo(depthFiltered, mask);
        // mask 为 0 的位置自动保持 0（Mat 默认初始化为 0）
    } else {
        depthFiltered = depth;
    }

    // 构造 SensorData（使用递增的帧 ID）
    int id = ++frameId_;
    SensorData sensorData(rgb, depthFiltered, *cameraModel_, id, timestamp);

    if (!sensorData.isValid()) {
        std::cerr << "[SLAM] Invalid SensorData.\n";
        return result;
    }

    // ========================================================
    // 里程计: 估计当前帧位姿
    // ========================================================
    OdometryInfo odomInfo;
    Transform odomPose = odom_->process(sensorData, &odomInfo);

    if (odomPose.isNull()) {
        // 里程计丢失
        std::cerr << "[SLAM] Odometry lost! Frame " << id << " skipped.\n";
        return result;
    }

    // ========================================================
    // SLAM: 回环检测 + 图优化
    // ========================================================
    bool added = rtabmap_->process(sensorData, odomPose);

    if (added) {
        result.success = true;
        result.totalFrames = 1;

        // 检查回环
        int loopId = rtabmap_->getLoopClosureId();
        if (loopId > 0) {
            result.loopClosureCount = 1;
            std::cout << "[SLAM] Loop closure detected! id=" << loopId
                      << " value=" << rtabmap_->getLoopClosureValue() << "\n";
        }

        // 获取地图修正（回环后位姿图优化结果）
        *odomCorrection_ = rtabmap_->getStatistics().mapCorrection();
    }

    return result;
}

ScanResult ScannerSLAM::processStereoPair(const StereoPair& rawPair,
                                           const CalibrationData& cali,
                                           FFSInference& ffs) {
    ScanResult result;
    result.success = false;

    if (!rawPair.valid()) return result;

    // Step 1: 极线校正（使用标定数据的 remap 表）
    cv::Mat leftRect, rightRect;
    if (!cali.mapLX.empty() && !cali.mapLY.empty()) {
        cv::remap(rawPair.left, leftRect, cali.mapLX, cali.mapLY, cv::INTER_LINEAR);
        cv::remap(rawPair.right, rightRect, cali.mapRX, cali.mapRY, cv::INTER_LINEAR);
    } else {
        std::cerr << "[SLAM] Rectification maps not available. Run calibration first.\n";
        return result;
    }

    // Step 2: FFS 推理 → 深度图（使用最快的 inferDepth 路径）
    cv::Mat depth;
    if (!ffs.inferDepth(leftRect, rightRect, cali.fx, cali.baseline, depth)) {
        std::cerr << "[SLAM] FFS inference failed.\n";
        return result;
    }

    // Step 3: 深度图 + RGB → RTAB-Map
    return processFrame(leftRect, depth, rawPair.timestamp);
}

ScanResult ScannerSLAM::getCurrentResult() {
    ScanResult result;
    result.success = false;

    if (!initialized_) return result;

    result.optimizedPoses = rtabmap_->getLocalOptimizedPoses();
    result.totalFrames = static_cast<int>(result.optimizedPoses.size());
    result.success = true;

    return result;
}

bool ScannerSLAM::exportPointCloud(const std::string& path, float voxelSize) {
    if (!initialized_) return false;

    std::cout << "[SLAM] Exporting point cloud to: " << path << "\n";

    // 获取完整图（含所有节点和位姿）
    std::map<int, Signature> nodes;
    std::map<int, Transform> poses;
    std::multimap<int, Link> links;
    rtabmap_->getGraph(poses, links, true, true,
                       &nodes, true, true, true, true);

    if (poses.empty()) {
        std::cerr << "[SLAM] No poses to export.\n";
        return false;
    }

    // 逐节点生成点云并变换到全局坐标系
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr globalCloud(
        new pcl::PointCloud<pcl::PointXYZRGB>);

    for (auto& iter : poses) {
        auto nodeIt = nodes.find(iter.first);
        if (nodeIt == nodes.end()) continue;

        Signature& node = nodeIt->second;
        node.sensorData().uncompressData();

        pcl::PointCloud<pcl::PointXYZRGB>::Ptr tmp =
            util3d::cloudRGBFromSensorData(
                node.sensorData(),
                config_.featureDecimation,
                config_.maxDepth,
                0.0f,
                0
            );

        if (!tmp || tmp->empty()) continue;

        // 去除 NaN
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr tmpNoNaN(
            new pcl::PointCloud<pcl::PointXYZRGB>);
        std::vector<int> indices;
        pcl::removeNaNFromPointCloud(*tmp, *tmpNoNaN, indices);

        if (tmpNoNaN->empty()) continue;

        // 用优化位姿变换到全局坐标系
        *globalCloud += *util3d::transformPointCloud(tmpNoNaN, iter.second);
    }

    if (globalCloud->empty()) {
        std::cerr << "[SLAM] Empty point cloud.\n";
        return false;
    }

    // 体素滤波降采样
    if (voxelSize > 0) {
        globalCloud = util3d::voxelize(globalCloud, voxelSize);
    }

    // 保存
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".ply") {
        pcl::io::savePLYFileBinary(path, *globalCloud);
    } else {
        pcl::io::savePCDFileBinary(path, *globalCloud);
    }

    std::cout << "[SLAM] Point cloud saved: " << globalCloud->size()
              << " points, " << path << "\n";
    return true;
}

bool ScannerSLAM::exportTrajectory(const std::string& path) {
    if (!initialized_) return false;

    std::map<int, Transform> poses;
    std::multimap<int, Link> links;
    rtabmap_->getGraph(poses, links, true, true);

    // KITTI 格式 (format=2)
    graph::exportPoses(path, 2, poses, links);

    std::cout << "[SLAM] Trajectory exported: " << path << "\n";
    return true;
}

int ScannerSLAM::loopClosureCount() const {
    if (!rtabmap_) return 0;
    return rtabmap_->getStatistics().loopClosureId() > 0 ? 1 : 0;
}

} // namespace stereo_scanner
