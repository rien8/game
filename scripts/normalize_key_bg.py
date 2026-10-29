"""把背景漂移的帧重新归一到品红键色。

H3 偶尔会把纯色背景换成另一种颜色（walk 段就从品红漂成了绿色）。画面里的
主体是干净的，只有底色变了，所以不必重生成整段视频：按每帧的实际底色做
距离抠像，再把主体重新合成回统一品红底。

底色判定用「与底色的最大通道差」而不是连通域：角色的 jade 吊坠本身偏绿，
连通域漫灌能护住它，但脚下被袍角围住的口袋区漫灌进不去，会残留绿斑。
实测吊坠与绿底的最小距离是 61，而背景与背景边缘的混合像素都在 30 以内，
所以阈值卡在 30 既能清掉口袋区，又碰不到吊坠。

用法：py -3 scripts/normalize_key_bg.py <输入.mp4> <输出.mp4>
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

KEY = (255, 0, 255)
CUT = 30        # 与底色的最大通道差小于它算纯背景
SOFT_HI = 55    # 差值大于它算纯主体
BAND = 5        # 背景掩膜外扩的像素数，用于处理边缘混合


def border_color(arr: np.ndarray) -> tuple[int, int, int]:
    """取画布四边像素的中位色作为该帧底色。"""
    edge = np.concatenate(
        [arr[0, :, :], arr[-1, :, :], arr[:, 0, :], arr[:, -1, :]]
    )
    counter = Counter(map(tuple, edge.tolist()))
    return counter.most_common(1)[0][0]


def background_mask(img: Image.Image, bg: tuple[int, int, int]) -> Image.Image:
    """与底色足够接近的像素记为背景（0/255 掩膜）。"""
    arr = np.asarray(img.convert("RGB")).astype(np.int16)
    dist = np.abs(arr - np.array(bg, dtype=np.int16)).max(axis=2)
    return Image.fromarray((dist <= CUT).astype(np.uint8) * 255, "L")


def normalize(img: Image.Image) -> Image.Image:
    arr = np.asarray(img.convert("RGB")).astype(np.int16)
    bg = border_color(arr)
    mask = np.asarray(background_mask(img.convert("RGB"), bg)).astype(bool)

    # 背景边缘的混合像素：按与底色的距离算不透明度
    dist = np.abs(arr - np.array(bg, dtype=np.int16)).max(axis=2)
    alpha = np.clip((dist - CUT) / (SOFT_HI - CUT), 0.0, 1.0)
    alpha = np.where(mask, 0.0, alpha)

    # 背景掩膜往外扩一圈，让边缘混合像素也参与；掩膜之外的像素保持不透明，
    # 角色内部与绿底同色但距离更远的 jade 吊坠因此不受影响
    band = np.asarray(
        Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(BAND))
    ).astype(bool)
    alpha = np.where(band, alpha, 1.0)

    out = arr * alpha[..., None] + np.array(KEY, dtype=np.int16) * (1 - alpha[..., None])
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGB")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-i", str(src), "-vsync", "0", str(tmpdir / "f%04d.png")],
            check=True,
        )
        frames = sorted(tmpdir.glob("*.png"))
        for i, f in enumerate(frames):
            normalize(Image.open(f)).save(f)
            if (i + 1) % 40 == 0:
                print(f"  {i + 1}/{len(frames)}")
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-framerate", "24", "-i", str(tmpdir / "f%04d.png"),
             "-c:v", "libx264", "-crf", "16", "-pix_fmt", "yuv420p", str(dst)],
            check=True,
        )
    print("归一化完成:", dst)


if __name__ == "__main__":
    main()
