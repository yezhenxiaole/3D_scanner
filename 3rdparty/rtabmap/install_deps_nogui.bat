@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: 精简版 vcpkg 安装脚本（无 GUI）
:: 跳过 Qt5/Qt6、VTK、PCL visualization
:: 适用于 16GB 内存环境，避免 qtdeclarative OOM
:: ============================================================

call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" x64

set "VCPKG_ROOT=E:\3D_scanner\structured+RTAB_map\rtabmap\vcpkg"
set "TRIPLET=x64-windows-release"

cd /d "E:\3D_scanner\structured+RTAB_map\rtabmap"

echo ============================================================
echo  Installing vcpkg dependencies (NO GUI, NO Qt, NO VTK)
echo  Already installed: OpenCV4, g2o, ceres, boost, eigen3
echo  Will install: PCL (no viz), sqlite3, zlib, yaml-cpp, octomap
echo ============================================================
echo.

:: 不带 --x-feature=gui，避免拉入 Qt/VTK/PCL-viz
"%VCPKG_ROOT%\vcpkg.exe" install ^
    --triplet=%TRIPLET% ^
    --host-triplet=%TRIPLET% ^
    --clean-after-build ^
    --x-feature=opencv-contrib ^
    --x-feature=opencv-nonfree ^
    --x-feature=openmp ^
    --x-feature=octomap ^
    --x-feature=tools

if %errorlevel% NEQ 0 (
    echo.
    echo [X] vcpkg install failed!
    echo [X] Check the error above.
    pause
    exit /b 1
)

echo.
echo ============================================================
echo  vcpkg install COMPLETE (no GUI mode)
echo ============================================================
echo.
echo  Now run: build_all.bat to build RTAB-Map + stereo_scanner
echo.
pause
