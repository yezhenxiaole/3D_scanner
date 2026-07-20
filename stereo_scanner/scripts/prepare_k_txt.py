#!/usr/bin/env python3
"""
K.txt 辅助生成脚本

为 Fast-FoundationStereo 生成 K.txt 内参文件。
支持两种输入来源:
  1. 从 stereo_scanner 标定结果 (cali.yml) 自动读取
  2. 手动指定内参参数

FFS K.txt 格式:
  第1行: fx 0 cx 0 fy cy 0 0 1  (3x3内参矩阵展平, 空格分隔)
  第2行: baseline (米)

使用方式:
  # 从标定文件生成
  python prepare_k_txt.py --cali config/cali.yml --output models/ffs_engine/K.txt

  # 手动指定参数
  python prepare_k_txt.py --fx 450 --fy 450 --cx 320 --cy 240 --baseline 0.06 --output K.txt

  # 从 cali.yml 生成到 demo_data 目录 (用于 FFS Python 验证)
  python prepare_k_txt.py --cali config/cali.yml --output ../Fast-FoundationStereo-master/demo_data/K.txt
"""

import argparse
import os
import sys
import yaml


def load_from_cali_yml(path: str) -> dict:
    """从 stereo_scanner 的 cali.yml 读取内参"""
    # OpenCV FileStorage 保存的 YAML 格式
    # 注意: OpenCV YAML 的矩阵格式与标准 YAML 不同
    # 需要用 OpenCV 读取, 这里用简单解析方式

    result = {}
    try:
        import cv2
        fs = cv2.FileStorage(path, cv2.FILE_STORAGE_READ)
        if not fs.isOpened():
            raise IOError(f"Cannot open {path}")

        M1 = fs.getNode("M1").mat()
        baseline = fs.getNode("baseline_m").real()

        result['fx'] = M1[0, 0]
        result['fy'] = M1[1, 1]
        result['cx'] = M1[0, 2]
        result['cy'] = M1[1, 2]
        result['baseline'] = baseline

        fs.release()
    except ImportError:
        # 没有 OpenCV, 尝试手动解析
        with open(path, 'r') as f:
            content = f.read()

        # OpenCV YAML 格式: 矩阵以 rows:cols:type:data 格式存储
        # 这比较复杂, 提供一个简化的解析
        import re

        # 尝试解析 baseline_m
        baseline_match = re.search(r'baseline_m:\s*([\d.]+)', content)
        if baseline_match:
            result['baseline'] = float(baseline_match.group(1))

        # M1 矩阵的解析比较困难, 建议安装 OpenCV
        if 'fx' not in result:
            print("Warning: Cannot parse M1 matrix without OpenCV. "
                  "Please provide fx/fy/cx/cy manually.")
            return None

    return result


def write_k_txt(output_path: str, fx: float, fy: float,
                cx: float, cy: float, baseline: float):
    """写入 FFS K.txt 格式"""
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)

    with open(output_path, 'w') as f:
        # 第1行: 3x3 内参矩阵展平
        f.write(f"{fx} 0.0 {cx} 0.0 {fy} {cy} 0.0 0.0 1.0\n")
        # 第2行: baseline (米)
        f.write(f"{baseline}\n")

    print(f"K.txt written to: {output_path}")
    print(f"  fx={fx}, fy={fy}, cx={cx}, cy={cy}")
    print(f"  baseline={baseline}m ({baseline*1000}mm)")


def main():
    parser = argparse.ArgumentParser(description='Generate K.txt for Fast-FoundationStereo')
    parser.add_argument('--cali', type=str, default='',
                        help='标定文件路径 (cali.yml)')
    parser.add_argument('--fx', type=float, default=0, help='焦距 fx (像素)')
    parser.add_argument('--fy', type=float, default=0, help='焦距 fy (像素)')
    parser.add_argument('--cx', type=float, default=0, help='主点 cx (像素)')
    parser.add_argument('--cy', type=float, default=0, help='主点 cy (像素)')
    parser.add_argument('--baseline', type=float, default=0, help='基线距离 (米)')
    parser.add_argument('--output', type=str, default='config/K.txt',
                        help='K.txt 输出路径')
    parser.add_argument('--image_width', type=int, default=640,
                        help='图像宽度 (用于默认主点计算)')
    parser.add_argument('--image_height', type=int, default=480,
                        help='图像高度 (用于默认主点计算)')
    args = parser.parse_args()

    fx, fy, cx, cy, baseline = args.fx, args.fy, args.cx, args.cy, args.baseline

    if args.cali:
        # 从标定文件读取
        params = load_from_cali_yml(args.cali)
        if params:
            fx = params.get('fx', fx)
            fy = params.get('fy', fy)
            cx = params.get('cx', cx)
            cy = params.get('cy', cy)
            baseline = params.get('baseline', baseline)
            print(f"Loaded from {args.cali}:")
        else:
            print("Failed to load cali.yml, using manual parameters")

    # 验证参数
    if fx <= 0:
        print("Error: fx must be > 0. Provide --cali or --fx manually.")
        sys.exit(1)
    if baseline <= 0:
        print("Error: baseline must be > 0. Provide --cali or --baseline manually.")
        sys.exit(1)

    # 默认主点 = 图像中心
    if fy <= 0:
        fy = fx  # 通常 fx ≈ fy
    if cx <= 0:
        cx = args.image_width / 2.0
    if cy <= 0:
        cy = args.image_height / 2.0

    write_k_txt(args.output, fx, fy, cx, cy, baseline)


if __name__ == '__main__':
    main()
