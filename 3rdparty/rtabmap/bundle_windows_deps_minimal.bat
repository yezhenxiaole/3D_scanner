@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: 精简版 RTAB-Map 依赖构建脚本
:: 仅安装 stereo_scanner 项目所需的最小依赖集
:: 跳过: Kinect, RealSense, Orbbec, DepthAI, libfreenect2, Python
:: ============================================================

:: --- CONFIGURATION ---
set "VCPKG_ROOT=%~dp0vcpkg"
set "EXPORT_DIR=%~dp0vcpkg_binaries"
set "SRC_DIR=%~dp0vcpkg_deps_from_source"
set "TRIPLET=x64-windows-release"

:: CMake (不在PATH中, 使用完整路径)
set "CMAKE_EXE=E:\nzy\cmake-3.30.0-rc1-windows-x86_64\cmake-3.30.0-rc1-windows-x86_64\bin\cmake.exe"

:: 7-Zip (NVIDIA自带的)
set "SEVENZIP_EXE=C:\Program Files\NVIDIA Corporation\NVIDIA App\7z.exe"

:: vcpkg baseline from vcpkg.json
set "VCPKG_JSON=%~dp0vcpkg.json"
for /f "usebackq tokens=*" %%a in (`powershell -NoProfile -Command "(Get-Content '%VCPKG_JSON%' -Raw | ConvertFrom-Json).'builtin-baseline'"` ) do set "VCPKG_COMMIT=%%a"
echo [+] Detected VCPKG baseline commit: %VCPKG_COMMIT%

if "%VCPKG_COMMIT%"=="" (
    echo [X] Error: Could not find 'builtin-baseline' in %VCPKG_JSON%
    pause
    exit /b 1
)

set VCPKG_COMMIT_SHORT=%VCPKG_COMMIT:~0,8%

:: ========================================
:: 1. Setup Local vcpkg
:: ========================================
if not exist "%VCPKG_ROOT%" (
    echo [+] Cloning vcpkg...
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
)
pushd "%VCPKG_ROOT%"
git checkout %VCPKG_COMMIT%
call .\bootstrap-vcpkg.bat
popd

:: ========================================
:: 2. Install MINIMAL vcpkg dependencies
:: ========================================
:: Only what stereo_scanner + RTAB-Map core needs:
::   - opencv (with contrib, nonfree for feature matching)
::   - pcl + vtk (for point cloud and visualization)
::   - eigen3 (for g2o, ceres)
::   - g2o (for loop closure optimization)
::   - ceres (for optimization)
::   - sqlite3 (RTAB-Map database)
::   - zlib, boost (transitive deps)
::   - qtbase (for RTAB-Map GUI/CloudViewer)
:: NOT installing: kinect, realsense, orbbec, depthai, freenect2, python, libpointmatcher

echo [+] Installing minimal dependencies via vcpkg...
"%VCPKG_ROOT%\vcpkg.exe" install ^
    --triplet=%TRIPLET% ^
    --host-triplet=%TRIPLET% ^
    --clean-after-build ^
    --x-feature=gui ^
    --x-feature=opencv-contrib ^
    --x-feature=opencv-nonfree ^
    --x-feature=openmp ^
    --x-feature=octomap ^
    --x-feature=tools || exit /b !errorlevel!

