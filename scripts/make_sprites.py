"""从 AI 生成的角色合集图切出 32x32 像素精灵，并拼带中文标注的索引图。

合集图背景近白、角色互不重叠，所以用投影找零区当分隔即可，不用分割模型。
每格取最大的连通块作为人物本体，框外的火焰碎片、投影、水印都不进画面；
透明通道按轮廓生成：轮廓外且与画布边缘连通的像素透明，被深色描边围住的
浅色衣服和脸部亮部保持不透明。

用法：py -3 scripts/make_sprites.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent / "assets" / "sprites" / "characters"
OUT = ROOT / "individual"
CELL = 32
# 掩膜阈值要低到能容下肤色：这些角色脸色很浅，阈值一高脸就被当成背景抠平。
SOFT = 12
PALETTE = 32
ZOOM = 5
FONT_CANDIDATES = (
    "C:/Windows/Fonts/msyh.ttc",
    "C:/Windows/Fonts/simhei.ttf",
    "/System/Library/Fonts/PingFang.ttc",
)

GROUPS = [
    {
        "title": "九境修士",
        "parts": [
            {
                "sheet": "preview-realms_001.jpg",
                "grid": (3, 3),
                "names": [
                    ("realm1_yinqi", "引气"),
                    ("realm2_lianqi", "炼气"),
                    ("realm3_zhuji", "筑基"),
                    ("realm4_jindan", "金丹"),
                    ("realm5_yuanying", "元婴"),
                    ("realm6_huashen", "化神"),
                    ("realm7_lianxu", "炼虚"),
                    ("realm8_heti", "合体"),
                    None,
                ],
            },
            {
                "sheet": "preview-dacheng_001.jpg",
                "grid": (1, 1),
                "names": [("realm9_dacheng", "大乘")],
            },
        ],
    },
    {
        "title": "宗门职位",
        "parts": [
            {
                "sheet": "preview-offices_001.jpg",
                "grid": (3, 3),
                "trim_bottom_artifact": True,
                "names": [
                    ("office1_sect_master", "宗主"),
                    ("office2_elder", "长老"),
                    ("office3_inner_m", "内门弟子"),
                    ("office4_inner_f", "内门弟子"),
                    ("office5_inner_f2", "内门弟子"),
                    ("office6_outer_m", "外门弟子"),
                    ("office7_outer_m2", "外门弟子"),
                    ("office8_outer_f", "外门弟子"),
                    ("office9_rogue", "散修"),
                ],
            }
        ],
    },
    {
        "title": "四修流派",
        "parts": [
            {
                "sheet": "preview-paths_001.jpg",
                "grid": (1, 4),
                "names": [
                    ("path_sword", "剑修"),
                    ("path_alchemy", "丹修"),
                    ("path_talisman", "符修"),
                    ("path_body", "体修"),
                ],
            }
        ],
    },
]


def mask_of(img: Image.Image, tol: int) -> Image.Image:
    """与白色的灰度差超过 tol 的像素记为内容。"""
    white = Image.new("RGB", img.size, (255, 255, 255))
    diff = ImageChops.difference(img, white).convert("L")
    return diff.point(lambda v: 255 if v > tol else 0)


def split_axis(profile: list[int], n: int) -> list[tuple[int, int]]:
    """把投影切成 n 段。

    切点取内容范围内最宽的 n-1 段零区。范围限定在有内容的像素上：画布
    四周的大片留白同样是零区，放进来会把两三个人并进同一格。
    """
    m = len(profile)
    nz = [i for i, v in enumerate(profile) if v]
    if not nz:
        return [(0, m)]
    lo, hi = nz[0], nz[-1] + 1
    if n <= 1:
        return [(0, m)]

    runs: list[tuple[int, int]] = []
    start: int | None = None
    for i in range(lo, hi):
        if profile[i] == 0:
            if start is None:
                start = i
        elif start is not None:
            runs.append((start, i))
            start = None
    if start is not None:
        runs.append((start, hi))

    runs.sort(key=lambda r: r[1] - r[0], reverse=True)
    cuts: list[int] = []
    for a, b in runs:
        if len(cuts) >= n - 1:
            break
        mid = (a + b) // 2
        if all(abs(mid - c) > 1 for c in cuts):
            cuts.append(mid)
    if len(cuts) < n - 1:
        raise SystemExit(f"投影里找不到 {n - 1} 个分隔")

    cuts.sort()
    bounds = [0, *cuts, m]
    return list(zip(bounds[:-1], bounds[1:]))


def trim_bottom_artifact(img: Image.Image) -> Image.Image:
    """切掉画面底部独立成簇的文字水印，只留角色脚下那一簇。"""
    h = img.size[1]
    zone_top = int(h * 0.75)
    mask = mask_of(img, 30)
    px = mask.load()
    profile = [
        sum(1 for x in range(mask.size[0]) if px[x, y])
        for y in range(mask.size[1])
    ]

    bands: list[tuple[int, int]] = []
    start: int | None = None
    for y in range(zone_top, h):
        if profile[y]:
            if start is None:
                start = y
        elif start is not None:
            bands.append((start, y))
            start = None
    if start is not None:
        bands.append((start, h))
    if len(bands) < 2:
        return img
    return img.crop((0, 0, img.size[0], bands[0][1]))


def largest_component_mask(mask: Image.Image) -> Image.Image | None:
    """返回最大连通块的掩膜（0/255）。

    角色旁边的火焰碎片、地上的投影、角落的水印都是别的连通块，靠它剔掉。
    """
    w, h = mask.size
    px = mask.load()
    seen = bytearray(w * h)
    best_pixels: list[int] | None = None

    for y in range(h):
        base = y * w
        for x in range(w):
            if not px[x, y] or seen[base + x]:
                continue
            stack = [(x, y)]
            seen[base + x] = 1
            pixels: list[int] = []
            while stack:
                cx, cy = stack.pop()
                pixels.append(cy * w + cx)
                for nx, ny in (
                    (cx - 1, cy),
                    (cx + 1, cy),
                    (cx, cy - 1),
                    (cx, cy + 1),
                ):
                    if (
                        0 <= nx < w
                        and 0 <= ny < h
                        and px[nx, ny]
                        and not seen[ny * w + nx]
                    ):
                        seen[ny * w + nx] = 1
                        stack.append((nx, ny))
            if best_pixels is None or len(pixels) > len(best_pixels):
                best_pixels = pixels

    if best_pixels is None:
        return None
    out = Image.new("L", mask.size, 0)
    op = out.load()
    for idx in best_pixels:
        op[idx % w, idx // w] = 255
    return out


def cut_shadow(art: Image.Image) -> Image.Image:
    """切掉角色脚下那圈浅灰投影。

    以最后一个还有深色像素（鞋、衣褶）的行为界，往下全是投影。
    """
    w, h = art.size
    px = art.load()
    last_dark = 0
    for y in range(h):
        dark = sum(1 for x in range(w) if sum(px[x, y][:3]) / 3 < 140)
        if dark >= max(2, w * 0.02):
            last_dark = y
    if 0 < last_dark < h - 1:
        return art.crop((0, 0, w, last_dark + 1))
    return art


def silhouette_alpha(mask: Image.Image) -> Image.Image:
    """由人物轮廓生成 alpha 通道。

    mask 里 255 是人物、0 是其它。轮廓外且与画布边缘连通的像素透明；被
    人物围住的 0 像素（浅色衣服、脸部亮部）保持不透明。直接按亮度抠白色
    会把浅色脸和衣服一起抠掉，所以只认连通性。
    """
    w, h = mask.size
    px = mask.load()
    seen = bytearray(w * h)
    stack: list[tuple[int, int]] = []
    for x in range(w):
        stack += [(x, 0), (x, h - 1)]
    for y in range(h):
        stack += [(0, y), (w - 1, y)]

    while stack:
        x, y = stack.pop()
        if not (0 <= x < w and 0 <= y < h) or seen[y * w + x]:
            continue
        if px[x, y]:
            continue
        seen[y * w + x] = 1
        stack += [(x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)]

    alpha = Image.new("L", (w, h), 255)
    ap = alpha.load()
    for y in range(h):
        for x in range(w):
            if seen[y * w + x]:
                ap[x, y] = 0
    return alpha


def make_sprite(img: Image.Image, cell: tuple[int, int, int, int]) -> Image.Image | None:
    crop = img.crop(cell)
    comp = largest_component_mask(mask_of(crop, SOFT))
    if comp is None:
        return None
    bbox = comp.getbbox()
    if bbox is None:
        return None
    art = cut_shadow(crop.crop(bbox))
    comp = comp.crop(bbox)

    scale = min((CELL - 2) / art.width, (CELL - 2) / art.height)
    w = max(1, round(art.width * scale))
    h = max(1, round(art.height * scale))
    small = art.resize((w, h), Image.LANCZOS)
    small_comp = comp.resize((w, h), Image.NEAREST)

    canvas = Image.new("L", (CELL, CELL), 0)
    canvas.paste(small_comp, ((CELL - w) // 2, (CELL - h) // 2))
    alpha = silhouette_alpha(canvas)

    full = Image.new("RGBA", (CELL, CELL), (255, 255, 255, 0))
    full.paste(small.convert("RGBA"), ((CELL - w) // 2, (CELL - h) // 2), small_comp)
    full.putalpha(alpha)

    quantized = full.convert("RGB").quantize(colors=PALETTE, method=Image.MEDIANCUT)
    out = quantized.convert("RGBA")
    out.putalpha(alpha)
    return out


def cells_of(img: Image.Image, nrows: int, ncols: int) -> list[tuple[int, int, int, int]]:
    mask = mask_of(img, 30)
    w, h = mask.size
    px = mask.load()
    rows = [0] * h
    cols = [0] * w
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                rows[y] += 1
                cols[x] += 1

    row_bands = split_axis(rows, nrows)
    col_bands = split_axis(cols, ncols)
    return [(x0, y0, x1, y1) for (y0, y1) in row_bands for (x0, x1) in col_bands]


def load_font(size: int) -> ImageFont.ImageFont:
    for path in FONT_CANDIDATES:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def build_index(
    groups: list[tuple[str, list[tuple[str, str, Image.Image]]]]
) -> Image.Image:
    """把所有精灵拼成一张带中文标注的总索引图。"""
    font = load_font(15)
    title_font = load_font(20)
    pad = 12
    label_h = 24
    title_h = 30
    per_row = 4

    width = pad + per_row * (CELL * ZOOM + pad)
    height = pad
    for _, sprites in groups:
        height += title_h + (CELL * ZOOM + label_h) * (
            (len(sprites) + per_row - 1) // per_row
        ) + pad

    sheet = Image.new("RGB", (width, height), (248, 248, 250))
    draw = ImageDraw.Draw(sheet)

    y = pad
    for title, sprites in groups:
        draw.text((pad, y), title, font=title_font, fill=(30, 30, 40))
        y += title_h
        for i, (_, label, sprite) in enumerate(sprites):
            x = pad + (i % per_row) * (CELL * ZOOM + pad)
            yy = y + (i // per_row) * (CELL * ZOOM + label_h)
            big = sprite.resize((CELL * ZOOM, CELL * ZOOM), Image.NEAREST)
            sheet.paste(big.convert("RGB"), (x, yy), big)
            draw.rectangle(
                [x, yy, x + CELL * ZOOM - 1, yy + CELL * ZOOM - 1],
                outline=(208, 208, 214),
            )
            draw.text((x, yy + CELL * ZOOM + 3), label, font=font, fill=(70, 70, 80))
        y += (CELL * ZOOM + label_h) * (
            (len(sprites) + per_row - 1) // per_row
        ) + pad
    return sheet


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    groups: list[tuple[str, list[tuple[str, str, Image.Image]]]] = []

    for group in GROUPS:
        sprites: list[tuple[str, str, Image.Image]] = []
        for part in group["parts"]:
            img = Image.open(ROOT / part["sheet"]).convert("RGB")
            if part.get("trim_bottom_artifact"):
                img = trim_bottom_artifact(img)
                img.save(ROOT / part["sheet"], quality=95)
            cells = cells_of(img, *part["grid"])
            if len(cells) != len(part["names"]):
                raise SystemExit(
                    f"{part['sheet']} 切出 {len(cells)} 格，"
                    f"名字有 {len(part['names'])} 个"
                )
            for cell, name in zip(cells, part["names"]):
                if name is None:
                    continue
                slug, label = name
                sprite = make_sprite(img, cell)
                if sprite is None:
                    raise SystemExit(f"{slug} 没切出内容")
                sprite.save(OUT / f"{slug}.png")
                sprites.append((slug, label, sprite))
                print(f"{slug}.png  {label}")
        groups.append((group["title"], sprites))

    index = build_index(groups)
    index.save(ROOT / "index.png")
    print(f"index.png  {index.size}")


if __name__ == "__main__":
    main()
