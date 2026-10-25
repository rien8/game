"""Remove the elliptical drop shadow baked into each sprite under
assets/parts_processed.

Algorithm (per PNG):
  1. Read the part's bbox from manifest.json. The bbox encloses the character.
  2. In the bbox's lower 70% (cy >= bbox.y + bbox.h * 0.30), scan every row
     across the bbox's horizontal extent and count "shadow-like" pixels:
         RGB >= 240 (near white) AND 0 < alpha < 80.
  3. Find the longest contiguous run of rows that all have at least
     min(row_count_threshold, ~80) shadow pixels. This run is the horizontal
     band of the elliptical drop shadow.
  4. Within that band, take the longest *consecutive* stretch of rows that
     still clear the threshold (CORE_THRESHOLDS, strictest first), and fit an
     ellipse to that stretch:
       cx = bbox center x (already constrained to the bbox extent)
       cy = run midpoint
       a_axis = horizontal extent of the run on its longest row
       b_axis = half the run height
  5. Erase every pixel inside that ellipse (alpha -> 0).

Usage:
    python scripts/strip_shadows.py            # process all parts in place
    python scripts/strip_shadows.py --dry-run  # report, don't modify

A safety backup of parts_processed is written to assets/parts_processed.bak
each time the script runs without --dry-run.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
ASSETS = REPO / "assets" / "parts_processed"
BACKUP = REPO / "temp" / "parts_processed.bak"

# Tunables — chosen by inspection of the 76 sprite PNGs (256x256, RGBA).
RGB_MIN = 240               # near-white = part of the shadow ellipse
ALPHA_MAX = 80              # shadow pixels have low alpha; character body is high
LOWER_HALF_START = 0.30     # search the bottom 70% of the bbox — shadows can sit anywhere below the upper third
ROW_DETECT_THRESHOLD = 40   # a row counts as "shadow band" if contiguous run >= this
MIN_RUN_ROWS = 4            # the row run must span at least this many rows
CORE_THRESHOLDS = (100, 80, 60, 40)  # try the strictest first, fall back if no ellipse found
ELLIPSE_PAD = 1.15          # grow the fitted ellipse slightly to catch anti-aliased fringe


def _row_max_contiguous(rgba, y: int, x_lo: int, x_hi: int) -> tuple[int, int]:
    """Return (longest_run, run_start) of shadow-like pixels on row y,
    scanning only [x_lo, x_hi) — the bbox's horizontal extent."""
    longest = 0
    cur = 0
    cur_start = x_lo
    best_start = x_lo
    for x in range(x_lo, x_hi):
        r, g, b, a = rgba[x, y]
        if r >= RGB_MIN and g >= RGB_MIN and b >= RGB_MIN and 0 < a < ALPHA_MAX:
            if cur == 0:
                cur_start = x
            cur += 1
            if cur > longest:
                longest = cur
                best_start = cur_start
        else:
            cur = 0
    return longest, best_start


def _longest_core_run(rows: list[tuple[int, int, int]], start: int,
                      end: int, threshold: int) -> list[tuple[int, int, int]]:
    """Longest run of *consecutive* rows within [start, end] whose contiguous
    run is >= threshold.

    Consecutiveness matters: b_axis and cy are derived from the first and last
    row of the result, so a gap between two qualifying rows would silently
    inflate both. The caller then erases a taller ellipse than the shadow it
    found, clipping whatever sits between them (typically the feet).
    """
    best: list[tuple[int, int, int]] = []
    cur: list[tuple[int, int, int]] = []
    for y, run, sx in rows:
        if y < start or y > end or run < threshold:
            cur = []
            continue
        cur.append((y, run, sx))
        if len(cur) > len(best):
            best = list(cur)
    return best


