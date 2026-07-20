# NEXUS-3D: 双目三维扫描系统

基于 **RTAB-Map (SLAM)** 与 **Fast-FoundationStereo (立体匹配)** 的桌面级双目三维扫描仪。

## 快速开始

> **Windows x64 环境要求**：Visual Studio 2022、CMake、CUDA ≥ 12.0、TensorRT 10.x

```batch
:: 1. 克隆仓库
git clone https://github.com/yezhenxiaole/3D_scanner.git
cd 3D_scanner

:: 2. 一键构建（自动检测 VS/cmake，仅需提供 TensorRT 路径）
build.bat --tensorrt "C:\Program Files\NVIDIA GPU Computing Toolkit\TensorRT-10.x.x"
```

详细构建指南见 [BUILD.md](BUILD.md)。

## 项目结构

```
3D_scanner/
├── stereo_scanner/              ← 核心应用代码
│   ├── src/                     ← C++ 源码
│   │   ├── app/                 ← 主程序入口
│   │   ├── calibration/         ← 双目相机标定
│   │   ├── common/              ← 通用配置与类型
│   │   ├── stereo/              ← FFS 立体匹配推理
│   │   └── slam/                ← RTAB-Map SLAM 封装
│   ├── ui/                      ← 前端界面 (HTML/JS)
│   ├── scripts/                 ← Python 工具脚本
│   ├── CMakeLists.txt           ← CMake 构建配置
│   └── build/                   ← 构建输出 (git ignored)
├── 3rdparty/                    ← 第三方库 (源码)
│   ├── rtabmap/                 ← RTAB-Map SLAM 库
│   └── Fast-FoundationStereo-master/  ← 立体匹配模型
├── build.bat                    ← 一键构建脚本
├── BUILD.md                     ← 完整构建指南
└── .gitignore
```

## 功能

- **双目相机标定**：`stereo_calibrate` — 基于棋盘格的在线标定
- **离线深度估计**：`stereo_offline` — 对图像序列进行立体匹配 + SLAM 建图
- **实时扫描**：（开发中）

## 技术栈

- **深度学习立体匹配**：Fast-FoundationStereo (CUDA/TensorRT)
- **SLAM 后端**：RTAB-Map (Ceres/g2o)
- **GUI**：Web 界面 (ECharts 可视化)
- **依赖管理**：vcpkg
