#pragma once
#include <string>
#include <opencv2/core.hpp>

namespace stereo_scanner {

struct ScannerConfig {
    // === 标定相关 ===
    std::string calibrationFile;    // cali.yml 路径
    cv::Size chessboardSize;        // 棋盘格内角点数 (cols, rows)
    double chessboardSquareSize;    // 棋盘格方格尺寸(米)
    int minCalibrationPairs;        // 最少标定图像对数

    // === 相机相关 ===
    int leftCameraId;               // 左相机设备ID (OpenCV VideoCapture)
    int rightCameraId;              // 右相机设备ID
    cv::Size captureResolution;     // 采集分辨率

    // === FFS 推理相关 ===
    std::string ffsEngineDir;       // TRT引擎目录 (含 .engine + onnx.yaml)
    int ffsMaxDisp;                 // 最大视差搜索范围
    int ffsValidIters;              // GRU迭代次数 (离线8, 实时4)
    float ffsScale;                 // 输入缩放因子 (1.0 或 0.5)

    // === RTAB-Map SLAM 相关 ===
    std::string rtabmapDatabase;    // 数据库路径 (空=内存模式)
    double rtabmapLoopThr;          // 回环检测阈值
    double rtabmapLinearUpdate;     // 最小位移更新(米)
    double rtabmapAngularUpdate;    // 最小角度更新(弧度)
    float maxDepth;                 // 最大有效深度(米)
    int odomStrategy;               // 里程计策略 (0=F2M默认)
    int featureDecimation;          // 特征降采样因子

    // === 输出相关 ===
    std::string outputDir;          // 输出目录
    float voxelSize;                // 点云体素滤波尺寸(米)
    bool exportPointCloud;          // 是否导出点云
    bool exportTrajectory;          // 是否导出轨迹

    // === 数据源 ===
    std::string imageDir;           // 离线模式: 图像目录
    std::string imageFormat;        // 图像文件格式 (png/jpg/bmp)

    // 默认值
    ScannerConfig()
        : calibrationFile("config/cali.yml")
        , chessboardSize(9, 6)
        , chessboardSquareSize(0.025)    // 25mm 方格
        , minCalibrationPairs(20)
        , leftCameraId(0)
        , rightCameraId(1)
        , captureResolution(640, 480)
        , ffsEngineDir("models/ffs_engine")
        , ffsMaxDisp(192)
        , ffsValidIters(8)
        , ffsScale(1.0f)
        , rtabmapDatabase("")
        , rtabmapLoopThr(0.11)
        , rtabmapLinearUpdate(0.1)
        , rtabmapAngularUpdate(0.1)
        , maxDepth(10.0f)
        , odomStrategy(0)
        , featureDecimation(4)
        , outputDir("output")
        , voxelSize(0.01f)
        , exportPointCloud(true)
        , exportTrajectory(true)
        , imageDir("")
        , imageFormat("png")
    {}

    // 从 YAML 文件加载
    static ScannerConfig fromFile(const std::string& path);

    // 保存到 YAML 文件
    void toFile(const std::string& path) const;
};

} // namespace stereo_scanner
