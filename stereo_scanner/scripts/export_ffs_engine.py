#!/usr/bin/env python3
"""
FFS 模型导出脚本 — 一次性执行: 下载权重 → 导出 ONNX → 编译 TRT 引擎

整个流程完成后, C++ 运行时零 Python 依赖。

使用方式:
  # 1. 完整流程 (首次)
  python export_ffs_engine.py --model_dir <path/to/model_best.pth> --output_dir models/ffs_engine

  # 2. 仅导出 ONNX (已有权重)
  python export_ffs_engine.py --step onnx --model_dir <path> --output_dir models/ffs_engine

  # 3. 仅编译 TRT 引擎 (已有 ONNX)
  python export_ffs_engine.py --step trt --onnx_path models/ffs_engine/fast_foundationstereo.onnx --output_dir models/ffs_engine

  # 4. 使用 trtexec (推荐, 更快)
  python export_ffs_engine.py --step onnx --model_dir <path> --output_dir models/ffs_engine
  # 然后手动:
  trtexec --onnx=models/ffs_engine/fast_foundationstereo.onnx --saveEngine=models/ffs_engine/fast_foundationstereo.engine

注意: 默认使用 FP32 精度。FP16 在 Fast-FoundationStereo 模型上会产生全 NaN 输出
(数值溢出), 不要使用 --fp16 除非已验证特定模型版本兼容。

依赖: 3D_scanner conda 环境 (Python 3.12, torch, onnx, tensorrt)
"""

import argparse
import logging
import os
import shutil
import subprocess
import sys
from pathlib import Path

logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')
log = logging.getLogger(__name__)


def find_ffs_repo() -> Path:
    """查找 FFS 仓库根目录"""
    # 相对于本脚本: scripts/ → stereo_scanner/ → structured+RTAB_map/
    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir.parent / "3rdparty" / "Fast-FoundationStereo-master",
        script_dir.parent.parent / "3rdparty" / "Fast-FoundationStereo-master",
    ]
    for p in candidates:
        if (p / "scripts" / "make_single_onnx.py").exists():
            return p
    raise FileNotFoundError(
        "Cannot find Fast-FoundationStereo repo. "
        "Expected at ../3rdparty/Fast-FoundationStereo-master/"
    )


def step_onnx(ffs_repo: Path, model_dir: str, output_dir: str,
              height: int, width: int, max_disp: int, valid_iters: int) -> str:
    """Step 1: 导出 ONNX 模型"""
    onnx_script = ffs_repo / "scripts" / "make_single_onnx.py"
    if not onnx_script.exists():
        raise FileNotFoundError(f"make_single_onnx.py not found at {onnx_script}")

    # 检查模型权重
    model_path = Path(model_dir)
    if not model_path.exists():
        # 尝试 FFS weights 目录
        alt = ffs_repo / "weights" / "model_best_bp2_serialize.pth"
        if alt.exists():
            model_dir = str(alt)
            log.info(f"Using default weights: {alt}")
        else:
            raise FileNotFoundError(
                f"Model weights not found: {model_path}\n"
                f"Download from Google Drive first, see FFS README."
            )

    os.makedirs(output_dir, exist_ok=True)

    cmd = [
        sys.executable, str(onnx_script),
        "--model_dir", model_dir,
        "--save_path", output_dir,
        "--height", str(height),
        "--width", str(width),
        "--max_disp", str(max_disp),
        "--valid_iters", str(valid_iters),
    ]

    log.info(f"Exporting ONNX: {' '.join(cmd)}")
    result = subprocess.run(cmd, check=True)

    onnx_path = os.path.join(output_dir, "fast_foundationstereo.onnx")
    if not os.path.exists(onnx_path):
        raise RuntimeError(f"ONNX export failed: {onnx_path} not found")

    log.info(f"ONNX exported: {onnx_path}")
    return onnx_path


