cmake_minimum_required(VERSION 3.20)
project(stereo_scanner VERSION 1.0.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 构建类型
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Build type: Debug, Release, RelWithDebInfo" FORCE)
endif()

# ---------------------------------------------------------------------------
# CUDA 架构（适配 RTX 5060 = sm_120 Blackwell）
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
  set(CMAKE_CUDA_ARCHITECTURES "80;86;89;90;120" CACHE STRING "" FORCE)
endif()

add_compile_options(
  $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
  $<$<COMPILE_LANGUAGE:CUDA>:--diag-suppress=20015>
  $<$<COMPILE_LANGUAGE:CUDA>:--diag-suppress=20013>
  $<$<COMPILE_LANGUAGE:CUDA>:--diag-suppress=20011>
  $<$<COMPILE_LANGUAGE:CUDA>:--diag-suppress=20091>
)

# MSVC 特定设置
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
  add_compile_definitions(_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING)
endif()

# ---------------------------------------------------------------------------
# Qt6 (for stereo_gui)
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets OpenGL OpenGLWidgets)
if(Qt6_FOUND)
  message(STATUS "Qt6 version: ${Qt6Core_VERSION} found at ${Qt6_DIR}")
  set(QT6_FOUND 1)
else()
  message(WARNING "Qt6 not found - stereo_gui target will be skipped.")
  set(QT6_FOUND 0)
endif()

# ---------------------------------------------------------------------------
# 依赖查找
# ---------------------------------------------------------------------------
find_package(CUDAToolkit REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d highgui videoio)
find_package(Eigen3 REQUIRED)

# ---- TensorRT ----
# 通过 -DTENSORRT_ROOT="C:/TensorRT-10.x.x" 在 cmake 命令行指定
set(TENSORRT_ROOT "" CACHE PATH "TensorRT installation root (e.g. C:/TensorRT-10.x.x)")
set(TENSORRT_HINT_DIRS
  ${TENSORRT_ROOT}
  $ENV{CONDA_PREFIX}
  ${CUDAToolkit_TARGET_DIR}
)

find_library(NVINFER_LIB
  NAMES nvinfer nvinfer_10
  HINTS ${TENSORRT_HINT_DIRS}
  PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
)
if(NOT NVINFER_LIB)
  message(FATAL_ERROR "TensorRT nvinfer not found. Set -DTENSORRT_ROOT=...")
endif()
message(STATUS "TensorRT nvinfer: ${NVINFER_LIB}")

find_library(NVONNXPARSER_LIB
  NAMES nvonnxparser nvonnxparser_10
  HINTS ${TENSORRT_HINT_DIRS}
  PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
)
if(NVONNXPARSER_LIB)
  message(STATUS "TensorRT nvonnxparser: ${NVONNXPARSER_LIB}")
endif()

find_path(NVINFER_INCLUDE_DIR NvInfer.h
  HINTS ${TENSORRT_HINT_DIRS}
  PATH_SUFFIXES include include/x86_64-linux-gnu
)
if(NOT NVINFER_INCLUDE_DIR)
  message(FATAL_ERROR "TensorRT headers not found. Set -DTENSORRT_ROOT=...")
endif()
message(STATUS "TensorRT include: ${NVINFER_INCLUDE_DIR}")

# ---- RTAB-Map ----
# 两种查找方式:
#   1. 通过 RTABMAP_DIR 手动指定 RTAB-Map 构建目录 (含 install 子目录)
#   2. 通过 find_package(Rtabmap) 系统安装路径
set(RTABMAP_DIR "" CACHE PATH "RTAB-Map build or install directory")

if(RTABMAP_DIR)
  # 手动路径模式: 指向 RTAB-Map 的 install 目录或 build 目录
  # 尝试查找 rtabmap_core 库
  set(_RTABMAP_SEARCH_DIRS
    ${RTABMAP_DIR}
    ${RTABMAP_DIR}/install
    ${RTABMAP_DIR}/bin
    ${RTABMAP_DIR}/lib
  )
  set(RTABMAP_INCLUDE_DIRS
    ${RTABMAP_DIR}/include/rtabmap-0.23
    ${RTABMAP_DIR}/install/include/rtabmap-0.23
  )
  find_library(RTABMAP_CORE_LIB
    NAMES rtabmap_core
    HINTS ${_RTABMAP_SEARCH_DIRS}
    PATH_SUFFIXES lib bin Release Debug
  )
  find_library(RTABMAP_GUI_LIB
    NAMES rtabmap_gui
    HINTS ${_RTABMAP_SEARCH_DIRS}
    PATH_SUFFIXES lib bin Release Debug
  )
  if(RTABMAP_CORE_LIB)
    set(RTABMAP_FOUND 1)
    message(STATUS "RTAB-Map core: ${RTABMAP_CORE_LIB}")
    if(RTABMAP_GUI_LIB)
      message(STATUS "RTAB-Map gui: ${RTABMAP_GUI_LIB}")
    endif()
  else()
    message(WARNING "RTAB-Map core lib not found in: ${RTABMAP_DIR}")
    set(RTABMAP_FOUND 0)
  endif()
