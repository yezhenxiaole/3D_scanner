@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: stereo_scanner 完整构建脚本 (简化版)
:: 前提: vcpkg install 已完成 (install_deps_nogui.bat)
:: 本脚本: 构建 RTAB-Map → 构建 stereo_scanner
::
:: 关键决策:
::   - WITH_QT=OFF    → 不需要 VTK, 不编译 GUI
::   - WITH_GTSAM=OFF → 用 g2o 替代 (vcpkg 已安装)
::   - WITH_OPENGV=OFF → 不需要立体视觉里程计
::   - 直接使用 vcpkg_installed, 不需要 vcpkg export
:: ============================================================

:: --- 路径配置 ---
set "RTABMAP_SRC=%~dp0..\3rdparty\rtabmap"
set "SCANNER_SRC=%~dp0"
set "CMAKE_EXE=E:\nzy\cmake-3.30.0-rc1-windows-x86_64\cmake-3.30.0-rc1-windows-x86_64\bin\cmake.exe"
set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "TRIPLET=x64-windows-release"
set "VCPKG_ROOT=%RTABMAP_SRC%\vcpkg"
set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
:: 使用 junction 路径 (在源码树外), 避免 CMake INTERFACE_INCLUDE_DIRECTORIES 报错
set "VCPKG_INSTALLED=E:\3D_scanner\structured+RTAB_map\vcpkg_installed"

:: vcvars64
if not defined VCINSTALLDIR (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" x64
)

:: 设置 PATH (DLL 运行时需要)
set "PATH=%VCPKG_INSTALLED%\%TRIPLET%\bin;%PATH%"

:: 验证 vcpkg 依赖已安装
if not exist "%VCPKG_INSTALLED%\%TRIPLET%\include" (
    echo [X] Error: vcpkg dependencies not found at %VCPKG_INSTALLED%
    echo [X] Run install_deps_nogui.bat first!
    pause
    exit /b 1
)

echo [+] vcpkg dependencies found: %VCPKG_INSTALLED%

:: ============================================================
:: 1. 构建 RTAB-Map (无 GUI, 无 GTSAM, 无 OpenGV)
:: ============================================================
echo.
echo [1/3] Building RTAB-Map...

set "RTABMAP_BUILD=%RTABMAP_SRC%\build"
set "RTABMAP_INSTALL=%RTABMAP_BUILD%\install"

if not exist "%RTABMAP_BUILD%" (
    echo [+] Configuring RTAB-Map...
    "%CMAKE_EXE%" -S "%RTABMAP_SRC%" -B "%RTABMAP_BUILD%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
        -DBUILD_AS_BUNDLE=OFF ^
        -DWITH_QT=OFF ^
        -DWITH_GTSAM=OFF ^
        -DWITH_OPENGV=OFF ^
        -DWITH_CERES=ON ^
        -DWITH_ZED=OFF ^
        -DWITH_FREENECT2=OFF ^
        -DWITH_K4W2=OFF ^
        -DWITH_K4A=OFF ^
        -DWITH_DEPTHAI=OFF ^
        -DWITH_REALSENSE2=OFF ^
        -DWITH_ORBBEC_SDK=OFF ^
        -DWITH_PYTHON=OFF ^
        -DWITH_TORO=ON ^
        -DWITH_VERTIGO=ON ^
        -DVCPKG_MANIFEST_INSTALL=OFF ^
        -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
        -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED%" ^
        -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
        -DCMAKE_INSTALL_PREFIX="%RTABMAP_INSTALL%" || (
            echo [X] RTAB-Map configure failed!
            pause
            exit /b 1
        )
)

echo [+] Building RTAB-Map (this may take 10-20 minutes)...
"%CMAKE_EXE%" --build "%RTABMAP_BUILD%" --config Release -- -j %NUMBER_OF_PROCESSORS% || (
    echo [X] RTAB-Map build failed!
    pause
    exit /b 1
)

echo [+] Installing RTAB-Map...
"%CMAKE_EXE%" --install "%RTABMAP_BUILD%" --config Release --prefix "%RTABMAP_INSTALL%" || (
    echo [!] RTAB-Map install failed, will use build directory directly.
    set "RTABMAP_INSTALL=%RTABMAP_BUILD%"
)

