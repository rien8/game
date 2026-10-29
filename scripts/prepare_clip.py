"""把 H3 源视频整理成可打包的片段：背景归一化 + 截取角色外形稳定的窗口。

两件事：

1. H3 常把纯色背景换成别的颜色（品红 → 粉 → 白 → 青）。角色本身没变，
   只是底色变了，用 normalize_key_bg.py 把每帧底色归回品红即可，零成本。
2. 少数情况下 H3 会把角色整个重画（外形、发型都变了）。这时按外形与
   首帧的差异找出稳定窗口，只取稳定的那一段。

用法：py -3 scripts/prepare_clip.py <源视频> <输出>
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

from normalize_key_bg import KEY, normalize

FPS = 24
MIN_RUN = 24        # 一个步态周期至少要这么多帧才够抽
MAX_SHAPE_DRIFT = 0.25  # 外形与首帧的差异超过主体面积这个比例就算重画
KEY_DIST = 60       # 与键色的最大通道差超过它算主体


def silhouettes(frames: list[Path]) -> list[np.ndarray]:
    out = []
    for f in frames:
        arr = np.asarray(Image.open(f).convert("RGB")).astype(np.int16)
        out.append(np.abs(arr - np.array(KEY, dtype=np.int16)).max(axis=2) > KEY_DIST)
    return out


def stable_runs(masks: list[np.ndarray]) -> list[tuple[int, int]]:
    ref = masks[0]
    size = max(int(ref.sum()), 1)
    limit = MAX_SHAPE_DRIFT * size
    ok = [int(np.logical_xor(m, ref).sum()) <= limit for m in masks]
    runs = []
    start = None
    for i, good in enumerate(ok + [False]):
        if good and start is None:
            start = i
        elif not good and start is not None:
            runs.append((start, i))
            start = None
    return runs


def motion_of(masks: list[np.ndarray], run: tuple[int, int]) -> int:
    start, end = run
    return sum(int(np.logical_xor(masks[i], masks[i - 1]).sum())
               for i in range(start + 1, end))


def remove_ground_bar(arr: np.ndarray) -> np.ndarray:
    """抹掉 H3 加在脚下的地面横条。

    H3 有时会在角色脚下画一条比身体更宽的实心横条当地面。特征很固定：
    紧贴画面最底部的连续若干行，宽度明显超过下半身的中位宽度。角色本体
    不会这样——袍摆是收窄的，不会突然比腰还宽。
    """
    h, w = arr.shape[:2]
    mask = np.abs(arr - np.array(KEY, dtype=np.int16)).max(axis=2) > KEY_DIST
    widths = np.zeros(h, dtype=np.int32)
    for y in range(h):
        xs = np.nonzero(mask[y])[0]
        if len(xs):
            widths[y] = xs.max() - xs.min() + 1
    rows = np.nonzero(widths)[0]
    if len(rows) == 0:
        return arr

    bottom = rows.max()
    lo = max(0, int(h * 0.7))
    sample = widths[lo:bottom + 1]
    sample = sample[sample > 0]
    if len(sample) == 0:
        return arr
    median = float(np.median(sample))

    y = bottom
    cut = bottom
    while y >= lo and widths[y] >= 1.3 * median:
        cut = y
        y -= 1
    if cut == bottom:
        return arr
    # 地面条下面必须没有内容，保证它确实压在画面最底部
    if widths[bottom + 1:].any():
        return arr
    arr = arr.copy()
    arr[cut:bottom + 1] = np.array(KEY, dtype=np.int16)
    return arr


def trim_frozen_ends(masks: list[np.ndarray]) -> tuple[int, int]:
    """切掉开头结尾的冻结段，返回要保留的帧区间 [head, tail]。

    H3 的 FL2VA 会把首尾帧钉在参考图上，于是每段视频都是
    「冻结的参考姿势 → 动作 → 冻结的参考姿势」。中间的冻结段对导出毫无
    作用，还会让循环的首尾接缝跳变。判断用累计运动量：动作开始前累计量
    一直是 0，动作结束后也不再增长。
    """
    if len(masks) < 4:
        return 0, len(masks) - 1
    size = max(int(masks[0].sum()), 1)
    d = np.array([int(np.logical_xor(masks[i], masks[i - 1]).sum()) / size
                  for i in range(1, len(masks))])
    total = d.sum()
    if total <= 0:
        return 0, len(masks) - 1
    cum = np.cumsum(d)
    head = int(np.searchsorted(cum, 0.01 * total)) + 1
    tail = int(np.searchsorted(cum, 0.99 * total))
    if tail <= head:
        return 0, len(masks) - 1
    return head, tail


def prepare(src: Path, dst: Path) -> tuple[int, int]:
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        raw = tmpdir / "raw"
        raw.mkdir()
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-i", str(src), "-vsync", "0", str(raw / "f%05d.png")],
            check=True,
        )
        frames = sorted(raw.glob("*.png"))

        # 背景归一化：每帧按自己的底色刷回品红，再抹掉脚下地面条
        norm = tmpdir / "norm"
        norm.mkdir()
        for f in frames:
            img = normalize(Image.open(f))
            arr = remove_ground_bar(np.asarray(img).astype(np.int16))
            Image.fromarray(arr.astype(np.uint8), "RGB").save(norm / f.name)

        masks = silhouettes(sorted(norm.glob("*.png")))
        head, tail = trim_frozen_ends(masks)
        print(f"{src.name}: {len(frames)} 帧，冻结段 [{0}, {head}) 与 "
              f"({tail}, {len(frames)}] 已切除，保留 [{head}, {tail}]")

        dst.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-framerate", str(FPS), "-start_number", "0",
             "-i", str(norm / "f%05d.png"),
             "-vf", f"select='between(n,{head},{tail})'",
             "-vsync", "0", "-frames:v", str(tail - head),
             "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p",
             str(dst)],
            check=True,
        )
        return head, tail


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("用法：py -3 scripts/prepare_clip.py <源视频> <输出>")
    prepare(Path(sys.argv[1]), Path(sys.argv[2]))


if __name__ == "__main__":
    main()