else()
  find_package(Rtabmap QUIET)
  if(Rtabmap_FOUND)
    set(RTABMAP_INCLUDE_DIRS ${Rtabmap_INCLUDE_DIRS})
    set(RTABMAP_CORE_LIB ${Rtabmap_LIBRARIES})
    set(RTABMAP_FOUND 1)
  else()
    message(WARNING "RTAB-Map not found. Set -DRTABMAP_DIR=<rtabmap_build_dir>")
    set(RTABMAP_FOUND 0)
  endif()
endif()

# ---------------------------------------------------------------------------
# Fast-FoundationStereo C++ TRT 代码（直接编译源码到项目中）
# ---------------------------------------------------------------------------
set(FFS_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/Fast-FoundationStereo-master/cpp"
    CACHE PATH "Fast-FoundationStereo C++ source directory")

if(EXISTS "${FFS_CPP_DIR}/CMakeLists.txt")
  set(FFS_FOUND 1)
  message(STATUS "FFS C++ dir: ${FFS_CPP_DIR}")
else()
  message(WARNING "FFS C++ directory not found at: ${FFS_CPP_DIR}")
  set(FFS_FOUND 0)
endif()

# ---------------------------------------------------------------------------
# 项目内部库: scanner_core (标定 + 配置 + 类型)
# ---------------------------------------------------------------------------
add_library(scanner_core STATIC
  src/common/config.cpp
  src/calibration/stereo_calibrator.cpp
)
target_include_directories(scanner_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(scanner_core PUBLIC
  ${OpenCV_LIBS}
  Eigen3::Eigen
)

# ---------------------------------------------------------------------------
# 项目内部库: scanner_stereo (FFS 推理封装)
# ---------------------------------------------------------------------------
if(FFS_FOUND)
  add_library(scanner_stereo STATIC
    src/stereo/ffs_inference.cpp
    # FFS 原生源码直接编译进来
    ${FFS_CPP_DIR}/src/ffs_depth_single_tensorrt.cpp
    ${FFS_CPP_DIR}/src/ffs_depth_tensorrt.cpp
    ${FFS_CPP_DIR}/src/gwc_volume_plugin.cpp
    ${FFS_CPP_DIR}/src/depth_kernels.cu
  )
  target_include_directories(scanner_stereo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFS_CPP_DIR}/include
    ${NVINFER_INCLUDE_DIR}
  )
  target_link_libraries(scanner_stereo PUBLIC
    scanner_core
    CUDA::cudart
    ${NVINFER_LIB}
  )
  if(NVONNXPARSER_LIB)
    target_link_libraries(scanner_stereo PUBLIC ${NVONNXPARSER_LIB})
  endif()
  # CUDA 分离编译（FFS GWC Plugin 需要）
  set_target_properties(scanner_stereo PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
  )
endif()

# ---------------------------------------------------------------------------
# 项目内部库: scanner_slam (RTAB-Map SLAM 封装)
# ---------------------------------------------------------------------------
if(RTABMAP_FOUND)
  # PCL 组件: 与 RTAB-Map corelib (无Qt模式) 一致
  # RTAB-Map 在 WITH_QT=OFF 时需要: common io kdtree search surface filters registration sample_consensus segmentation
  find_package(PCL REQUIRED COMPONENTS common io kdtree search surface filters registration sample_consensus segmentation)

  add_library(scanner_slam STATIC
    src/slam/scanner_slam.cpp
  )
  target_include_directories(scanner_slam PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${RTABMAP_INCLUDE_DIRS}
    ${PCL_INCLUDE_DIRS}
  )
  # PCL 禁用预编译头
  target_compile_definitions(scanner_slam PUBLIC
    PCL_NO_PRECOMPILE
    BOOST_ALL_NO_LIB
  )
  target_link_libraries(scanner_slam PUBLIC
    scanner_core
    ${RTABMAP_CORE_LIB}
    ${PCL_LIBRARIES}
    ${OpenCV_LIBS}
  )
  if(RTABMAP_GUI_LIB)
    target_link_libraries(scanner_slam PUBLIC ${RTABMAP_GUI_LIB})
  endif()
endif()

# ---------------------------------------------------------------------------
# 可执行文件: 标定工具
# ---------------------------------------------------------------------------
add_executable(stereo_calibrate src/app/main_calibrate.cpp)
target_link_libraries(stereo_calibrate PRIVATE scanner_core ${OpenCV_LIBS})

# ---------------------------------------------------------------------------
# 可执行文件: 离线批处理 SLAM
# ---------------------------------------------------------------------------
if(FFS_FOUND AND RTABMAP_FOUND)
  add_executable(stereo_offline src/app/main_offline.cpp)
  target_link_libraries(stereo_offline PRIVATE
    scanner_core
    scanner_stereo
    scanner_slam
    ${OpenCV_LIBS}
  )
