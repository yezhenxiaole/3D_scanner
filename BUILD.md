# NEXUS-3D 构建指南

## 前置环境要求

| 工具 | 版本要求 | 获取方式 |
|---|---|---|
| **Windows 11/10** | x64 | — |
| **Visual Studio 2022** | 含 "C++ 桌面开发" 工作负载 | [Visual Studio](https://visualstudio.microsoft.com/) |
| **CMake** | ≥ 3.28 | `winget install CMake` 或 [cmake.org](https://cmake.org/download/) |
| **CUDA Toolkit** | ≥ 12.0 | [NVIDIA CUDA](https://developer.nvidia.com/cuda-downloads) |
| **TensorRT** | 10.x | [NVIDIA TensorRT](https://developer.nvidia.com/tensorrt) (需注册账号下载) |
| **Git** | — | `winget install Git.Git` 或 [git-scm.com](https://git-scm.com/) |

> **注意**：请将 cmake.exe 加入系统 PATH（安装时勾选 "Add to PATH"）。

---

## 快速构建（推荐）

打开 **"x64 Native Tools Command Prompt for VS 2022"**（开始菜单里搜索），然后：

```batch
:: 1. 进入项目
cd C:\你的路径\3D_scanner

:: 2. 安装 vcpkg 依赖（仅首次，耗时 30-60 分钟）
call 3rdparty\rtabmap\vcpkg\vcpkg install --triplet x64-windows-release

:: 3. 构建 RTAB-Map（仅首次，耗时 10-20 分钟）
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

:: 4. 构建 stereo_scanner
cmake -S stereo_scanner -B stereo_scanner/build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DRTABMAP_DIR="3rdparty/rtabmap/build/install" ^
    -DTENSORRT_ROOT="C:\Program Files\NVIDIA GPU Computing Toolkit\TensorRT-10.x.x" ^
    -DVCPKG_MANIFEST_INSTALL=OFF ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
    -DCMAKE_TOOLCHAIN_FILE="3rdparty/rtabmap/vcpkg/scripts/buildsystems/vcpkg.cmake"

cmake --build stereo_scanner/build --config Release -- -j %NUMBER_OF_PROCESSORS%

:: 5. 复制运行时 DLL（确保 exe 能运行）
copy 3rdparty\rtabmap\build\install\bin\*.dll stereo_scanner\build\
copy vcpkg_installed\x64-windows-release\bin\*.dll stereo_scanner\build\
```

> ⚠️ **注意**：第 4 步中 `-DTENSORRT_ROOT=` 要换成你实际的 TensorRT 安装路径。

---

## 手动分步说明

### 第一步：安装 vcpkg 依赖

```batch
cd 3rdparty\rtabmap
vcpkg install --triplet x64-windows-release
```

这行命令会自动下载并编译 OpenCV（含 contrib）、PCL、Eigen3、Ceres、g2o、SQLite3 等所有依赖。**只需执行一次**。

### 第二步：构建 RTAB-Map

```batch
cd 项目根目录
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

编译成功后，`3rdparty/rtabmap/build/install/` 下会生成：
- `bin/rtabmap_core.dll`
- `bin/rtabmap_utilite.dll`
- `include/rtabmap/`（头文件）
- `lib/rtabmap_core.lib`

### 第三步：配置 TensorRT

TensorRT 需要从 NVIDIA 官网下载并解压到本地。关键文件结构：

```
TensorRT-10.x.x/
├── lib/
│   ├── nvinfer.lib
│   ├── nvinfer.dll
│   └── nvonnxparser.lib
├── include/
│   └── NvInfer.h
└── bin/
    └── trtexec.exe
```

### 第四步：构建 stereo_scanner

```batch
cmake -S stereo_scanner -B stereo_scanner/build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DRTABMAP_DIR="3rdparty/rtabmap/build/install" ^
    -DTENSORRT_ROOT="C:\Program Files\NVIDIA GPU Computing Toolkit\TensorRT-10.x.x" ^
    -DVCPKG_MANIFEST_INSTALL=OFF ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
    -DCMAKE_TOOLCHAIN_FILE="3rdparty/rtabmap/vcpkg/scripts/buildsystems/vcpkg.cmake"

cmake --build stereo_scanner/build --config Release -- -j %NUMBER_OF_PROCESSORS%
```

### 第五步：复制运行时 DLL

程序运行需要以下 DLL 在同一目录：

```batch
cd stereo_scanner\build

:: RTAB-Map DLL
copy ..\..\3rdparty\rtabmap\build\install\bin\rtabmap_core.dll .
copy ..\..\3rdparty\rtabmap\build\install\bin\rtabmap_utilite.dll .

:: vcpkg 依赖 DLL（OpenCV、PCL 等）
copy ..\..\vcpkg_installed\x64-windows-release\bin\*.dll .

:: CUDA DLL（cudart, cublas 等）
copy "%CUDA_PATH%\bin\cudart64_*.dll" .
copy "%CUDA_PATH%\bin\cublas64_*.dll" .
copy "%CUDA_PATH%\bin\cublasLt64_*.dll" .

:: TensorRT DLL
copy "%TENSORRT_ROOT%\lib\nvinfer*.dll" .
copy "%TENSORRT_ROOT%\lib\nvonnxparser*.dll" .
```

---

## 导出 FFS 推理引擎

构建完成后，还需要导出深度学习模型：

```bash
cd stereo_scanner
python scripts/export_ffs_engine.py --height 480 --width 640 --max_disp 192 --valid_iters 8
```

这会生成 `stereo_scanner/models/ffs_engine/fast_foundationstereo.engine`。

---

## 验证安装

```batch
stereo_scanner\build\stereo_offline.exe --help
```

看到帮助信息即表示构建成功！
