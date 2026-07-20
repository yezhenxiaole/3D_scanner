#include "ffs_inference.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>

// FFS 原生 C++ 头文件 — 编译时需要 FFS 源码路径
#include "ffs_depth_single_tensorrt.hpp"
#include "ffs_depth_tensorrt.hpp"
#include "ffs_gwc_plugin.hpp"

namespace stereo_scanner {

// ============================================================
// 构造 / 析构
// ============================================================

FFSInference::FFSInference()
    : initialized_(false)
    , useSingleEngine_(true)
    , ffsEngine_(nullptr)
    , dLeftBuf_(nullptr)
    , dRightBuf_(nullptr)
    , dDepthBuf_(nullptr)
    , allocatedPixels_(0)
{}

FFSInference::~FFSInference() {
    // 释放 GPU 缓冲区
    if (dLeftBuf_)   cudaFree(dLeftBuf_);
    if (dRightBuf_)  cudaFree(dRightBuf_);
    if (dDepthBuf_)  cudaFree(dDepthBuf_);

    // 释放 FFS 推理引擎（根据路由类型选择正确的 delete）
    if (ffsEngine_) {
        if (useSingleEngine_) {
            delete static_cast<ffs_depth::FFSSingleEngineInference*>(ffsEngine_);
        } else {
            delete static_cast<ffs_depth::FFSDepthInference*>(ffsEngine_);
        }
    }
}

// ============================================================
// 初始化
// ============================================================

bool FFSInference::init(const std::string& engineDir) {
    config_.engineDir = engineDir;

    std::cout << "[FFS] Initializing engine from: " << engineDir << "\n";

    // 检测引擎路由: 单引擎 (Route A) vs 双引擎 (Route B)
    useSingleEngine_ = detectRoute(engineDir);

    if (useSingleEngine_) {
        std::cout << "[FFS] Using Route A (single engine + GWC Plugin)\n";
        try {
            auto* engine = new ffs_depth::FFSSingleEngineInference(engineDir);
            ffsEngine_ = engine;
        } catch (const std::exception& e) {
            std::cerr << "[FFS] Failed to init single engine: " << e.what() << "\n";
            return false;
        }
    } else {
        std::cout << "[FFS] Using Route B (dual engine + external GWC)\n";
        try {
            auto* engine = new ffs_depth::FFSDepthInference(engineDir);
            ffsEngine_ = engine;
        } catch (const std::exception& e) {
            std::cerr << "[FFS] Failed to init dual engine: " << e.what() << "\n";
            return false;
        }
    }

    initialized_ = true;
    std::cout << "[FFS] Engine initialized successfully.\n";

    return true;
}

bool FFSInference::detectRoute(const std::string& engineDir) {
    // Route A: fast_foundationstereo.engine 存在
    std::string singlePath = engineDir + "/fast_foundationstereo.engine";
    std::ifstream f(singlePath);
    if (f.good()) {
        f.close();
        return true;  // 单引擎
    }

    // Route B: feature_runner.engine + post_runner.engine 存在
    std::string featPath = engineDir + "/feature_runner.engine";
    std::string postPath = engineDir + "/post_runner.engine";
    std::ifstream f2(featPath);
    std::ifstream f3(postPath);
    if (f2.good() && f3.good()) {
        f2.close();
        f3.close();
        return false;  // 双引擎
    }

    std::cerr << "[FFS] No valid engine files found in: " << engineDir << "\n";
    std::cerr << "[FFS] Expected either:\n"
              << "  - fast_foundationstereo.engine (Route A)\n"
              << "  - feature_runner.engine + post_runner.engine (Route B)\n";
    return true;  // 默认尝试单引擎（会报错但不会崩溃）
}

// ============================================================
// GPU 缓冲区管理
// ============================================================

bool FFSInference::ensureGPUBuffers(int H, int W) {
    int64_t pixels = static_cast<int64_t>(H) * W;

    if (pixels <= allocatedPixels_ && dLeftBuf_ && dRightBuf_ && dDepthBuf_) {
        return true;  // 已分配且够用
    }

    // 释放旧缓冲区
    if (dLeftBuf_)   { cudaFree(dLeftBuf_);   dLeftBuf_ = nullptr; }
    if (dRightBuf_)  { cudaFree(dRightBuf_);  dRightBuf_ = nullptr; }
    if (dDepthBuf_)  { cudaFree(dDepthBuf_);  dDepthBuf_ = nullptr; }

    size_t imgBytes  = pixels * 3 * sizeof(uint8_t);
    size_t depthBytes = pixels * sizeof(float);

    cudaError_t err;
    err = cudaMalloc(&dLeftBuf_, imgBytes);
    if (err != cudaSuccess) {
        std::cerr << "[FFS] Failed to allocate left GPU buffer: "
                  << cudaGetErrorString(err) << "\n";
        return false;
    }
    err = cudaMalloc(&dRightBuf_, imgBytes);
    if (err != cudaSuccess) {
        std::cerr << "[FFS] Failed to allocate right GPU buffer: "
                  << cudaGetErrorString(err) << "\n";
        return false;
    }
    err = cudaMalloc(&dDepthBuf_, depthBytes);
    if (err != cudaSuccess) {
        std::cerr << "[FFS] Failed to allocate depth GPU buffer: "
                  << cudaGetErrorString(err) << "\n";
        return false;
    }

    allocatedPixels_ = pixels;
    return true;
}

// ============================================================
// 推理: 仅视差
// ============================================================

bool FFSInference::inferDisparity(const cv::Mat& leftRectBGR,
                                   const cv::Mat& rightRectBGR,
                                   cv::Mat& disparity) {
    if (!initialized_) {
        std::cerr << "[FFS] Not initialized.\n";
        return false;
    }

    if (leftRectBGR.empty() || rightRectBGR.empty()) {
        std::cerr << "[FFS] Empty input images.\n";
        return false;
    }

    int H = leftRectBGR.rows;
    int W = leftRectBGR.cols;

    // 确保 BGR uint8 连续内存
    cv::Mat leftCont, rightCont;
    if (!leftRectBGR.isContinuous())
        leftCont = leftRectBGR.clone();
    else
        leftCont = leftRectBGR;

    if (!rightRectBGR.isContinuous())
        rightCont = rightRectBGR.clone();
    else
        rightCont = rightRectBGR;

    // 分配 GPU 缓冲区
    if (!ensureGPUBuffers(H, W)) return false;

    // H2D: 图像上传到 GPU
    size_t imgBytes = H * W * 3 * sizeof(uint8_t);
    cudaMemcpy(dLeftBuf_, leftCont.data, imgBytes, cudaMemcpyHostToDevice);
    cudaMemcpy(dRightBuf_, rightCont.data, imgBytes, cudaMemcpyHostToDevice);

    // 执行推理（异步）
    if (useSingleEngine_) {
        auto* engine = static_cast<ffs_depth::FFSSingleEngineInference*>(ffsEngine_);
        engine->infer(
            static_cast<const uint8_t*>(dLeftBuf_),
            static_cast<const uint8_t*>(dRightBuf_),
            H, W,
            static_cast<float*>(dDepthBuf_));  // 临时用 depth buffer 存视差
    } else {
        auto* engine = static_cast<ffs_depth::FFSDepthInference*>(ffsEngine_);
        engine->infer(
            static_cast<const uint8_t*>(dLeftBuf_),
            static_cast<const uint8_t*>(dRightBuf_),
            H, W,
            static_cast<float*>(dDepthBuf_));  // 临时用 depth buffer 存视差
    }

    // D2H: 视差下载到 CPU
    size_t dispBytes = H * W * sizeof(float);
    disparity.create(H, W, CV_32FC1);
    cudaMemcpy(disparity.data, dDepthBuf_, dispBytes, cudaMemcpyDeviceToHost);

    return true;
}

// ============================================================
// 推理: 仅深度（最快路径）
// ============================================================

bool FFSInference::inferDepth(const cv::Mat& leftRectBGR,
                               const cv::Mat& rightRectBGR,
                               double fx, double baseline,
                               cv::Mat& depth) {
    if (!initialized_) {
        std::cerr << "[FFS] Not initialized.\n";
        return false;
    }

    if (leftRectBGR.empty() || rightRectBGR.empty()) {
        std::cerr << "[FFS] Empty input images.\n";
        return false;
    }

    int H = leftRectBGR.rows;
    int W = leftRectBGR.cols;

    // 确保 BGR uint8 连续内存
    cv::Mat leftCont, rightCont;
    if (!leftRectBGR.isContinuous())
        leftCont = leftRectBGR.clone();
    else
        leftCont = leftRectBGR;

    if (!rightRectBGR.isContinuous())
        rightCont = rightRectBGR.clone();
    else
        rightCont = rightRectBGR;

    // 分配 GPU 缓冲区
    if (!ensureGPUBuffers(H, W)) return false;

    // H2D: 图像上传到 GPU
    size_t imgBytes = H * W * 3 * sizeof(uint8_t);
    cudaMemcpy(dLeftBuf_, leftCont.data, imgBytes, cudaMemcpyHostToDevice);
    cudaMemcpy(dRightBuf_, rightCont.data, imgBytes, cudaMemcpyHostToDevice);

    // 执行 inferDepth（推理 + 深度转换 一体化 GPU 操作）
    if (useSingleEngine_) {
        auto* engine = static_cast<ffs_depth::FFSSingleEngineInference*>(ffsEngine_);
        engine->inferDepth(
            static_cast<const uint8_t*>(dLeftBuf_),
            static_cast<const uint8_t*>(dRightBuf_),
            H, W,
            static_cast<float>(fx), static_cast<float>(baseline),
            static_cast<float*>(dDepthBuf_));
        engine->sync();
    } else {
        auto* engine = static_cast<ffs_depth::FFSDepthInference*>(ffsEngine_);
        engine->inferDepth(
            static_cast<const uint8_t*>(dLeftBuf_),
            static_cast<const uint8_t*>(dRightBuf_),
            H, W,
            static_cast<float>(fx), static_cast<float>(baseline),
            static_cast<float*>(dDepthBuf_));
        engine->sync();
    }

    // D2H: 深度图下载到 CPU
    size_t depthBytes = H * W * sizeof(float);
    depth.create(H, W, CV_32FC1);
    cudaMemcpy(depth.data, dDepthBuf_, depthBytes, cudaMemcpyDeviceToHost);

    return true;
}

// ============================================================
// 推理: 视差 + 深度
// ============================================================

bool FFSInference::infer(const cv::Mat& leftRectBGR, const cv::Mat& rightRectBGR,
                          const CalibrationData& cali,
                          cv::Mat& disparity, cv::Mat& depth) {
    if (!initialized_) {
        std::cerr << "[FFS] Not initialized.\n";
        return false;
    }

    // 使用 inferDepth 一步到位获取深度
    if (!inferDepth(leftRectBGR, rightRectBGR, cali.fx, cali.baseline, depth))
        return false;

    // 如果同时需要视差，从深度反算（或单独推理视差）
    // 注意: depth = fx * baseline / disp => disp = fx * baseline / depth
    // 这比再跑一次推理高效得多
    disparity.create(depth.size(), CV_32FC1);
    int H = depth.rows, W = depth.cols;
    float fb = static_cast<float>(cali.fx * cali.baseline);
    for (int y = 0; y < H; ++y) {
        const float* depthRow = depth.ptr<float>(y);
        float* dispRow = disparity.ptr<float>(y);
        for (int x = 0; x < W; ++x) {
            float d = depthRow[x];
            if (d > 0 && std::isfinite(d)) {
                dispRow[x] = fb / d;
            } else {
                dispRow[x] = 0.0f;
            }
        }
    }

    return true;
}

// ============================================================
// CPU 回退: 视差 → 深度
// ============================================================

cv::Mat FFSInference::disparityToDepthCPU(const cv::Mat& disparity,
                                           double fx, double baseline) {
    cv::Mat depth(disparity.size(), CV_32FC1);

    int H = disparity.rows;
    int W = disparity.cols;
    float fb = static_cast<float>(fx * baseline);

    for (int y = 0; y < H; ++y) {
        const float* dispRow = disparity.ptr<float>(y);
        float* depthRow = depth.ptr<float>(y);
        for (int x = 0; x < W; ++x) {
            float d = dispRow[x];
            if (d <= 0 || !std::isfinite(d)) {
                depthRow[x] = 0.0f;  // 无效
            } else if (static_cast<float>(x) - d < 0) {
                depthRow[x] = 0.0f;  // 右图对应点超出边界
            } else {
                depthRow[x] = fb / d;
            }
        }
    }

    return depth;
}

// ============================================================
// 模型信息
// ============================================================

int FFSInference::modelHeight() const {
    if (!initialized_) return 0;
    if (useSingleEngine_) {
        return static_cast<ffs_depth::FFSSingleEngineInference*>(ffsEngine_)->modelHeight();
    } else {
        return static_cast<ffs_depth::FFSDepthInference*>(ffsEngine_)->modelHeight();
    }
}

int FFSInference::modelWidth() const {
    if (!initialized_) return 0;
    if (useSingleEngine_) {
        return static_cast<ffs_depth::FFSSingleEngineInference*>(ffsEngine_)->modelWidth();
    } else {
        return static_cast<ffs_depth::FFSDepthInference*>(ffsEngine_)->modelWidth();
    }
}

int FFSInference::maxDisp() const {
    if (!initialized_) return 0;
    if (useSingleEngine_) {
        return static_cast<ffs_depth::FFSSingleEngineInference*>(ffsEngine_)->maxDisp();
    } else {
        return static_cast<ffs_depth::FFSDepthInference*>(ffsEngine_)->maxDisp();
    }
}

} // namespace stereo_scanner