def step_trt(onnx_path: str, output_dir: str, fp16: bool = False,
             workspace_mb: int = 4096, use_trtexec: bool = True,
             plugin_lib: str = "") -> str:
    """Step 2: 编译 TensorRT 引擎"""
    onnx = Path(onnx_path)
    if not onnx.exists():
        raise FileNotFoundError(f"ONNX file not found: {onnx_path}")

    engine_path = os.path.join(output_dir, "fast_foundationstereo.engine")

    # 方法1: 使用 trtexec (推荐, 速度快, 自动注册 plugin)
    if use_trtexec:
        trtexec_cmd = _find_trtexec()
        if trtexec_cmd:
            cmd = [
                trtexec_cmd,
                f"--onnx={onnx_path}",
                f"--saveEngine={engine_path}",
                f"--memPoolSize=workspace:{workspace_mb}M",
            ]
            if fp16:
                cmd.append("--fp16")

            log.info(f"Building TRT engine with trtexec: {' '.join(cmd)}")
            subprocess.run(cmd, check=True)

            if os.path.exists(engine_path):
                log.info(f"TRT engine built: {engine_path}")
                return engine_path
            else:
                log.warning("trtexec failed, falling back to Python build")

    # 方法2: 使用 FFS 的 Python TRT 构建脚本 (需要 plugin .so)
    ffs_repo = find_ffs_repo()
    build_script = ffs_repo / "scripts" / "build_plugin_trt.py"

    if build_script.exists():
        cmd = [
            sys.executable, str(build_script),
            onnx_path,
            engine_path,
            f"--workspace-mb={workspace_mb}",
        ]
        if not fp16:
            cmd.append("--fp32")
        if plugin_lib:
            cmd.extend(["--plugin_lib", plugin_lib])

        log.info(f"Building TRT engine with Python: {' '.join(cmd)}")
        try:
            subprocess.run(cmd, check=True)
            if os.path.exists(engine_path):
                return engine_path
        except Exception as e:
            log.warning(f"Python TRT build failed: {e}")

    # 方法3: 使用 C++ build_single_engine (最可靠)
    cpp_build = ffs_repo / "cpp" / "build"
    cpp_engine_builder = None
    for name in ["ffs_build_single_engine", "ffs_build_single_engine.exe",
                 "Release/ffs_build_single_engine.exe"]:
        if (cpp_build / name).exists():
            cpp_engine_builder = str(cpp_build / name)
            break

    if cpp_engine_builder:
        log.info(f"Building TRT engine with C++ builder: {cpp_engine_builder}")
        # C++ builder 的参数格式需要查看源码
        # 通常: ffs_build_single_engine <onnx> <engine_output>
        cmd = [cpp_engine_builder, onnx_path, engine_path]
        try:
            subprocess.run(cmd, check=True)
            if os.path.exists(engine_path):
                return engine_path
        except Exception as e:
            log.warning(f"C++ TRT build failed: {e}")

    raise RuntimeError(
        f"Failed to build TRT engine. Try manually:\n"
        f"  trtexec --onnx={onnx_path} --saveEngine={engine_path}\n"
        f"(FP32 is recommended; FP16 produces all-NaN output on this model)"
    )


def _find_trtexec() -> str:
    """查找 trtexec 可执行文件"""
    # Windows - 常见安装位置 (含当前环境路径)
    for p in [
        "E:/nzy/TensorRT-10.16.1.11/bin/trtexec.exe",
        "E:/nzy/TensorRT-10.16.1.11/trtexec.exe",
        "C:/TensorRT/bin/trtexec.exe",
        "C:/TensorRT-10/bin/trtexec.exe",
    ]:
        if os.path.exists(p):
            return p

    # Linux
    for p in [
        "/usr/src/tensorrt/bin/trtexec",
        "/usr/local/tensorrt/bin/trtexec",
    ]:
        if os.path.exists(p):
            return p

    # 在 PATH 中查找
    trtexec = shutil.which("trtexec")
    if trtexec:
        return trtexec

    return ""


def step_k_txt(output_dir: str, fx: float, fy: float,
               cx: float, cy: float, baseline: float) -> str:
    """生成 K.txt (FFS 需要的相机内参文件)"""
    k_path = os.path.join(output_dir, "K.txt")
    with open(k_path, 'w') as f:
        # 第1行: 3x3 内参矩阵展平
        f.write(f"{fx} 0.0 {cx} 0.0 {fy} {cy} 0.0 0.0 1.0\n")
        # 第2行: baseline (米)
        f.write(f"{baseline}\n")
    log.info(f"K.txt written: {k_path}")
    return k_path


