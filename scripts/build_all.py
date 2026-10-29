"""批量生成所有角色的动画精灵图。

单个角色跑一遍完整链路：合成定妆图 → H3 生成 idle → 用 idle 首帧生成 walk →
背景归一化与地面条清理 → 打包并质检。

用法：
    py -3 scripts/build_all.py <角色名> [...]      # 指定角色
    py -3 scripts/build_all.py --all               # 全部 22 个
    py -3 scripts/build_all.py --all --skip office1_sect_master

已经生成过且质检通过的角色用 --skip 跳过，避免重复消耗积分。
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "scripts"
ANIM = ROOT / "assets" / "sprites" / "characters" / "anim"
INDIVIDUAL = ROOT / "assets" / "sprites" / "characters" / "individual"
PY = sys.executable


def run(*args: str) -> None:
    print(f"\n$ {' '.join(str(a) for a in args)}", flush=True)
    subprocess.run([str(a) for a in args], check=True)


def all_names() -> list[str]:
    return sorted(p.stem for p in INDIVIDUAL.glob("*.png"))


def build(name: str) -> None:
    run(PY, SCRIPTS / "make_key_image.py", name)
    run(PY, SCRIPTS / "h3_pipeline.py", "chain", name)
    run(PY, SCRIPTS / "make_anim_atlas.py", name)


def main() -> None:
    args = sys.argv[1:]
    skip: set[str] = set()
    while "--skip" in args:
        i = args.index("--skip")
        skip.update(args[i + 1].split(","))
        del args[i:i + 2]
    names = all_names() if "--all" in args else args
    names = [n for n in names if n not in skip]
    if not names:
        raise SystemExit("没有要处理的角色")

    print(f"共 {len(names)} 个角色：{', '.join(names)}")
    for i, name in enumerate(names, 1):
        print(f"\n===== [{i}/{len(names)}] {name} =====", flush=True)
        build(name)
    print("\n全部完成")


if __name__ == "__main__":
    main()
