@echo off

rem ============================================================
rem NEXUS-3D Full Build Script (VS2026 + Qt6 + CUDA 13.3)
rem Usage: build_vs2026.bat [--tensorrt <path>]
rem
rem Prerequisites:
rem   1. cmake.exe in PATH
rem   2. Visual Studio 2026 installed
rem   3. CUDA Toolkit >= 13.3 installed
rem   4. TensorRT 10.16.1.11 downloaded
rem   5. Qt 6.11.1 msvc2022_64 installed
rem
rem This script replaces the old build.bat (VS2022 + Qt5 + CUDA 12.x)
rem ============================================================

title NEXUS-3D Build VS2026

rem ---- Parameter parsing (--tensorrt overrides env var) ----
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

rem ---- 0. Auto-detect VS2026 (must be before cmake/ninja checks) ----
if not defined VCINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%VSWHERE%" (
        set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    if exist "%VSWHERE%" (
        for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSINST=%%i"
        if defined VSINST (
            if exist "%VSINST%\VC\Auxiliary\Build\vcvars64.bat" (
                call "%VSINST%\VC\Auxiliary\Build\vcvars64.bat" x64
            )
        )
    )
    if not defined VCINSTALLDIR (
        echo [X] Visual Studio 2026 not found.
        echo     Run from "x64 Native Tools Command Prompt for VS 2026".
        pause
        exit /b 1
    )
)
echo [v] Visual Studio: %VisualStudioVersion%  MSVC: %VCToolsVersion%

rem ---- 0.1 Check cmake (after vcvars64 so VS-bundled cmake/ninja are available) ----
where cmake >nul 2>&1
if errorlevel 1 (
    echo [X] cmake not found in PATH.
    echo     Install CMake from https://cmake.org/download/ and add to PATH.
    pause
    exit /b 1
)
for /f "tokens=3 delims= " %%a in ('cmake --version ^| findstr "cmake version"') do set "CMAKE_VER=%%a"
echo [v] cmake %CMAKE_VER%

where ninja >nul 2>&1
if errorlevel 1 (
    echo [X] ninja not found in PATH.
    echo     Run from "x64 Native Tools Command Prompt for VS 2026" or add ninja to PATH.
    pause
    exit /b 1
)
echo [v] ninja found

rem ---- 2. Project paths ----
set "ROOT_DIR=%~dp0"
set "RTABMAP_SRC=%ROOT_DIR%3rdparty\rtabmap"
set "SCANNER_SRC=%ROOT_DIR%stereo_scanner"
set "RTABMAP_BUILD=%RTABMAP_SRC%\build_vs2026"
set "RTABMAP_INSTALL=%RTABMAP_BUILD%\install"
set "SCANNER_BUILD=%SCANNER_SRC%\build_vs2026"
set "VCPKG_TOOLCHAIN=%RTABMAP_SRC%\vcpkg\scripts\buildsystems\vcpkg.cmake"
set "TRIPLET=x64-windows-release"
set "Qt6_DIR=%Qt6_DIR%"
if not defined Qt6_DIR (
    set /p "Qt6_DIR=Enter Qt6 msvc2022_64 path (e.g. G:\nzy\Qt_6.11\6.11.1\msvc2022_64): "
) else (
    echo [v] Qt6_DIR from environment: %Qt6_DIR%
)

rem ---- 2.5 TensorRT path (env var TENSORRT_ROOT or --tensorrt arg or interactive) ----
if not defined TENSORRT_ROOT (
    set /p "TENSORRT_ROOT=Enter TensorRT root path (e.g. G:\nzy\TensorRT-10.16.1.11): "
) else (
    echo [v] TENSORRT_ROOT from environment: %TENSORRT_ROOT%
)
if not exist "%TENSORRT_ROOT%\lib\nvinfer.lib" (
    if not exist "%TENSORRT_ROOT%\lib\nvinfer_10.lib" (
        echo [X] nvinfer.lib not found at "%TENSORRT_ROOT%\lib"
        pause
        exit /b 1
    )
)
echo [v] TensorRT: %TENSORRT_ROOT%

rem ---- 2.6 Qt6 path ----
if not exist "%Qt6_DIR%\lib\Qt6Widgets.lib" (
    echo [X] Qt6 not found at %Qt6_DIR%
    echo     Install Qt 6.11.1 msvc2022_64 or set Qt6_DIR
    pause
    exit /b 1
)
echo [v] Qt6: %Qt6_DIR%

rem ---- 3. Verify vcpkg ----
set "VCPKG_INC=%ROOT_DIR%3rdparty\vcpkg_installed\%TRIPLET%\include"
if not exist "%VCPKG_INC%\opencv4\opencv2\core.hpp" (
    echo.
    echo [2/6] Installing vcpkg dependencies (first time, 6-10 hours)...
    pushd "%RTABMAP_SRC%"
    call vcpkg\vcpkg.exe install --triplet %TRIPLET% --x-install-root=..\vcpkg_installed
    if errorlevel 1 (
        popd
        echo [X] vcpkg install failed!
        pause
        exit /b 1
    )
    popd
) else (
    echo [v] vcpkg dependencies found
)

