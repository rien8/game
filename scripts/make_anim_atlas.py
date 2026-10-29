"""打包宗主精灵图的 idle + walk 两段动作为单张 atlas。

参数与规格卡一致：cell 32x32、pivot bottom-center、facing front、A 游戏关键帧
（idle 4 帧 / walk 6 帧，12fps）、透明背景、纹理上限 4096。

pad/extrude 取 1：脚本要求 pad >= extrude，且内部还要再留 2px 安全边，
所以 32 的 cell 下角色最高只能到 26px，比现有静态精灵的 30px 略矮。

源视频先裁到主体并集包围盒——裁剪不丢任何像素（原包围盒即并集），
去掉的是四周品红留白，否则并集画布会被留白撑大、角色被压小。

两段视频必须用同一个裁剪框：打包脚本的 fixed 对齐要求所有动作共享
同一画布尺寸，各自裁剪会报 SOURCE_CANVAS_MISMATCH。

用法：py -3 scripts/make_anim_atlas.py
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ANIM = ROOT / "assets" / "sprites" / "characters" / "anim"
SKILL_SCRIPT = Path(
    "C:/Users/lenovo/.minimax/v2/plugin-cache/official"
    "/sha256-tree-v1-58129ce33a6ed76200e511b6c25ba0ae80a478b2930e9acdf5db2611333935ba"
    "/skills/h3-sprite-sheet/scripts/make_atlas.py"
)
MARGIN = 8  # 裁剪时给主体四周留的余量

# (动作名, 帧数)。帧数必须显式给：脚本默认的 8 帧会让短周期动作的抽帧挤在
# 快速相位上，直接把 playback_qc 判成 FAIL。
MOVES = [
    ("idle", 4),
    ("walk", 6),
]


def border_color(arr: np.ndarray) -> tuple[int, int, int]:
    edge = np.concatenate(
        [arr[0, :, :], arr[-1, :, :], arr[:, 0, :], arr[:, -1, :]]
    )
    counter = Counter(map(tuple, edge.tolist()))
    return counter.most_common(1)[0][0]


def subject_bbox(clip: Path) -> tuple[tuple[int, int, int, int], int, int]:
    """主体在画面里的并集包围盒，以及画面尺寸。"""
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-i", str(clip), "-vsync", "0", str(tmpdir / "f%05d.png")],
            check=True,
        )
        minx = miny = 10**9
        maxx = maxy = -1
        w = h = 0
        for f in sorted(tmpdir.glob("*.png")):
            arr = np.asarray(Image.open(f).convert("RGB")).astype(np.int16)
            h, w = arr.shape[:2]
            bg = np.array(border_color(arr), dtype=np.int16)
            dist = np.abs(arr - bg).max(axis=2)
            ys, xs = np.nonzero(dist > 60)
            if len(xs) == 0:
                continue
            minx, maxx = min(minx, xs.min()), max(maxx, xs.max())
            miny, maxy = min(miny, ys.min()), max(maxy, ys.max())
    return (minx, miny, maxx, maxy), w, h


def union_bbox(clips: list[Path]) -> tuple[tuple[int, int, int, int], int, int]:
    """多段视频主体的联合包围盒，保证裁剪后画布一致。"""
    minx = miny = 10**9
    maxx = maxy = -1
    w = h = 0
    for clip in clips:
        (x0, y0, x1, y1), cw, ch = subject_bbox(clip)
        minx, maxx = min(minx, x0), max(maxx, x1)
        miny, maxy = min(miny, y0), max(maxy, y1)
        w, h = cw, ch
    return (minx, miny, maxx, maxy), (w, h)


def crop_clip(clip: Path, dst: Path, box: tuple[int, int, int, int],
              size: tuple[int, int]) -> None:
    x0, y0, x1, y1 = box
    w, h = size
    x0 = max(0, x0 - MARGIN)
    y0 = max(0, y0 - MARGIN)
    x1 = min(w - 1, x1 + MARGIN)
    y1 = min(h - 1, y1 + MARGIN)
    cw = (x1 - x0 + 1) // 2 * 2
    ch = (y1 - y0 + 1) // 2 * 2
    subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
         "-i", str(clip),
         "-vf", f"crop={cw}:{ch}:{x0}:{y0}",
         "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p", str(dst)],
        check=True,
    )
    print(f"裁剪 {clip.name} -> {dst.name}  crop={cw}x{ch}+{x0}+{y0}")


def build(out_dir: Path, clips: dict[str, Path],
          frames: dict[str, int]) -> None:
    args = [sys.executable, str(SKILL_SCRIPT), "--out", str(out_dir)]
    for name, clip in clips.items():
        spec = f"{name}={clip}"
        if name in frames:
            spec += f":{frames[name]}"
        args.append(spec)
    args += [
        "--fps-suggest", "12",
        "--cell-size", "32",
        "--asset-kind", "character",
        "--pivot", "bottom-center",
        "--facing", "front",
        "--max-texture-size", "4096",
        "--pad", "1",
        "--extrude", "1",
        "--edge-erode", "1",
        "--loop", "idle,walk",
        "--quality-profile", "game-keyframes",
        "--background-mode", "transparent",
        "--pixelate", "32",
        "--colors", "32",
    ]
    subprocess.run(args, check=True)


def main() -> None:
    if not SKILL_SCRIPT.exists():
        raise SystemExit(f"找不到打包脚本：{SKILL_SCRIPT}")
    if len(sys.argv) < 2:
        raise SystemExit("用法：py -3 scripts/make_anim_atlas.py <角色名> [...]")

    for char in sys.argv[1:]:
        base = ANIM / char
        videos = base / "videos"
        srcs = [videos / f"{move}.mp4" for move, _ in MOVES]
        for src in srcs:
            if not src.exists():
                raise SystemExit(f"缺少源视频：{src}")

        box, size = union_bbox(srcs)
        clips: dict[str, Path] = {}
        for move, _ in MOVES:
            dst = videos / f"{move}_裁剪.mp4"
            crop_clip(videos / f"{move}.mp4", dst, box, size)
            clips[move] = dst

        out_dir = base / "atlas"
        if out_dir.exists():
            shutil.rmtree(out_dir)
        build(out_dir, clips, dict(MOVES))
        print("atlas ->", out_dir)


if __name__ == "__main__":
    main()
