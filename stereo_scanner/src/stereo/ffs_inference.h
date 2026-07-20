#pragma once
#include "common/types.h"
#include <memory>
#include <string>

// 前置声明: FFS-TRT 推理类（来自 Fast-FoundationStereo C++ 代码）
// 实际类型在 ffs_depth 命名空间中，这里仅用 void* 降低耦合
namespace stereo_scanner {

class FFSInference {
public:
    struct Config {
        std::string engineDir;  // TRT引擎目录
        int maxDisp = 192;
        int validIters = 8;
    };

    FFSInference();
    ~FFSInference();

    // 禁止拷贝（持有 GPU 资源）
    FFSInference(const FFSInference&) = delete;
    FFSInference& operator=(const FFSInference&) = delete;

    // 初始化 TRT 推理引擎
    // engineDir: 包含 .engine + onnx.yaml 的目录
    bool init(const std::string& engineDir);

    // 推理: 校正后的左右图 → 视差图 + 深度图
    // 输入: 校正后的 BGR uint8 cv::Mat (连续内存)
    // 输出: disparity CV_32FC1 (像素), depth CV_32FC1 (米)
    // 内部使用 FFS inferDepth() 一体化 GPU 接口，避免 CPU 深度转换
    bool infer(const cv::Mat& leftRectBGR, const cv::Mat& rightRectBGR,
               const CalibrationData& cali,
               cv::Mat& disparity, cv::Mat& depth);

    // 仅推理视差（不含深度转换）
    bool inferDisparity(const cv::Mat& leftRectBGR, const cv::Mat& rightRectBGR,
                        cv::Mat& disparity);

    // 仅推理深度（最快速路径，视差不回传 CPU）
    bool inferDepth(const cv::Mat& leftRectBGR, const cv::Mat& rightRectBGR,
                    double fx, double baseline,
                    cv::Mat& depth);

    // 视差 → 深度（CPU 回退版本，用于无 GPU 或调试）
    static cv::Mat disparityToDepthCPU(const cv::Mat& disparity,
                                       double fx, double baseline);

    bool isInitialized() const { return initialized_; }

    // 获取模型信息
    int modelHeight() const;
    int modelWidth() const;
    int maxDisp() const;

private:
    bool initialized_;
    bool useSingleEngine_;  // true=Route A, false=Route B
    Config config_;

    // FFS 推理引擎（使用 void* 避免头文件耦合，实际类型在 cpp 中 cast）
    void* ffsEngine_;       // ffs_depth::FFSSingleEngineInference 或 ffs_depth::FFSDepthInference

    // GPU 缓冲区: 仅用于 H2D 图像上传和 D2H 结果下载
    // FFS 内部有自己的 GPU buffer 管理，我们只管输入/输出
    void* dLeftBuf_;        // uint8_t* GPU buffer for left image
    void* dRightBuf_;       // uint8_t* GPU buffer for right image
    void* dDepthBuf_;       // float* GPU buffer for depth output
    int64_t allocatedPixels_;  // 已分配缓冲区对应的像素数（用于判断是否需要重分配）

    // 内部实现
    bool detectRoute(const std::string& engineDir);
    bool ensureGPUBuffers(int H, int W);
};

} // namespace stereo_scanner