else()
  message(WARNING "Skipping stereo_offline: requires both FFS and RTAB-Map.")
endif()

# ---------------------------------------------------------------------------
# 可执行文件: 在线实时 SLAM
# ---------------------------------------------------------------------------
if(FFS_FOUND AND RTABMAP_FOUND)
  add_executable(stereo_online src/app/main_online.cpp)
  target_link_libraries(stereo_online PRIVATE
    scanner_core
    scanner_stereo
    scanner_slam
    ${OpenCV_LIBS}
  )
else()
  message(WARNING "Skipping stereo_online: requires both FFS and RTAB-Map.")
endif()

# ---------------------------------------------------------------------------
# 可执行文件: Qt GUI 主界面
# ---------------------------------------------------------------------------
# stereo_gui target marker
if(QT6_FOUND AND FFS_FOUND AND RTABMAP_FOUND)
  # 自动 MOC/UIC/RCC
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTOUIC ON)
  set(CMAKE_AUTORCC ON)

  add_executable(stereo_gui WIN32
    src/app/main_gui.cpp
    src/gui/theme.h             src/gui/theme.cpp
    src/gui/pointcloud_widget.h src/gui/pointcloud_widget.cpp
    src/gui/stereo_preview_widget.h src/gui/stereo_preview_widget.cpp
    src/gui/right_panel.h       src/gui/right_panel.cpp
    src/gui/scan_controller.h   src/gui/scan_controller.cpp
    src/gui/main_window.h       src/gui/main_window.cpp
  )
  target_include_directories(stereo_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Qt6Widgets_INCLUDE_DIRS}
    ${Qt6OpenGL_INCLUDE_DIRS}
    ${Qt6OpenGLWidgets_INCLUDE_DIRS}
    ${NVINFER_INCLUDE_DIR}
    ${RTABMAP_INCLUDE_DIRS}
    ${PCL_INCLUDE_DIRS}
  )
  target_compile_definitions(stereo_gui PRIVATE
    PCL_NO_PRECOMPILE
    BOOST_ALL_NO_LIB
  )
  target_link_libraries(stereo_gui PRIVATE
    scanner_core
    scanner_stereo
    scanner_slam
    ${OpenCV_LIBS}
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
  )
  # Windows: 拷贝 Qt6 DLL 到输出目录
  if(WIN32)
    add_custom_command(TARGET stereo_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${Qt6_DIR}/../../../plugins"
              "$<TARGET_FILE_DIR:stereo_gui>/plugins"
      COMMENT "Copying Qt6 plugins to output"
    )
  endif()
else()
  message(WARNING "Skipping stereo_gui: requires Qt6 + FFS + RTAB-Map.")
endif()

# ---------------------------------------------------------------------------
# Windows DLL 复制（运行时需要 TRT 的 DLL）
# ---------------------------------------------------------------------------
if(WIN32 AND FFS_FOUND)
  # 复制 TensorRT DLL 到输出目录
  get_filename_component(TRT_BIN_DIR "${NVINFER_LIB}" DIRECTORY)
  set(TRT_BIN_DIR "${TRT_BIN_DIR}/../bin")
  if(NOT EXISTS "${TRT_BIN_DIR}")
    get_filename_component(TRT_BIN_DIR "${NVINFER_LIB}" DIRECTORY)
  endif()
  file(GLOB TRT_DLLS "${TRT_BIN_DIR}/*.dll")
  foreach(DLL ${TRT_DLLS})
    add_custom_command(TARGET scanner_stereo POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" $<TARGET_FILE_DIR:scanner_stereo>
    )
  endforeach()
endif()

# ---------------------------------------------------------------------------
# 安装规则
# ---------------------------------------------------------------------------
install(TARGETS stereo_calibrate
  RUNTIME DESTINATION bin
)
if(TARGET stereo_offline)
  install(TARGETS stereo_offline
    RUNTIME DESTINATION bin
  )
endif()

if(TARGET stereo_online)
  install(TARGETS stereo_online
    RUNTIME DESTINATION bin
  )
endif()
if(TARGET stereo_gui)
  install(TARGETS stereo_gui
    RUNTIME DESTINATION bin)
endif()


# ---------------------------------------------------------------------------
# 摘要
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "========== stereo_scanner Configuration ==========")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  CUDA archs:    ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "  OpenCV:        ${OpenCV_VERSION}")
message(STATUS "  TensorRT:      ${NVINFER_LIB}")
message(STATUS "  FFS C++:       ${FFS_FOUND}")
message(STATUS "  RTAB-Map:      ${RTABMAP_FOUND}")
if(RTABMAP_FOUND)
  message(STATUS "  RTAB-Map lib:  ${RTABMAP_CORE_LIB}")
  message(STATUS "  PCL:           ${PCL_VERSION}")
endif()
message(STATUS "===================================================")
message(STATUS "")