def main():
    parser = argparse.ArgumentParser(
        description='FFS Model Export: ONNX → TensorRT Engine (one-time setup)')
    parser.add_argument('--step', choices=['all', 'onnx', 'trt'], default='all',
                        help='执行步骤: all=完整流程, onnx=仅导出ONNX, trt=仅编译TRT')
    parser.add_argument('--model_dir', type=str, default='',
                        help='FFS 模型权重 .pth 路径')
    parser.add_argument('--onnx_path', type=str, default='',
                        help='已有 ONNX 文件路径 (step=trt 时使用)')
    parser.add_argument('--output_dir', type=str, default='models/ffs_engine',
                        help='输出目录 (存放 .onnx + .engine + .yaml)')
    parser.add_argument('--height', type=int, default=480,
                        help='输入图像高度 (需32整除)')
    parser.add_argument('--width', type=int, default=640,
                        help='输入图像宽度 (需32整除)')
    parser.add_argument('--max_disp', type=int, default=192,
                        help='最大视差搜索范围')
    parser.add_argument('--valid_iters', type=int, default=8,
                        help='GRU迭代次数 (离线8, 实时4)')
    parser.add_argument('--fp16', action='store_true', default=False,
                        help='启用 FP16 (不推荐: 此模型 FP16 会产生全 NaN 输出)')
    parser.add_argument('--fp32', action='store_true', default=True,
                        help='强制 FP32 (默认, 推荐)')
    parser.add_argument('--workspace_mb', type=int, default=4096,
                        help='TRT workspace 内存限制(MiB)')
    # K.txt 参数 (可选, 用于生成 FFS 的相机内参文件)
    parser.add_argument('--fx', type=float, default=0,
                        help='焦距 fx (像素), 用于生成 K.txt')
    parser.add_argument('--fy', type=float, default=0,
                        help='焦距 fy (像素)')
    parser.add_argument('--cx', type=float, default=0,
                        help='主点 cx (像素)')
    parser.add_argument('--cy', type=float, default=0,
                        help='主点 cy (像素)')
    parser.add_argument('--baseline', type=float, default=0,
                        help='基线距离 (米), 用于生成 K.txt')
    args = parser.parse_args()

    if args.height % 32 != 0 or args.width % 32 != 0:
        raise ValueError(f"height={args.height} and width={args.width} must be divisible by 32")

    fp16 = args.fp16
    if fp16:
        log.warning("=" * 60)
        log.warning("WARNING: FP16 mode enabled!")
        log.warning("Fast-FoundationStereo FP16 engines produce ALL-NaN output")
        log.warning("due to numerical overflow. FP32 is strongly recommended.")
        log.warning("Only use FP16 if you have verified it works for your model.")
        log.warning("=" * 60)

    ffs_repo = find_ffs_repo()
    log.info(f"FFS repo: {ffs_repo}")

    os.makedirs(args.output_dir, exist_ok=True)

    onnx_path = args.onnx_path

    # Step 1: ONNX 导出
    if args.step in ('all', 'onnx'):
        onnx_path = step_onnx(
            ffs_repo, args.model_dir, args.output_dir,
            args.height, args.width, args.max_disp, args.valid_iters
        )

    # Step 2: TRT 引擎编译
    if args.step in ('all', 'trt'):
        if not onnx_path:
            onnx_path = os.path.join(args.output_dir, "fast_foundationstereo.onnx")
        if not os.path.exists(onnx_path):
            raise FileNotFoundError(f"ONNX file not found: {onnx_path}")

        engine_path = step_trt(
            onnx_path, args.output_dir,
            fp16=fp16, workspace_mb=args.workspace_mb
        )
        log.info(f"TRT engine: {engine_path}")

    # 可选: 生成 K.txt
    if args.fx > 0 and args.baseline > 0:
        fy = args.fy if args.fy > 0 else args.fx
        cx = args.cx if args.cx > 0 else args.width / 2.0
        cy = args.cy if args.cy > 0 else args.height / 2.0
        step_k_txt(args.output_dir, args.fx, fy, cx, cy, args.baseline)

    # 最终摘要
    log.info("=" * 50)
    log.info("Export complete!")
    log.info(f"  Output dir: {args.output_dir}")
    if os.path.exists(os.path.join(args.output_dir, "fast_foundationstereo.onnx")):
        log.info(f"  ONNX: fast_foundationstereo.onnx")
    if os.path.exists(os.path.join(args.output_dir, "fast_foundationstereo.engine")):
        log.info(f"  Engine: fast_foundationstereo.engine")
    log.info(f"  Resolution: {args.height} x {args.width}")
    log.info(f"  Max disp: {args.max_disp}")
    log.info(f"  Valid iters: {args.valid_iters}")
    log.info("=" * 50)


if __name__ == '__main__':
    main()
