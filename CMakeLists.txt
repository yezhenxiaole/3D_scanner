cmake_minimum_required(VERSION 3.20)
project(NEXUS-3D VERSION 1.0.0 LANGUAGES CXX CUDA)

# ============================================================
# NEXUS-3D 根 CMakeLists.txt
#
# 直接用 VS Code 打开此项目文件夹，CMake 插件即可识别。
# cmake 配置:
#   cmake -B build -DTENSORRT_ROOT="C:\TensorRT-xxx" -DRTABMAP_DIR="..."
# ============================================================

# 实际构建逻辑在 stereo_scanner 子目录中
add_subdirectory(stereo_scanner)