:: 验证 RTAB-Map 库
if not exist "%RTABMAP_INSTALL%\lib\rtabmap_core.lib" (
    if not exist "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" (
        echo [!] Warning: rtabmap_core not found in install dir, checking build dir...
        if exist "%RTABMAP_BUILD%\lib\rtabmap_core.lib" (
            set "RTABMAP_INSTALL=%RTABMAP_BUILD%"
        ) else (
            echo [X] Error: rtabmap_core library not found!
            pause
            exit /b 1
        )
    )
)

echo [+] RTAB-Map built successfully at: %RTABMAP_INSTALL%

:: 将 RTAB-Map DLL 加入 PATH
set "PATH=%RTABMAP_INSTALL%\bin;%PATH%"

:: ============================================================
:: 2. 配置 stereo_scanner
:: ============================================================
echo.
echo [2/3] Configuring stereo_scanner...

set "SCANNER_BUILD=%SCANNER_SRC%build"

if exist "%SCANNER_BUILD%" (
    echo [+] Cleaning previous build...
    rd /s /q "%SCANNER_BUILD%"
)

"%CMAKE_EXE%" -S "%SCANNER_SRC%" -B "%SCANNER_BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
    -DRTABMAP_DIR="%RTABMAP_INSTALL%" ^
    -DVCPKG_MANIFEST_INSTALL=OFF ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALLED%" ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" || (
        echo [X] stereo_scanner configure failed!
        pause
        exit /b 1
    )

:: ============================================================
:: 3. 构建 stereo_scanner
:: ============================================================
echo.
echo [3/3] Building stereo_scanner...

"%CMAKE_EXE%" --build "%SCANNER_BUILD%" --config Release -- -j %NUMBER_OF_PROCESSORS% || (
    echo [X] stereo_scanner build failed!
    pause
    exit /b 1
)

:: ============================================================
:: 复制运行时 DLL 到输出目录
:: ============================================================
echo [+] Copying runtime DLLs...

:: RTAB-Map DLLs
if exist "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" (
    copy /Y "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" "%SCANNER_BUILD%\" >nul 2>&1
)
if exist "%RTABMAP_INSTALL%\bin\rtabmap_utilite.dll" (
    copy /Y "%RTABMAP_INSTALL%\bin\rtabmap_utilite.dll" "%SCANNER_BUILD%\" >nul 2>&1
)

:: vcpkg DLLs: 直接复制全部 DLL (避免遗漏传递依赖)
:: vcpkg OpenCV 是分模块编译 (opencv_core4.dll 等), 没有 opencv_world
echo [+] Copying all vcpkg DLLs...
for %%F in ("%VCPKG_INSTALLED%\%TRIPLET%\bin\*.dll") do (
    copy /Y "%%F" "%SCANNER_BUILD%\" >nul 2>&1
)

:: CUDA 运行时 DLLs
set "CUDA_BIN=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.3\bin"
if exist "%CUDA_BIN%" (
    for %%F in ("%CUDA_BIN%\cudart64_*.dll" "%CUDA_BIN%\cublas64_*.dll" "%CUDA_BIN%\cublasLt64_*.dll" "%CUDA_BIN%\nvrtc64_*.dll") do (
        copy /Y "%%F" "%SCANNER_BUILD%\" >nul 2>&1
    )
)

:: TensorRT DLLs
set "TRT_LIB=E:\nzy\TensorRT-10.13.0\lib"
if exist "%TRT_LIB%" (
    for %%F in ("%TRT_LIB%\nvinfer_*.dll" "%TRT_LIB%\nvinfer_plugin_*.dll" "%TRT_LIB%\nvonnxparser_*.dll") do (
        copy /Y "%%F" "%SCANNER_BUILD%\" >nul 2>&1
    )
)

echo.
echo ============================================
echo  BUILD SUCCESSFUL!
echo ============================================
echo.
echo  stereo_calibrate: %SCANNER_BUILD%\stereo_calibrate.exe
echo  stereo_offline:   %SCANNER_BUILD%\stereo_offline.exe
echo.
echo  RTAB-Map install: %RTABMAP_INSTALL%
echo  vcpkg deps:       %VCPKG_INSTALLED%\%TRIPLET%
echo.
echo  Next steps:
echo    1. Run stereo_calibrate to calibrate your cameras
echo    2. Run scripts/export_ffs_engine.py to export FFS model
echo    3. Run stereo_offline to process image sequences
echo.

pause