def _find_shadow_ellipse(im: Image.Image, bbox: tuple[int, int, int, int]) -> tuple[float, float, float, float] | None:
    """Return (cx, cy, a_axis, b_axis) for the drop shadow, or None if not found.

    Two-pass detection with adaptive thresholds:
      1. Find the longest contiguous row run in the bbox's lower half where
         every row has contiguous shadow-like pixels >= ROW_DETECT_THRESHOLD.
      2. Within that row run, keep only rows with run >= CORE_THRESHOLD.
      3. Try CORE_THRESHOLDS in order — the strictest first. Take the first
         threshold that yields a valid core of MIN_RUN_ROWS rows.
    """
    bx, by_, bw, bh = bbox
    cy_start = int(by_ + bh * LOWER_HALF_START)
    w, h = im.size
    rgba = im.load()
    x_lo = max(0, bx)
    x_hi = min(w, bx + bw)

    # 1) Per-row contiguous run within bbox width.
    rows: list[tuple[int, int, int]] = []  # (y, run_len, run_start_x)
    for y in range(max(0, cy_start), min(h, by_ + bh + 1)):
        run, sx = _row_max_contiguous(rgba, y, x_lo, x_hi)
        rows.append((y, run, sx))

    # 2) Longest contiguous run of qualifying rows.
    best_start = -1
    best_end = -1
    cur_start = -1
    for y, run, _ in rows:
        if run >= ROW_DETECT_THRESHOLD:
            if cur_start < 0:
                cur_start = y
            cur_end = y
            if cur_end - cur_start > best_end - best_start:
                best_start, best_end = cur_start, cur_end
        else:
            cur_start = -1
    if best_start < 0 or (best_end - best_start + 1) < MIN_RUN_ROWS:
        return None

    # 3) Try each CORE_THRESHOLD in order, strictest first.
    for core_thresh in CORE_THRESHOLDS:
        core_rows = _longest_core_run(rows, best_start, best_end, core_thresh)
        if len(core_rows) < MIN_RUN_ROWS:
            continue
        widest_w = 0
        widest_sx = 0
        for _, run, sx in core_rows:
            if run > widest_w:
                widest_w = run
                widest_sx = sx
        a_axis = widest_w / 2.0
        b_axis = (core_rows[-1][0] - core_rows[0][0] + 1) / 2.0
        cx = widest_sx + a_axis
        cy = (core_rows[0][0] + core_rows[-1][0]) / 2.0
        return cx, cy, a_axis, b_axis
    return None


def _erase_ellipse(im: Image.Image, cx: float, cy: float, a_axis: float, b_axis: float) -> None:
    a_axis *= ELLIPSE_PAD
    b_axis *= ELLIPSE_PAD
    rgba = im.load()
    x_lo = max(0, int(cx - a_axis))
    x_hi = min(im.size[0] - 1, int(cx + a_axis))
    y_lo = max(0, int(cy - b_axis))
    y_hi = min(im.size[1] - 1, int(cy + b_axis))
    for y in range(y_lo, y_hi + 1):
        for x in range(x_lo, x_hi + 1):
            dx = (x - cx) / a_axis
            dy = (y - cy) / b_axis
            if dx * dx + dy * dy <= 1.0:
                r, g, b, _ = rgba[x, y]
                rgba[x, y] = (r, g, b, 0)


def _make_backup() -> None:
    if BACKUP.exists():
        shutil.rmtree(BACKUP)
    shutil.copytree(ASSETS, BACKUP)


def _process(dry_run: bool) -> tuple[int, int, list[str]]:
    manifest_path = ASSETS / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    parts: list[dict] = []
    for cat, info in manifest["categories"].items():
        for p in info["parts"]:
            parts.append(p)

    modified = 0
    skipped = 0
    errors: list[str] = []
    for p in parts:
        rel = p["file"]
        path = ASSETS / rel
        bbox = tuple(p["bbox"])  # type: ignore[assignment]
        try:
            im = Image.open(path).convert("RGBA")
            ellipse = _find_shadow_ellipse(im, bbox)
            if ellipse is None:
                skipped += 1
                continue
            cx, cy, a_axis, b_axis = ellipse
            if not dry_run:
                _erase_ellipse(im, cx, cy, a_axis, b_axis)
                im.save(path)
            modified += 1
        except Exception as e:  # noqa: BLE001
            errors.append(f"{rel}: {e}")

    return modified, skipped, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would change without writing files")
    args = parser.parse_args()

    if not args.dry_run:
        _make_backup()
        print(f"backup -> {BACKUP.relative_to(REPO)}")

    modified, skipped, errors = _process(dry_run=args.dry_run)
    print(f"{'(dry-run) ' if args.dry_run else ''}modified: {modified}, "
          f"skipped (no shadow found): {skipped}, errors: {len(errors)}")
    for e in errors:
        print(f"  ! {e}")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
