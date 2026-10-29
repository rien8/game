"""把 32×32 角色精灵合成成 512×512 键色定妆图，供 H3 当首帧参考。

H3 要的输入是「单主体 + 可分离的纯色背景」，所以精灵的透明区必须变成键色
而不是透明。最近邻放大 16 倍铺满 512×512，再叠到纯品红底上。

用法：py -3 scripts/make_key_image.py <角色名> [<角色名> ...]
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "sprites" / "characters" / "individual"
OUT = ROOT / "assets" / "sprites" / "characters" / "anim"
KEY_RGB = (255, 0, 255)
CANVAS = 512

# 与主体最近颜色的距离必须大于这个值，否则抠像时主体边缘会被啃掉
MIN_KEY_DISTANCE = 120.0

# 精灵里画在角色背后的光环（特效）。动画帧按规格卡不带特效，而 H3 会把定妆图
# 里的东西照着画出来——实测炼气的白色光盘被原样渲染进了动画，把角色压得很小。
# 所以这些角色合成定妆图前先把光环剔掉。
HALO_RULES = {
    "realm2_lianqi": "green",   # 白盘 + 淡绿边
    "realm3_zhuji": "arc",      # 金色断环
    "realm6_huashen": "gold",   # 白盘 + 金环
}


def halo_mask(arr: np.ndarray, rule: str) -> np.ndarray:
    """标出光环像素。"""
    rgb = arr[..., :3].astype(np.int16)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = rgb.max(axis=2)
    mn = rgb.min(axis=2)
    if rule == "green":
        return ((g > r + 25) & (g > b + 25) & (g > 150)) | (mn > 200)
    if rule == "gold":
        return ((r > 230) & (g > 140) & (g < 195) & (b < 95)) | (mn > 200)
    if rule == "arc":
        return (r > 230) & (g > 200) & (b < 200) & (mn > 150)
    raise ValueError(rule)


def components(mask: np.ndarray) -> np.ndarray:
    """8 连通域标号。"""
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    cur = 0
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not lab[y, x]:
                cur += 1
                stack = [(y, x)]
                lab[y, x] = cur
                while stack:
                    cy, cx = stack.pop()
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < h and 0 <= nx < w and \
                                    mask[ny, nx] and not lab[ny, nx]:
                                lab[ny, nx] = cur
                                stack.append((ny, nx))
    return lab


def strip_halo(arr: np.ndarray, rule: str) -> np.ndarray:
    """剔掉角色背后的光环，保留角色自身。

    光环和角色的白/金色撞色，单靠颜色分不开。做法是：去掉光环色后取最大
    连通域当角色主体，再把「被主体包住」的光环色像素加回来——领口的白、金
    色的腰带被袍子围着，会被加回；贴着轮廓的白色光盘只挨着角色边缘，不会。
    """
    alpha = arr[..., 3] > 0
    halo = halo_mask(arr, rule)
    lab = components(alpha & ~halo)
    if lab.max() == 0:
        return arr
    sizes = [(int((lab == i).sum()), i) for i in range(1, lab.max() + 1)]
    core = lab == max(sizes)[1]

    neigh = np.zeros_like(core, dtype=np.int32)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            neigh += np.roll(np.roll(core, dy, 0), dx, 1).astype(np.int32)
    keep = core | (alpha & halo & (neigh >= 4))

    out = arr.copy()
    out[~keep] = 0
    return out


def build(name: str) -> Path:
    src = SRC / f"{name}.png"
    if not src.exists():
        raise SystemExit(f"缺少源精灵：{src}")
    sprite = Image.open(src).convert("RGBA")
    if sprite.size != (32, 32):
        raise SystemExit(f"源精灵不是 32×32：{src} {sprite.size}")

    rule = HALO_RULES.get(name)
    if rule:
        arr = np.asarray(sprite)
        sprite = Image.fromarray(strip_halo(arr, rule), "RGBA")

    # 整张 32×32 精灵等比放大 16 倍铺满画布：角色保持原比例和位置，
    # 精灵自带的透明区自然透出键色。
    big = sprite.resize((CANVAS, CANVAS), Image.NEAREST)
    base = Image.new("RGBA", (CANVAS, CANVAS), KEY_RGB + (255,))
    base.alpha_composite(big)

    out = OUT / name / "key.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    base.save(out)
    verify(base, out)
    return out


def verify(img: Image.Image, out: Path) -> None:
    arr = np.asarray(img.convert("RGB")).astype(np.float32)
    key = np.array(KEY_RGB, dtype=np.float32)
    dist = np.sqrt(((arr - key) ** 2).sum(-1))

    corners = [tuple(arr[y, x]) for y, x in
               ((0, 0), (0, arr.shape[1] - 1),
                (arr.shape[0] - 1, 0), (arr.shape[0] - 1, arr.shape[1] - 1))]
    for c in corners:
        if tuple(c) != KEY_RGB:
            raise SystemExit(f"角不是键色：{out} {c}")

    subject = dist[dist > 1e-6]
    if subject.size == 0:
        raise SystemExit(f"主体为空：{out}")
    nearest = float(subject.min())
    if nearest < MIN_KEY_DISTANCE:
        raise SystemExit(
            f"主体里有颜色离键色太近（{nearest:.0f} < {MIN_KEY_DISTANCE:.0f}），"
            f"抠像会伤到它：{out}"
        )
    print(f"{out.name} <- {out.parent.name}  主体最近键色距离 {nearest:.0f}")


def main() -> None:
    names = sys.argv[1:]
    if not names:
        raise SystemExit("用法：py -3 scripts/make_key_image.py <角色名> [...]")
    for name in names:
        build(name)


if __name__ == "__main__":
    main()
