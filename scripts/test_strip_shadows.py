"""Tests for scripts/strip_shadows.py.

Asserts the script removes the elliptical drop shadow baked into each sprite
in assets/parts_processed without damaging the character body. Run:
    python scripts/test_strip_shadows.py
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
ASSETS = REPO / "assets" / "parts_processed"
BACKUP = REPO / "temp" / "parts_processed.bak"
SCRIPT = REPO / "scripts" / "strip_shadows.py"

sys.path.insert(0, str(REPO / "scripts"))
import strip_shadows  # noqa: E402

SAMPLE_IDS = [
    "head_001", "horn_001", "tail_001", "torso_001",
    "forelimb_001", "hindlimb_001", "wing_001",
    # a few that were hard cases before the LOWER_HALF_START = 0.30 fix:
    "torso_003", "head_005", "wing_008",
]

# The sprite fixtures live outside git, so the end-to-end tests can only run on
# a machine that has them. The synthetic tests below cover the detection logic
# itself and always run.
ASSETS_PRESENT = (ASSETS / "manifest.json").exists()


class SkipTest(Exception):
    pass


def _skip_without_assets() -> None:
    if not ASSETS_PRESENT:
        raise SkipTest("assets/parts_processed not present")


def _read_manifest() -> dict:
    return json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))


def _restore_samples() -> None:
    if not BACKUP.exists():
        return
    for sid in SAMPLE_IDS:
        cat = sid.split("_")[0]
        src = BACKUP / cat / f"{sid}.png"
        dst = ASSETS / cat / f"{sid}.png"
        if src.exists():
            shutil.copy2(src, dst)


def _restore_all() -> None:
    if not BACKUP.exists():
        return
    for cat_dir in BACKUP.iterdir():
        if not cat_dir.is_dir():
            continue
        for png in cat_dir.glob("*.png"):
            shutil.copy2(png, ASSETS / cat_dir.name / png.name)


def _shadow_pixel() -> tuple[int, int, int, int]:
    """A pixel the detector classifies as shadow-like."""
    return (250, 250, 250, 40)


def _build_sprite() -> tuple[Image.Image, tuple[int, int, int, int]]:
    """A 256x256 sprite with a decoy shadow band outside the bbox.

    The decoy (x 10..90, 80 px wide) is wider than the real shadow inside the
    bbox (50 px), so a scan that ignores the bbox's horizontal extent locks onto
    the decoy and centres the fitted ellipse at x ~ 50 instead of x ~ 128.
    """
    bbox = (100, 60, 56, 140)          # x 100..156, y 60..200
    im = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    px = im.load()

    # Decoy band: outside the bbox on the left, longer run, same row range.
    for y in range(140, 176):
        for x in range(10, 90):
            px[x, y] = _shadow_pixel()

    # Real shadow: inside the bbox, 50 px wide, centred on x = 128.
    for y in range(140, 171):
        for x in range(103, 153):
            px[x, y] = _shadow_pixel()

    return im, bbox


def test_detection_ignores_pixels_outside_the_bbox() -> None:
    """The row scan must be bounded by the bbox's x range.

    bbox is manifest data, not a type invariant, but it is the only thing
    saying which pixels belong to this part. Scanning the full sprite width lets
    any other near-white low-alpha pixels steer the fit.
    """
    im, bbox = _build_sprite()
    ellipse = strip_shadows._find_shadow_ellipse(im, bbox)

    assert ellipse is not None, "expected the in-bbox shadow to be found"
    cx, cy, a_axis, b_axis = ellipse
    bx, by, bw, bh = bbox
    assert bx <= cx <= bx + bw, (
        f"fitted cx={cx} falls outside the bbox x range [{bx}, {bx + bw}] — "
        f"the ellipse is being centred on pixels outside this part"
    )
    assert abs(cx - 128) <= 1.0, f"expected cx near 128, got {cx}"
    assert abs(cy - 155) <= 1.0, f"expected cy near 155, got {cy}"
    assert abs(a_axis - 25.0) <= 1.0, f"expected a_axis near 25, got {a_axis}"


def test_longest_core_run_does_not_span_gaps() -> None:
    """b_axis and cy come from the first and last row of the result, so a gap
    between two qualifying rows must split the run rather than be spanned."""
    # rows: y 10..13 qualify, y 14 is a gap, y 15..20 qualify (longer).
    rows = [(10, 120, 0), (11, 120, 0), (12, 120, 0), (13, 120, 0),
            (14, 5, 0), (15, 120, 0), (16, 120, 0), (17, 120, 0),
            (18, 120, 0), (19, 120, 0), (20, 120, 0)]

    best = strip_shadows._longest_core_run(rows, 10, 20, 100)
    assert [r[0] for r in best] == [15, 16, 17, 18, 19, 20], (
        "the run must not reach across the gap at y=14"
    )

    # Without the gap, the whole span comes back.
    full = strip_shadows._longest_core_run(
        [(10, 120, 0), (11, 120, 0), (12, 120, 0), (13, 120, 0)], 10, 13, 100)
    assert [r[0] for r in full] == [10, 11, 12, 13]


def test_erase_ellipse_stays_inside_the_shadow() -> None:
    """The padded ellipse must not reach a character pixel below the shadow."""
    im, bbox = _build_sprite()
    ellipse = strip_shadows._find_shadow_ellipse(im, bbox)
    assert ellipse is not None
    cx, cy, a_axis, b_axis = ellipse

    # Put an opaque "body" pixel just under the fitted ellipse.
    body = im.load()
    body_x, body_y = 128, int(cy + b_axis * strip_shadows.ELLIPSE_PAD) + 2
    body[body_x, body_y] = (200, 40, 40, 255)

    strip_shadows._erase_ellipse(im, cx, cy, a_axis, b_axis)
    assert im.load()[body_x, body_y][3] == 255, (
        "erasing the shadow also clipped a body pixel below it"
    )


def test_script_exists() -> None:
    assert SCRIPT.exists(), f"missing script: {SCRIPT}"


def test_dry_run_does_not_modify_files() -> None:
    _skip_without_assets()
    _restore_samples()
    by_id = {}
    for cat, info in _read_manifest()["categories"].items():
        for p in info["parts"]:
            by_id[p["id"]] = p
    mtimes_before = {
        (ASSETS / by_id[sid]["file"]).stat().st_mtime for sid in SAMPLE_IDS
    }
    result = subprocess.run(
        [sys.executable, str(SCRIPT), "--dry-run"],
        capture_output=True, text=True, cwd=str(REPO), timeout=120,
    )
    assert result.returncode == 0, f"dry-run failed:\n{result.stderr}"
    mtimes_after = {
        (ASSETS / by_id[sid]["file"]).stat().st_mtime for sid in SAMPLE_IDS
    }
    assert mtimes_before == mtimes_after, "dry-run must not modify any file"


def test_shadow_ellipse_is_removed() -> None:
    """After the strip, most bbox-internal pixel changes must be alpha -> 0
    erasures (the shadow being erased), not character body edits."""
    _skip_without_assets()
    _restore_samples()
    result = subprocess.run(
        [sys.executable, str(SCRIPT)],
        capture_output=True, text=True, cwd=str(REPO), timeout=300,
    )
    assert result.returncode == 0, f"strip_shadows failed:\n{result.stderr}"

    manifest = _read_manifest()
    by_id = {}
    for cat, info in manifest["categories"].items():
        for p in info["parts"]:
            by_id[p["id"]] = p

    for sid in SAMPLE_IDS:
        meta = by_id[sid]
        bbox = tuple(meta["bbox"])  # type: ignore[assignment]
        before = Image.open(BACKUP / meta["file"]).convert("RGBA")
        after = Image.open(ASSETS / meta["file"]).convert("RGBA")
        a = before.load(); b = after.load()
        bx, by, bw, bh = bbox
        changed = 0
        erased = 0
        for y in range(by, by + bh):
            for x in range(bx, bx + bw):
                if a[x, y] != b[x, y]:
                    changed += 1
                    if a[x, y][3] > 0 and b[x, y][3] == 0:
                        erased += 1
        assert changed > 500, (
            f"{sid}: expected >500 pixels changed, got {changed} "
            f"(script may have skipped this image)"
        )
        assert erased >= changed * 0.85, (
            f"{sid}: only {erased}/{changed} ({100*erased/changed:.1f}%) "
            f"changes were shadow erasures — expected >= 85%"
        )


def main() -> int:
    # Synthetic tests first: they need no fixtures and cover the detection
    # logic the end-to-end tests can only exercise when the sprites are present.
    always = [
        test_script_exists,
        test_detection_ignores_pixels_outside_the_bbox,
        test_longest_core_run_does_not_span_gaps,
        test_erase_ellipse_stays_inside_the_shadow,
    ]
    needs_assets = [
        test_dry_run_does_not_modify_files,
        test_shadow_ellipse_is_removed,
    ]

    try:
        for fn in always + needs_assets:
            try:
                fn()
            except SkipTest as skip:
                print(f"SKIP {fn.__name__}: {skip}")
        if not ASSETS_PRESENT:
            print("note: sprite fixtures absent — end-to-end tests did not run")
    finally:
        if ASSETS_PRESENT:
            _restore_all()
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