:: ========================================
:: 3. Export
:: ========================================
echo [+] Exporting built binaries...
set "VS_LOCATOR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VS_LOCATOR%" -latest -property catalog_productLineVersion`) do set VS_YEAR=vs%%i
set TARGET_NAME=vcpkg-export-%VCPKG_COMMIT_SHORT%-x64-%VS_YEAR%-minimal
set TARGET_FULL_PATH=%EXPORT_DIR%\%TARGET_NAME%
if exist "%TARGET_FULL_PATH%" rd /s /q "%TARGET_FULL_PATH%"
"%VCPKG_ROOT%\vcpkg.exe" export --raw --output-dir="%EXPORT_DIR%" --output=%TARGET_NAME% --triplet=%TRIPLET% || exit /b !errorlevel!
set "FINAL_EXPORT_PATH=%TARGET_FULL_PATH%"

:: ========================================
:: 4. Build gtsam from source (needed for RTAB-Map graph optimization)
:: ========================================
if not exist "%SRC_DIR%" mkdir "%SRC_DIR%"
cd /d "%SRC_DIR%"

echo [+] Building gtsam...
if not exist gtsam (
    git clone https://github.com/borglab/gtsam.git
    cd gtsam
    git checkout 4.3a0-ros
    git cherry-pick 18af4e6
    git apply "%~dp0patches\gtsam_4_3a0-ros.patch"
    cd ..
)
cd gtsam
"%CMAKE_EXE%" -S . -B build -GNinja ^
  -DVCPKG_MANIFEST_INSTALL=OFF ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
  -DVCPKG_INSTALLED_DIR="%FINAL_EXPORT_PATH%\installed" ^
  -DCMAKE_TOOLCHAIN_FILE="%FINAL_EXPORT_PATH%\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_INSTALL_PREFIX="%FINAL_EXPORT_PATH%\installed\%TRIPLET%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF ^
  -DGTSAM_BUILD_TESTS=OFF ^
  -DGTSAM_BUILD_UNSTABLE=OFF ^
  -DGTSAM_USE_SYSTEM_EIGEN=ON ^
  -DGTSAM_BUILD_WITH_PRECOMPILED_HEADERS=OFF ^
  -DGTSAM_UNSTABLE_BUILD_PYTHON=OFF ^
  -DGTSAM_WITH_EIGEN_MKL=OFF ^
  -DGTSAM_WITH_EIGEN_MKL_OPENMP=OFF ^
  -DCMAKE_CXX_FLAGS="-DBOOST_TIMER_ENABLE_DEPRECATED -DBOOST_BIND_GLOBAL_PLACEHOLDERS" || exit /b !errorlevel!
"%CMAKE_EXE%" --build build --config Release --target install || exit /b !errorlevel!
cd ..

:: ========================================
:: 5. Build opengv from source (optional, for RTAB-Map stereo visual odometry)
:: ========================================
echo [+] Building opengv...
if not exist opengv (
    git clone https://github.com/laurentkneip/opengv.git
    cd opengv
    git checkout 91f4b19c73450833a40e463ad3648aae80b3a7f3
    git apply "%~dp0patches\opengv_91f4b19c.patch"
    cd ..
)
cd opengv
"%CMAKE_EXE%" -S . -B build -GNinja ^
  -DVCPKG_MANIFEST_INSTALL=OFF ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
  -DVCPKG_INSTALLED_DIR="%FINAL_EXPORT_PATH%\installed" ^
  -DCMAKE_TOOLCHAIN_FILE="%FINAL_EXPORT_PATH%\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_INSTALL_PREFIX="%FINAL_EXPORT_PATH%\installed\%TRIPLET%" ^
  -DBUILD_TESTS=OFF ^
  -DBUILD_SHARED_LIBS=ON || exit /b !errorlevel!
"%CMAKE_EXE%" --build build --config Release --target install || exit /b !errorlevel!
cd ..

:: ========================================
:: 6. Create final package
:: ========================================
echo [+] Creating final package...
cd /d "%FINAL_EXPORT_PATH%"
del /s /q /f *.pdb >nul 2>&1
cd ..

set "FINAL_ZIP=%TARGET_NAME%.7z"
"%SEVENZIP_EXE%" u -t7z -mx9 "%FINAL_ZIP%" "%TARGET_FULL_PATH%\*" -up0q0

if !errorlevel! EQU 0 (
    echo [!] SUCCESS! Package created at %EXPORT_DIR%\%FINAL_ZIP%
) else (
    echo [X] 7-Zip failed with error code !errorlevel!
)

echo.
echo ============================================
echo Next step: Build RTAB-Map using the export
echo ============================================
echo.
echo Set these variables before building RTAB-Map:
echo   set VCPKG_UNZIPPED_EXPORT_PATH=%FINAL_EXPORT_PATH%
echo   set PATH=%%VCPKG_UNZIPPED_EXPORT_PATH%%\installed\x64-windows-release\bin;%%PATH%%
echo.
echo Then run:
echo   "%CMAKE_EXE%" -B build -GNinja ^
echo     -DCMAKE_BUILD_TYPE=Release ^
echo     -DBUILD_AS_BUNDLE=ON ^
echo     -DWITH_ZED=OFF ^
echo     -DWITH_CERES=ON ^
echo     -DWITH_FREENECT2=OFF ^
echo     -DWITH_K4W2=OFF ^
echo     -DWITH_K4A=OFF ^
echo     -DWITH_DEPTHAI=OFF ^
echo     -DWITH_REALSENSE2=OFF ^
echo     -DWITH_ORBBEC_SDK=OFF ^
echo     -DWITH_PYTHON=OFF ^
echo     -DVCPKG_MANIFEST_INSTALL=OFF ^
echo     -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
echo     -DVCPKG_INSTALLED_DIR="%%VCPKG_UNZIPPED_EXPORT_PATH%%/installed" ^
echo     -DCMAKE_TOOLCHAIN_FILE=%%VCPKG_UNZIPPED_EXPORT_PATH%%/scripts/buildsystems/vcpkg.cmake ^
echo     -DGTSAM_DIR=%%VCPKG_UNZIPPED_EXPORT_PATH%%\installed\x64-windows-release\CMake
echo.
echo   "%CMAKE_EXE%" --build build --config Release

pause
