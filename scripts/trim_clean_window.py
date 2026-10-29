"""把 H3 源视频裁到「背景没漂移」的连续帧段。

H3 的 FL2VA 会把首尾帧钉在参考图上，漂移只发生在中段：背景从键色漂成别的
颜色，角色也跟着变成另一个人。抠像只认键色，漂掉的段要么被整段丢弃，要么
被错误抠图，所以直接裁掉。

判定用背景色：逐帧取边缘主色，和首帧的边缘主色比欧氏距离，超出阈值算漂移。
背景变色和角色变形几乎同时发生，所以它也能当「角色还是不是参考图」的代理。

用法：py -3 scripts/trim_clean_window.py <视频> [输出]
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

DRIFT_TOL = 30.0
FPS = 24  # H3 768P 输出是 24fps
MIN_RUN = 24  # 一个步态周期至少要这么多帧才够抽


def border_color(arr: np.ndarray) -> np.ndarray:
    edge = np.concatenate(
        [arr[0, :, :], arr[-1, :, :], arr[:, 0, :], arr[:, -1, :]]
    )
    bg = Counter(map(tuple, edge.tolist())).most_common(1)[0][0]
    return np.array(bg, dtype=np.float32)


def clean_runs(colors: list[np.ndarray]) -> list[tuple[int, int]]:
    """所有连续的「背景未漂移」帧段。"""
    ref = colors[0]
    clean = [bool(np.linalg.norm(c - ref) <= DRIFT_TOL) for c in colors]
    runs = []
    start = None
    for i, ok in enumerate(clean + [False]):
        if ok and start is None:
            start = i
        elif not ok and start is not None:
            runs.append((start, i))
            start = None
    return runs


def motion_of(masks: list[np.ndarray], run: tuple[int, int]) -> int:
    """帧段内的累计运动量：相邻帧的前景像素变化数之和。"""
    start, end = run
    total = 0
    for i in range(start + 1, end):
        total += int(np.logical_xor(masks[i], masks[i - 1]).sum())
    return total


def pick_run(colors: list[np.ndarray], masks: list[np.ndarray]) -> tuple[int, int]:
    """在干净帧段里挑运动量最大的那段。

    漂移前后往往各留一小段静止的「参考图」，帧数多但没有动画；
    真正带循环的那段帧数不多却有明显运动，所以按运动量挑而不是按长度。
    但一段至少要够放下一个完整步态周期，太短的段直接判不可用。
    """
    runs = clean_runs(colors)
    if not runs:
        raise SystemExit("没有找到背景未漂移的帧段")
    long_runs = [r for r in runs if r[1] - r[0] >= MIN_RUN]
    if long_runs:
        return max(long_runs, key=lambda r: (motion_of(masks, r), r[1] - r[0]))
    return max(runs, key=lambda r: r[1] - r[0])


def trim(src: Path, dst: Path) -> tuple[int, int]:
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-i", str(src), "-vsync", "0", str(tmpdir / "f%05d.png")],
            check=True,
        )
        frames = sorted(tmpdir.glob("*.png"))
        key = np.array([255, 0, 255], dtype=np.int16)
        colors, masks = [], []
        for f in frames:
            arr = np.asarray(Image.open(f).convert("RGB")).astype(np.int16)
            colors.append(border_color(arr.astype(np.float32)))
            masks.append(np.abs(arr - key).max(axis=2) > 60)

        start, end = pick_run(colors, masks)
        total = len(frames)
        print(f"{src.name}: 共 {total} 帧，干净窗口 [{start}, {end}) "
              f"（{end - start} 帧，运动量 {motion_of(masks, (start, end))}）")
        if end - start < 4:
            raise SystemExit(f"干净窗口太短，无法使用：{src}")

        dst.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-framerate", str(FPS), "-start_number", "0",
             "-i", str(tmpdir / "f%05d.png"),
             "-vf", f"select='between(n,{start},{end - 1})'",
             "-vsync", "0", "-frames:v", str(end - start),
             "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p",
             str(dst)],
            check=True,
        )
        return start, end


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("用法：py -3 scripts/trim_clean_window.py <视频> [输出]")
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_name(
        src.stem + "_clean.mp4")
    trim(src, dst)
    print("->", dst)


if __name__ == "__main__":
    main()
