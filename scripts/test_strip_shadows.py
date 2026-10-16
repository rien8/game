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

SAMPLE_IDS = [
    "head_001", "horn_001", "tail_001", "torso_001",
    "forelimb_001", "hindlimb_001", "wing_001",
    # a few that were hard cases before the LOWER_HALF_START = 0.30 fix:
    "torso_003", "head_005", "wing_008",
]


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


def test_script_exists() -> None:
    assert SCRIPT.exists(), f"missing script: {SCRIPT}"


def test_dry_run_does_not_modify_files() -> None:
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
    try:
        test_script_exists()
        test_dry_run_does_not_modify_files()
        test_shadow_ellipse_is_removed()
    finally:
        _restore_all()
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
