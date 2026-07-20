#include "config.h"
#include <opencv2/core.hpp>
#include <fstream>
#include <iostream>

namespace stereo_scanner {

ScannerConfig ScannerConfig::fromFile(const std::string& path) {
    ScannerConfig cfg;
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        std::cerr << "[Config] Cannot open config file: " << path
                  << ", using defaults.\n";
        return cfg;
    }

    // 标定
    cv::read(fs["calibration_file"], cfg.calibrationFile, cfg.calibrationFile);
    int cbCols = fs["chessboard_cols"];
    int cbRows = fs["chessboard_rows"];
    if (cbCols > 0 && cbRows > 0)
        cfg.chessboardSize = cv::Size(cbCols, cbRows);
    cv::read(fs["chessboard_square_size"], cfg.chessboardSquareSize, cfg.chessboardSquareSize);
    cv::read(fs["min_calibration_pairs"], cfg.minCalibrationPairs, cfg.minCalibrationPairs);

    // 相机
    cv::read(fs["left_camera_id"], cfg.leftCameraId, cfg.leftCameraId);
    cv::read(fs["right_camera_id"], cfg.rightCameraId, cfg.rightCameraId);
    int capW = fs["capture_width"];
    int capH = fs["capture_height"];
    if (capW > 0 && capH > 0)
        cfg.captureResolution = cv::Size(capW, capH);

    // FFS
    cv::read(fs["ffs_engine_dir"], cfg.ffsEngineDir, cfg.ffsEngineDir);
    cv::read(fs["ffs_max_disp"], cfg.ffsMaxDisp, cfg.ffsMaxDisp);
    cv::read(fs["ffs_valid_iters"], cfg.ffsValidIters, cfg.ffsValidIters);
    cv::read(fs["ffs_scale"], cfg.ffsScale, cfg.ffsScale);

    // RTAB-Map
    cv::read(fs["rtabmap_database"], cfg.rtabmapDatabase, cfg.rtabmapDatabase);
    cv::read(fs["rtabmap_loop_thr"], cfg.rtabmapLoopThr, cfg.rtabmapLoopThr);
    cv::read(fs["rtabmap_linear_update"], cfg.rtabmapLinearUpdate, cfg.rtabmapLinearUpdate);
    cv::read(fs["rtabmap_angular_update"], cfg.rtabmapAngularUpdate, cfg.rtabmapAngularUpdate);
    cv::read(fs["max_depth"], cfg.maxDepth, cfg.maxDepth);
    cv::read(fs["odom_strategy"], cfg.odomStrategy, cfg.odomStrategy);
    cv::read(fs["feature_decimation"], cfg.featureDecimation, cfg.featureDecimation);

    // 输出
    cv::read(fs["output_dir"], cfg.outputDir, cfg.outputDir);
    cv::read(fs["voxel_size"], cfg.voxelSize, cfg.voxelSize);
    cv::read(fs["export_point_cloud"], cfg.exportPointCloud, cfg.exportPointCloud);
    cv::read(fs["export_trajectory"], cfg.exportTrajectory, cfg.exportTrajectory);

    // 数据源
    cv::read(fs["image_dir"], cfg.imageDir, cfg.imageDir);
    cv::read(fs["image_format"], cfg.imageFormat, cfg.imageFormat);

    fs.release();
    std::cout << "[Config] Loaded from: " << path << "\n";
    return cfg;
}

void ScannerConfig::toFile(const std::string& path) const {
    cv::FileStorage fs(path, cv::FileStorage::WRITE);

    // 标定
    fs << "calibration_file" << calibrationFile;
    fs << "chessboard_cols" << chessboardSize.width;
    fs << "chessboard_rows" << chessboardSize.height;
    fs << "chessboard_square_size" << chessboardSquareSize;
    fs << "min_calibration_pairs" << minCalibrationPairs;

    // 相机
    fs << "left_camera_id" << leftCameraId;
    fs << "right_camera_id" << rightCameraId;
    fs << "capture_width" << captureResolution.width;
    fs << "capture_height" << captureResolution.height;

    // FFS
    fs << "ffs_engine_dir" << ffsEngineDir;
    fs << "ffs_max_disp" << ffsMaxDisp;
    fs << "ffs_valid_iters" << ffsValidIters;
    fs << "ffs_scale" << ffsScale;

    // RTAB-Map
    fs << "rtabmap_database" << rtabmapDatabase;
    fs << "rtabmap_loop_thr" << rtabmapLoopThr;
    fs << "rtabmap_linear_update" << rtabmapLinearUpdate;
    fs << "rtabmap_angular_update" << rtabmapAngularUpdate;
    fs << "max_depth" << maxDepth;
    fs << "odom_strategy" << odomStrategy;
    fs << "feature_decimation" << featureDecimation;

    // 输出
    fs << "output_dir" << outputDir;
    fs << "voxel_size" << voxelSize;
    fs << "export_point_cloud" << exportPointCloud;
    fs << "export_trajectory" << exportTrajectory;

    // 数据源
    fs << "image_dir" << imageDir;
    fs << "image_format" << imageFormat;

    fs.release();
    std::cout << "[Config] Saved to: " << path << "\n";
}

} // namespace stereo_scanner
