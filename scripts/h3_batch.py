"""并行跑多个角色的 H3 动画生成。

串行跑 32 段视频要 8-10 小时。同一个角色的 idle 与 walk 必须串行（walk 的参考图
是 idle 的首帧），但不同角色之间没有任何依赖，所以并发提交、并发轮询。

**H3 同时在飞的生成任务上限是 5**，超过会返回 429
（video generation limit reached）。所以按波次调度：一波最多 5 个角色，
一波走完 idle→walk 再开下一波。任务状态一旦被查询确认完成，槽位就释放了。

流程：
    1. 合成定妆图（缺 key.png 时）
    2. 波次内并发提交 idle → 并发轮询 → 下载 → 归一化 → 抽 idle_ref.png
    3. 波次内并发提交 walk → 并发轮询 → 下载 → 归一化
    4. 打包 + 质检

用法：
    py -3 scripts/h3_batch.py <角色名> [...]
"""

from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

from h3_pipeline import (
    ANIM, DESC, POLL_INTERVAL, POLL_TIMEOUT, ROOT, build_prompt, download,
    extract_first_frame, mcode, parse_json, submit, upload,
)
from prepare_clip import prepare

PY = sys.executable
SCRIPTS = ROOT / "scripts"
WAVE = 5          # H3 同时在飞的任务上限
SUBMIT_GAP = 5    # 连续提交之间留的间隔，避免撞限流


def ensure_key(name: str) -> Path:
    # 每次都重建：定妆图规则可能改过（光环剔除之类），重建是确定性的
    subprocess.run([PY, str(SCRIPTS / "make_key_image.py"), name], check=True)
    return ANIM / name / "key.png"


def start_move(name: str, move: str, ref: Path) -> str:
    """提交一段生成，返回 task_id。

    已提交过的动作直接复用 task_id——批量跑到一半失败时重跑不会重复烧积分。
    """
    base = ANIM / name
    record = base / f"{move}.taskid.json"
    if record.exists():
        task_id = json.loads(record.read_text(encoding="utf-8"))["task_id"]
        print(f"[{name}/{move}] 复用已有任务 {task_id}", flush=True)
        return task_id

    prompt = build_prompt(DESC[name], move)
    (base / f"{move}.prompt.txt").write_text(prompt, encoding="utf-8")
    print(f"[{name}/{move}] 上传参考图 {ref.name}", flush=True)
    ref_url = upload(ref)
    task_id = submit(ref_url, prompt, base / move)
    record.write_text(json.dumps({"task_id": task_id}, ensure_ascii=False),
                      encoding="utf-8")
    print(f"[{name}/{move}] task_id={task_id}", flush=True)
    return task_id


def wait_all(jobs: list[tuple[str, str, str]]) -> dict[str, dict]:
    """并发轮询所有任务，返回 task_id -> 结果。"""
    pending = {tid: (name, move) for name, move, tid in jobs}
    deadline = time.time() + POLL_TIMEOUT
    done: dict[str, dict] = {}
    while pending and time.time() < deadline:
        for tid in list(pending):
            out = parse_json(mcode(
                "connector", "call",
                "connector__matrix__query_video_generation",
                "--args", f'{{"task_id":"{tid}","model":"MiniMax-H3"}}',
            ))
            status = out.get("status")
            if status in ("succeeded", "failed", "cancelled"):
                name, move = pending.pop(tid)
                print(f"[{name}/{move}] {status}", flush=True)
                done[tid] = out
        if pending:
            time.sleep(POLL_INTERVAL)
    for tid, (name, move) in pending.items():
        raise SystemExit(f"[{name}/{move}] 轮询超时：{tid}")
    return done


def fetch(tid: str, result: dict, name: str, move: str) -> None:
    """下载原始视频并做背景归一化。生成失败的任务只记录，不中断整批。"""
    base = ANIM / name
    if result.get("status") != "succeeded":
        print(f"[{name}/{move}] 生成未成功（{result.get('status')}），跳过", flush=True)
        return
    raw = base / "videos" / f"{move}_raw.mp4"
    download(result["node_id"], raw)
    (base / f"{move}.task.json").write_text(
        f'{{"task_id": "{tid}", "node_id": "{result["node_id"]}"}}',
        encoding="utf-8",
    )
    prepare(raw, base / "videos" / f"{move}.mp4")
    print(f"[{name}/{move}] 完成 -> {base / 'videos' / f'{move}.mp4'}", flush=True)


def ref_for(name: str, move: str) -> Path:
    """idle 用定妆图，walk 用 idle 原始视频的首帧（FL2VA 钉住的原始姿势）。"""
    base = ANIM / name
    if move == "idle":
        return base / "key.png"
    ref = base / "videos" / "idle_ref.png"
    if not ref.exists():
        extract_first_frame(base / "videos" / "idle_raw.mp4", ref)
    return ref


def run_wave(wave: list[str]) -> None:
    # 阶段一：没有 idle 的跑 idle；已有 idle 的直接跑 walk
    jobs: list[tuple[str, str, str]] = []
    fresh: list[str] = []
    for name in wave:
        if (ANIM / name / "videos" / "idle.mp4").exists():
            print(f"[{name}] 已有 idle，本波直接跑 walk", flush=True)
            move = "walk"
        else:
            move = "idle"
            fresh.append(name)
        jobs.append((name, move, start_move(name, move, ref_for(name, move))))
        time.sleep(SUBMIT_GAP)
    results = wait_all(jobs)
    for name, move, tid in jobs:
        fetch(tid, results[tid], name, move)

    # 阶段二：刚生成的 idle 接着跑 walk
    jobs = []
    for name in fresh:
        if not (ANIM / name / "videos" / "idle.mp4").exists():
            print(f"[{name}] idle 未产出，跳过 walk", flush=True)
            continue
        jobs.append((name, "walk",
                     start_move(name, "walk", ref_for(name, "walk"))))
        time.sleep(SUBMIT_GAP)
    if not jobs:
        return
    results = wait_all(jobs)
    for name, move, tid in jobs:
        fetch(tid, results[tid], name, move)


def pack(names: list[str]) -> None:
    """打包 + 质检。单个角色打包失败不影响其它角色。"""
    for name in names:
        missing = [m for m in ("idle", "walk")
                   if not (ANIM / name / "videos" / f"{m}.mp4").exists()]
        if missing:
            print(f"[{name}] 缺 {missing}，跳过打包", flush=True)
            continue
        subprocess.run([PY, str(SCRIPTS / "make_anim_atlas.py"), name],
                       check=False)


def main() -> None:
    names = sys.argv[1:]
    if not names:
        raise SystemExit("用法：py -3 scripts/h3_batch.py <角色名> [...]")

    for name in names:
        ensure_key(name)

    for i in range(0, len(names), WAVE):
        wave = names[i:i + WAVE]
        print(f"\n===== 第 {i // WAVE + 1} 波：{', '.join(wave)} =====", flush=True)
        run_wave(wave)
        pack(wave)

    print("\n全部生成完成", flush=True)


if __name__ == "__main__":
    main()