rem ---- 4. Build RTAB-Map ----
echo.
echo [3/6] Building RTAB-Map...
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
        -DVCPKG_INSTALLED_DIR="%ROOT_DIR%3rdparty\vcpkg_installed" ^
        -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
        -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
        -DCMAKE_INSTALL_PREFIX="%RTABMAP_INSTALL%"
    if errorlevel 1 (
        echo [X] RTAB-Map configure failed!
        pause
        exit /b 1
    )
)

echo [+] Building RTAB-Map (10-20 minutes)...
cmake --build "%RTABMAP_BUILD%" --config Release -- -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo [X] RTAB-Map build failed!
    pause
    exit /b 1
)

echo [+] Installing RTAB-Map...
cmake --install "%RTABMAP_BUILD%" --config Release --prefix "%RTABMAP_INSTALL%"
if errorlevel 1 (
    set "RTABMAP_INSTALL=%RTABMAP_BUILD%"
)

rem Verify
if not exist "%RTABMAP_INSTALL%\include\rtabmap-0.23" (
    echo [X] RTAB-Map install verification failed!
    pause
    exit /b 1
)
echo [v] RTAB-Map built: %RTABMAP_INSTALL%

rem ---- 5. Build stereo_scanner ----
echo.
echo [4/6] Configuring stereo_scanner...
if exist "%SCANNER_BUILD%" (
    rd /s /q "%SCANNER_BUILD%"
)

cmake -S "%SCANNER_SRC%" -B "%SCANNER_BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%Qt6_DIR%" ^
    -DRTABMAP_DIR="%RTABMAP_INSTALL%" ^
    -DTENSORRT_ROOT="%TENSORRT_ROOT%" ^
    -DVCPKG_INSTALLED_DIR="%ROOT_DIR%3rdparty\vcpkg_installed" ^
    -DVCPKG_TARGET_TRIPLET=%TRIPLET% ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%"
if errorlevel 1 (
    echo [X] stereo_scanner configure failed!
    pause
    exit /b 1
)

echo.
echo [5/6] Building stereo_scanner...
cmake --build "%SCANNER_BUILD%" --config Release -- -j %NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo [X] stereo_scanner build failed!
    pause
    exit /b 1
)

rem ---- 6. Copy runtime DLLs ----
echo.
echo [6/6] Copying runtime DLLs and deploying Qt6...

rem RTAB-Map
if exist "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" (
    copy /Y "%RTABMAP_INSTALL%\bin\rtabmap_core.dll" "%SCANNER_BUILD%\" >nul
    copy /Y "%RTABMAP_INSTALL%\bin\rtabmap_utilite.dll" "%SCANNER_BUILD%\" >nul
)

rem vcpkg dependencies
set "VCPKG_BIN=%ROOT_DIR%3rdparty\vcpkg_installed\%TRIPLET%\bin"
if exist "%VCPKG_BIN%" (
    copy /Y "%VCPKG_BIN%\*.dll" "%SCANNER_BUILD%\" >nul 2>&1
)

rem CUDA runtime
if not defined CUDA_PATH (
    echo [!] CUDA_PATH not set - CUDA DLLs will not be copied.
    echo     Install CUDA Toolkit 13.3+ and ensure CUDA_PATH is set.
)
if defined CUDA_PATH (
    for %%F in ("%CUDA_PATH%\bin\cudart64_*.dll" "%CUDA_PATH%\bin\cublas64_*.dll" "%CUDA_PATH%\bin\cublasLt64_*.dll") do (
        if exist "%%F" copy /Y "%%F" "%SCANNER_BUILD%\" >nul
    )
)

rem TensorRT
if exist "%TENSORRT_ROOT%\bin" (
    for %%F in ("%TENSORRT_ROOT%\bin\nvinfer*.dll" "%TENSORRT_ROOT%\bin\nvonnxparser*.dll") do (
        if exist "%%F" copy /Y "%%F" "%SCANNER_BUILD%\" >nul
    )
) else if exist "%TENSORRT_ROOT%\lib" (
    for %%F in ("%TENSORRT_ROOT%\lib\nvinfer*.dll" "%TENSORRT_ROOT%\lib\nvonnxparser*.dll") do (
        if exist "%%F" copy /Y "%%F" "%SCANNER_BUILD%\" >nul
    )
)

rem Qt6 deployment via windeployqt
set "WINDEPLOYQT=%Qt6_DIR%\bin\windeployqt.exe"
if exist "%WINDEPLOYQT%" (
    if exist "%SCANNER_BUILD%\stereo_gui.exe" (
        "%WINDEPLOYQT%" "%SCANNER_BUILD%\stereo_gui.exe" --release --no-translations --no-opengl-sw
    )
)

echo.
echo ============================================
echo  BUILD SUCCESSFUL!
echo ============================================
echo.
echo  stereo_calibrate: %SCANNER_BUILD%\stereo_calibrate.exe
echo  stereo_offline:   %SCANNER_BUILD%\stereo_offline.exe
echo  stereo_gui:       %SCANNER_BUILD%\stereo_gui.exe
echo.
echo  Next steps:
echo    1. Run stereo_gui.exe to verify Qt6 5-region rendering
echo    2. Rebuild FFS engine for sm_120: rebuild_ffs_engine.bat
echo    3. Run stereo_calibrate to calibrate cameras
echo.
pause
