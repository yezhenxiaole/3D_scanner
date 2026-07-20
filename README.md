# NEXUS-3D: 双目三维扫描系统

基于 **RTAB-Map (SLAM)** 与 **Fast-FoundationStereo (立体匹配)** 的桌面级双目三维扫描仪。

## 🚀 快速开始

> **Windows x64 环境要求**：Visual Studio 2022、CMake、CUDA ≥ 12.0、TensorRT 10.x、Git、Python ≥ 3.9

```batch
:: 1. 克隆仓库
git clone https://github.com/yezhenxiaole/3D_scanner.git
cd 3D_scanner

:: 2. 一键构建（首次约 60-90 分钟）
build.bat --tensorrt "C:\Program Files\NVIDIA GPU Computing Toolkit\TensorRT-10.x.x"

:: 3. 导出深度学习模型
cd stereo_scanner
python scripts/export_ffs_engine.py --height 480 --width 640 --max_disp 192 --valid_iters 8

:: 4. 运行
cd build
stereo_offline.exe --help
```

> 📖 详细构建指南请参见 [BUILD.md](BUILD.md)。

## 项目结构

```
3D_scanner/
├── stereo_scanner/              ← 核心应用代码（标定 / SLAM / GUI）
│   ├── src/                     ← C++ 源码
│   │   ├── app/                 ← 主程序入口
│   │   ├── calibration/         ← 双目相机标定
│   │   ├── common/              ← 通用配置与类型
│   │   ├── stereo/              ← FFS 立体匹配推理
│   │   ├── slam/                ← RTAB-Map SLAM 封装
│   │   └── gui/                 ← Qt5 可视化界面
│   ├── scripts/                 ← Python 工具脚本
│   ├── config/                  ← 配置文件
│   ├── CMakeLists.txt           ← CMake 构建配置
│   └── build/                   ← 构建输出（首次运行后生成）
├── 3rdparty/                    ← 第三方库（源码）
│   ├── rtabmap/                 ← RTAB-Map SLAM 库
│   └── Fast-FoundationStereo-master/  ← 立体匹配模型
├── build.bat                    ← 一键构建脚本
├── BUILD.md                     ← 完整构建指南
├── CMakeLists.txt               ← 根 CMake 配置
└── .gitignore
```

## 功能

| 命令 | 功能说明 |
|---|---|
| `stereo_calibrate` | 双目相机标定（基于棋盘格） |
| `stereo_offline` | 对图像序列进行立体匹配 + SLAM 建图 |
| `stereo_online` | 实时相机扫描（开发中） |
| `stereo_gui` | Qt5 可视化前端（可选，需安装 Qt5） |

## 技术栈

- **深度学习立体匹配**：Fast-FoundationStereo (CUDA/TensorRT)
- **SLAM 后端**：RTAB-Map (Ceres/g2o)
- **GUI**：Qt5 / Web 界面 (ECharts)
- **依赖管理**：vcpkg
