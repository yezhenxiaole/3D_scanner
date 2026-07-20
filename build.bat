@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: NEXUS-3D 自动构建脚本
:: 用法: build.bat [--tensorrt <路径>]
::
:: 前置条件:
::   1. cmake.exe 在 PATH 中
::   2. Visual Studio 2022 已安装
::   3. CUDA Toolkit >= 12.0 已安装
::   4. TensorRT 已下载到本地
:: ============================================================

title NEXUS-3D Build

:: ---- 参数解析 ----
set "TENSORRT_ROOT="
:parse_args
if not "%1"=="" (
    if "%1"=="--tensorrt" (
        set "TENSORRT_ROOT=%~2"
        shift
        shift
        goto parse_args
    )
    shift
    goto parse_args
)

:: ---- 0. 检测 cmake ----
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [X] cmake not found in PATH.
    echo     Install CMake from https://cmake.org/download/ and add to PATH.
    pause
    exit /b 1
)
for /f "tokens=3 delims= " %%a in ('cmake --version ^| findstr "cmake version"') do set "CMAKE_VER=%%a"
echo [v] cmake %CMAKE_VER%

:: ---- 1. 自动检测 VS ----
if not defined VCINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -property installationPath`) do set "VSINST=%%i"
        if defined VSINST (
            if exist "!VSINST!\VC\Auxiliary\Build\vcvars64.bat" (
                call "!VSINST!\VC\Auxiliary\Build\vcvars64.bat" x64
            )
        )
    )
    if not defined VCINSTALLDIR (
        echo [X] Visual Studio 2022 not found.
        echo     Run from "x64 Native Tools Command Prompt for VS 2022".
        pause
        exit /b 1
    )
)
echo [v] Visual Studio: %VisualStudioVersion%

:: ---- 2. 设置项目路径 ----
set "ROOT_DIR=%~dp0"
set "RTABMAP_SRC=%ROOT_DIR%3rdparty\rtabmap"
set "SCANNER_SRC=%ROOT_DIR%stereo_scanner"
set "RTABMAP_BUILD=%RTABMAP_SRC%\build"
set "RTABMAP_INSTALL=%RTABMAP_BUILD%\install"
set "SCANNER_BUILD=%SCANNER_SRC%\build"
set "VCPKG_TOOLCHAIN=%RTABMAP_SRC%\vcpkg\scripts\buildsystems\vcpkg.cmake"
set "TRIPLET=x64-windows-release"

:: ---- 2.5 询问 TensorRT 路径 ----
if not defined TENSORRT_ROOT (
    set /p "TENSORRT_ROOT=Enter TensorRT root path (e.g. C:\TensorRT-10.x.x): "
)
if not exist "%TENSORRT_ROOT%\lib\nvinfer.lib" (
    echo [X] nvinfer.lib not found at "%TENSORRT_ROOT%\lib"
    pause
    exit /b 1
)
echo [v] TensorRT: %TENSORRT_ROOT%

:: ---- 3. 验证 / 初始化 vcpkg ----
if not exist "%RTABMAP_SRC%\vcpkg.exe" (
    if not exist "%RTABMAP_SRC%\vcpkg\vcpkg.exe" (
        echo.
        echo [1/5] Cloning vcpkg (first time)...
        pushd "%RTABMAP_SRC%"
        git clone https://github.com/microsoft/vcpkg.git
        call vcpkg\bootstrap-vcpkg.bat
        popd
    )
    set "VCPKG_EXE=%RTABMAP_SRC%\vcpkg\vcpkg.exe"
    set "VCPKG_TOOLCHAIN=%RTABMAP_SRC%\vcpkg\scripts\buildsystems\vcpkg.cmake"
) else (
    set "VCPKG_EXE=%RTABMAP_SRC%\vcpkg.exe"
)

:: ---- 4. 安装 vcpkg 依赖 ----
if not exist "%RTABMAP_SRC%\vcpkg_installed\%TRIPLET%\include" (
    echo.
    echo [2/5] Installing vcpkg dependencies (first time, ~30-60 min)...
    pushd "%RTABMAP_SRC%"
    call "%VCPKG_EXE%" install --triplet %TRIPLET%
    if !errorlevel! neq 0 (
        popd
        echo [X] vcpkg install failed!
        pause
        exit /b 1
    )
    popd
) else (
    echo [v] vcpkg dependencies found
)

:: ---- 5. 构建 RTAB-Map ----
echo.
echo [3/5] Building RTAB-Map...
if not exist "%RTABMAP_BUILD%" (
    echo [+] Configuring RTAB-Map...
    cmake -S "%RTABMAP_SRC%" -B "%RTABMAP_BUILD%" -G Ninja ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DBUILD_AS_BUNDLE=OFF ^
        -DWITH_QT=OFF -DWITH_GTSAM=OFF -DWITH_OPENGV=OFF ^
        -DWITH_CERES=ON -DWITH_TORO=ON -DWITH_VERTIGO=ON ^
        -DWITH_PYTHON=OFF ^
        -DWITH_ZED=OFF -DWITH_FREENECT2=OFF ^
        -DWITH_K4W2=OFF -DWITH_K4A=OFF -DWITH_DEPTHAI=OFF ^
        -DWITH_REALSENSE2=OFF -DWITH_ORBBEC_SDK=OFF ^
        -DVCPKG_MANIFEST_INSTALL=OFF ^
        -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
        -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
        -DCMAKE_INSTALL_PREFIX="%RTABMAP_INSTALL%"
    if !errorlevel! neq 0 (
        echo [X] RTAB-Map configure failed!
        pause
        exit /b 1
    )
)

echo [+] Building RTAB-Map (this may take 10-20 minutes)...
cmake --build "%RTABMAP_BUILD%" --config Release -- -j %NUMBER_OF_PROCESSORS%
if !errorlevel! neq 0 (
    echo [X] RTAB-Map build failed!
    pause
    exit /b 1
)

echo [+] Installing RTAB-Map...
cmake --install "%RTABMAP_BUILD%" --config Release --prefix "%RTABMAP_INSTALL%"
if !errorlevel! neq 0 (
    set "RTABMAP_INSTALL=%RTABMAP_BUILD%"
)

:: 验证
if not exist "%RTABMAP_INSTALL%\lib\rtabmap_core.lib" (
    if not exist "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" (
        echo [X] rtabmap_core library not found!
        pause
        exit /b 1
    )
)
echo [v] RTAB-Map built: %RTABMAP_INSTALL%

:: ---- 6. 构建 stereo_scanner ----
echo.
echo [4/5] Configuring stereo_scanner...
if exist "%SCANNER_BUILD%" (
    rd /s /q "%SCANNER_BUILD%"
)

cmake -S "%SCANNER_SRC%" -B "%SCANNER_BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DRTABMAP_DIR="%RTABMAP_INSTALL%" ^
    -DTENSORRT_ROOT="%TENSORRT_ROOT%" ^
    -DVCPKG_MANIFEST_INSTALL=OFF ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%"
if !errorlevel! neq 0 (
    echo [X] stereo_scanner configure failed!
    pause
    exit /b 1
)

echo.
echo [5/5] Building stereo_scanner...
cmake --build "%SCANNER_BUILD%" --config Release -- -j %NUMBER_OF_PROCESSORS%
if !errorlevel! neq 0 (
    echo [X] stereo_scanner build failed!
    pause
    exit /b 1
)

:: ---- 6. 复制运行时 DLL ----
echo.
echo [+] Copying runtime DLLs...

:: RTAB-Map
if exist "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" (
    copy /Y "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" "%SCANNER_BUILD%\" >nul
    copy /Y "%RTABMAP_INSTALL%\bin\rtabmap_utilite.dll" "%SCANNER_BUILD%\" >nul
)

:: vcpkg dependencies (OpenCV, PCL, etc.)
set "VCPKG_BIN=%RTABMAP_SRC%\vcpkg_installed\%TRIPLET%\bin"
if exist "%VCPKG_BIN%" (
    copy /Y "%VCPKG_BIN%\*.dll" "%SCANNER_BUILD%\" >nul 2>&1
)

:: CUDA runtime
if defined CUDA_PATH (
    for %%F in ("%CUDA_PATH%\bin\cudart64_*.dll" "%CUDA_PATH%\bin\cublas64_*.dll" "%CUDA_PATH%\bin\cublasLt64_*.dll") do (
        if exist "%%F" copy /Y "%%F" "%SCANNER_BUILD%\" >nul
    )
)

:: TensorRT
if exist "%TENSORRT_ROOT%\lib" (
    for %%F in ("%TENSORRT_ROOT%\lib\nvinfer*.dll" "%TENSORRT_ROOT%\lib\nvonnxparser*.dll") do (
        if exist "%%F" copy /Y "%%F" "%SCANNER_BUILD%\" >nul
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
echo  Next steps:
echo    1. Run stereo_calibrate to calibrate your cameras
echo    2. Run scripts/export_ffs_engine.py to export FFS model
echo    3. Run stereo_offline to process image sequences
echo.
pause
