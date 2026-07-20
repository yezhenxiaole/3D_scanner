# NEXUS-3D 构建指南

## 🔧 前置环境要求

| 工具 | 版本要求 | 获取方式 |
|---|---|---|
| **Windows 11/10** | x64 | — |
| **Visual Studio 2022** | 含 "C++ 桌面开发" 工作负载 | [Visual Studio](https://visualstudio.microsoft.com/) |
| **CMake** | ≥ 3.28 | `winget install CMake` 或 [cmake.org](https://cmake.org/download/) |
| **CUDA Toolkit** | ≥ 12.0 | [NVIDIA CUDA](https://developer.nvidia.com/cuda-downloads) |
| **TensorRT** | 10.x | [NVIDIA TensorRT](https://developer.nvidia.com/tensorrt)（需注册账号下载） |
| **Git** | — | `winget install Git.Git` 或 [git-scm.com](https://git-scm.com/) |
| **Python** | ≥ 3.9 | [python.org](https://www.python.org/)（导出模型用） |

> **重要**：
> - 安装 CMake 时请勾选 **"Add CMake to the system PATH"**
> - 确保 `cmake.exe` 和 `git.exe` 都在 PATH 中（打开 CMD 输入 `cmake --version` 验证）

---

## 🚀 快速构建（推荐）

### 第 0 步：打开正确的命令行

> 按 **Win 键**，搜索 **"x64 Native Tools Command Prompt for VS 2022"**，右键以管理员身份运行。

### 第 1 步：克隆并进入项目

```batch
git clone https://github.com/yezhenxiaole/3D_scanner.git
cd 3D_scanner
```

### 第 2 步：一键构建

> 把下面 `C:\TensorRT-10.x.x` 换成你实际的 TensorRT 路径。

```batch
build.bat --tensorrt "C:\TensorRT-10.x.x"
```

脚本会自动：
1. ✅ 检测 CMake、VS 2022、CUDA
2. ✅ **首次自动下载 vcpkg + 安装依赖**（OpenCV、PCL、Eigen3 等，约 30-60 分钟，仅一次）
3. ✅ 编译 RTAB-Map（约 10-20 分钟，仅一次）
4. ✅ 编译 stereo_scanner（约 5-10 分钟）
5. ✅ 复制所有运行时 DLL

**首次构建总耗时：约 60-90 分钟**（主要是编译第三方依赖）。
**后续构建仅需：约 5-10 分钟**。

### 第 3 步：导出 FFS 推理引擎

```batch
cd stereo_scanner
python scripts/export_ffs_engine.py --height 480 --width 640 --max_disp 192 --valid_iters 8
```

### 第 4 步：验证

```batch
..\stereo_scanner\build\stereo_offline.exe --help
```

看到帮助信息即表示构建成功 ✅

---

## 📋 手动分步构建（不用 build.bat）

如果你想自己一步步来，以下是完整的手动流程。

### 1. 初始化 vcpkg 并安装依赖

```batch
cd 3rdparty\rtabmap

:: 克隆 vcpkg（仅首次）
git clone https://github.com/microsoft/vcpkg.git
call vcpkg\bootstrap-vcpkg.bat

:: 安装所有依赖（仅首次，30-60 分钟）
vcpkg install --triplet x64-windows-release

cd ..\..
```

这行命令会自动下载并编译 **OpenCV（含 contrib）、PCL、Eigen3、Ceres、g2o、SQLite3** 等所有依赖。**只需执行一次**。

### 2. 构建 RTAB-Map

```batch
cmake -S 3rdparty/rtabmap -B 3rdparty/rtabmap/build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_AS_BUNDLE=OFF ^
    -DWITH_QT=OFF -DWITH_GTSAM=OFF -DWITH_OPENGV=OFF ^
    -DWITH_CERES=ON -DWITH_TORO=ON -DWITH_VERTIGO=ON ^
    -DWITH_PYTHON=OFF ^
    -DVCPKG_MANIFEST_INSTALL=OFF ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
    -DCMAKE_TOOLCHAIN_FILE="3rdparty/rtabmap/vcpkg/scripts/buildsystems/vcpkg.cmake"

cmake --build 3rdparty/rtabmap/build --config Release -- -j %NUMBER_OF_PROCESSORS%
cmake --install 3rdparty/rtabmap/build --config Release --prefix 3rdparty/rtabmap/build/install
```

### 3. 构建 stereo_scanner

```batch
cmake -S stereo_scanner -B stereo_scanner/build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DRTABMAP_DIR="3rdparty/rtabmap/build/install" ^
    -DTENSORRT_ROOT="C:\TensorRT-10.x.x" ^
    -DVCPKG_MANIFEST_INSTALL=OFF ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
    -DCMAKE_TOOLCHAIN_FILE="3rdparty/rtabmap/vcpkg/scripts/buildsystems/vcpkg.cmake"

cmake --build stereo_scanner/build --config Release -- -j %NUMBER_OF_PROCESSORS%
```

> ⚠️ 把 `-DTENSORRT_ROOT=` 换成你实际的 TensorRT 安装路径。

### 4. 复制运行时 DLL

```batch
cd stereo_scanner\build

:: RTAB-Map DLL
copy ..\..\3rdparty\rtabmap\build\install\bin\rtabmap_core.dll .
copy ..\..\3rdparty\rtabmap\build\install\bin\rtabmap_utilite.dll .

:: vcpkg 依赖 DLL（OpenCV、PCL 等）
copy ..\..\3rdparty\rtabmap\vcpkg_installed\x64-windows-release\bin\*.dll .

:: CUDA DLL
copy "%CUDA_PATH%\bin\cudart64_*.dll" .
copy "%CUDA_PATH%\bin\cublas64_*.dll" .

:: TensorRT DLL
copy "%TENSORRT_ROOT%\lib\nvinfer*.dll" .
copy "%TENSORRT_ROOT%\lib\nvonnxparser*.dll" .
```

---

## 📦 项目结构（构建后）

```
3D_scanner/
├── stereo_scanner/
│   ├── src/                     ← C++ 源码
│   ├── scripts/                 ← Python 工具
│   ├── CMakeLists.txt           ← CMake 配置
│   └── build/                   ← 构建输出（exe 在这里）
├── 3rdparty/
│   ├── rtabmap/
│   │   ├── vcpkg/               ← vcpkg 包管理器（首次自动克隆）
│   │   ├── vcpkg_installed/     ← 所有依赖（OpenCV、PCL...）
│   │   └── build/install/       ← RTAB-Map 编译产物
│   └── Fast-FoundationStereo-master/
├── build.bat                    ← 一键构建脚本
├── BUILD.md                     ← 本文件
└── README.md
```

---

## 🔄 重新构建

```batch
:: 完全清理后重新构建
rd /s /q 3rdparty\rtabmap\build
rd /s /q 3rdparty\rtabmap\vcpkg_installed
rd /s /q stereo_scanner\build
build.bat --tensorrt "C:\TensorRT-10.x.x"
```

## ❓ 常见问题

**Q: build.bat 报错 "cmake not found in PATH"**
> A: 安装 CMake 时勾选 "Add to PATH"，或手动添加。

**Q: 找不到 TensorRT**
> A: 在 build.bat 后面加上 `--tensorrt "你的TensorRT路径"`，例如：
> ```batch
> build.bat --tensorrt "C:\Program Files\NVIDIA GPU Computing Toolkit\TensorRT-10.x.x"
> ```

**Q: 编译速度太慢**
> A: 首次安装 vcpkg 依赖是正常的（30-60 分钟）。之后重新构建只需几分钟。
