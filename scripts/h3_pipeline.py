"""H3 动画生成流水线：定妆图 → idle 视频 → walk 视频。

同角色的两段**串行**：walk 的首帧参考用 idle 输出的第一帧，这样同一个角色的
idle 和 walk 必然是同一种渲染风格（H3 每次生成的风格发挥会漂，串行能锁住）。

用法：
    py -3 scripts/h3_pipeline.py run <角色名> <动作>   # 单段（动作=idle|walk）
    py -3 scripts/h3_pipeline.py chain <角色名>        # idle 跑完再用它首帧跑 walk
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

from prepare_clip import prepare

ROOT = Path(__file__).resolve().parent.parent
ANIM = ROOT / "assets" / "sprites" / "characters" / "anim"
MODEL = "MiniMax-H3"
# H3 的时长下限是 4 秒。6 秒的中段会整段漂移（背景变色、角色变形），
# 4 秒把漂移窗口压到最小。
DURATION = 4
RESOLUTION = "768P"
POLL_INTERVAL = 20
POLL_TIMEOUT = 3600

# 每个角色最显眼的辨识特征。写进 prompt 当作参考图之外的补充描述，
# 不要和参考图矛盾——参考图才是造型的主来源。
# 每个角色最显眼的辨识特征。写进 prompt 当作参考图之外的补充描述。
#
# 铁律：文字绝不能和参考图矛盾。H3 会拿文字当改造指令——大乘的描述里写了
# "dark brown robe trimmed with gold"，而静态精灵其实是暗红袖 + 棕袍 + 白靴、
# 根本没有金色，于是 walk 生成时 H3 按文字重画了角色（红袖变棕、靴子消失、
# 袍摆外扩）。文字只能复述图里已经看得到的东西，且以参考图为准。
DESC = {
    "office1_sect_master":
        "a tall pointed golden hat with a wide flat brim, a long white beard, "
        "an ochre-yellow robe under a dark red wide cape that spreads to the floor",
    "office2_elder":
        "a bald pale head with white brows and a long white beard reaching his "
        "chest, a deep purple robe with dark red shoulders and wide sleeves",
    "office3_inner_m":
        "brown hair tied in a tall topknot, a blue robe with wide pale sleeves, "
        "a dark brown belt and dark trousers",
    "office4_inner_f":
        "brown hair in two side buns with small gold hairpins, a cream robe with "
        "brown trim and a pale waist sash",
    "office5_inner_f2":
        "dark brown hair in a tall bun, a rose-red robe with pink trim and an "
        "olive-green waist sash",
    "office6_outer_m":
        "brown hair in a topknot with a hairpin, a light grey robe with white and "
        "brown trim and a brown belt",
    "office7_outer_m2":
        "brown hair in a topknot, a white tunic with olive-green trim over a pale "
        "skirt and brown boots",
    "office8_outer_f":
        "brown hair in two side buns with a red hairpin, a tan robe with darker "
        "brown trim",
    "office9_rogue":
        "a shaggy dark brown fur cloak and long messy hair covering the body, "
        "only a small pale face showing, a crooked wooden staff in one hand",
    "path_sword":
        "long black hair with a headband, a white and pale grey robe, holding a "
        "straight silver sword raised in one hand",
    "path_alchemy":
        "brown hair in twin side buns with gold hairpins, a green robe with gold "
        "trim, holding a small green vessel in one hand",
    "path_body":
        "an enormous bare pale muscular torso with huge flexed arms and "
        "shoulders, dark brown shorts, a small topknot",
    "path_talisman":
        "dark messy brown hair with a gold-and-red hair ornament, a red robe with "
        "gold trim and a pale green scarf, a small gold talisman slip held up in "
        "one hand",
    "realm1_yinqi":
        "a tall grey wide-brimmed hat with a small orange jewel, a white robe with "
        "olive-green trim",
    "realm2_lianqi":
        "black hair in a topknot, a teal robe",
    "realm3_zhuji":
        "black hair in a topknot, a teal tunic with pale gold trim",
    "realm4_jindan":
        "a tall grey wide-brimmed hat with a small teal jewel, a tan robe with a "
        "white fur collar and a teal pendant",
    "realm5_yuanying":
        "a tall black wide-brimmed hat, a purple robe with gold trim",
    "realm6_huashen":
        "black hair in a topknot, a purple robe with a gold waist sash",
    "realm7_lianxu":
        "a tall black wide-brimmed hat over long messy dark hair, a dark brown "
        "robe, a long crooked dark staff held in one hand",
    "realm8_heti":
        "a tall black wide-brimmed hat, a dark brown robe with grey sleeves",
    "realm9_dacheng":
        "dark brown hood-like hair covering the sides of the head, a plain pale "
        "face, a tan-brown robe with dark red sleeves and white boots",
}

MOTION = {
    "idle":
        "The cultivator performs one restrained idle breathing and robe-sway cycle at a "
        "steady slow cadence: the chest and shoulders rise and fall slightly, the wide "
        "sleeves shift gently with the breathing, the hair and the hem of the robes sway "
        "softly, while the feet and lower body stay planted and the figure never leaves "
        "its position on screen.",
    "walk":
        "The cultivator repeats an in-place treadmill walk cycle at a steady cadence: the "
        "feet alternate forward and back in place without travelling across the frame, "
        "the body bobs slightly with each step, the wide sleeves swing gently opposite the "
        "legs, and the hair sways with the motion. The figure stays at exactly the same "
        "screen position, keeps the same front-facing orientation, and the clip completes "
        "only whole left-right gait cycles so that the final frame lands on the same "
        "foot-contact phase as the opening frame.",
}


def build_prompt(desc: str, move: str) -> str:
    """H3 官方三字段格式。FL2VA 必须写首尾帧对齐行，两个音频字段都写 N/A。"""
    return (
        "How the reference pictures align with the target video — Picture 1 (from Shot 1) "
        f"aligns with the 0.00-second mark of the target video; Picture 2 (from Shot 1) "
        f"aligns with the {DURATION:.2f}-second mark of the target video.\n\n"
        "integrated_multimodal_description: [Shot 1] Chibi pixel-art game sprite animation, "
        "two-head-tall proportions, crisp blocky pixels with hard pixel edges, "
        "no anti-aliasing, flat limited-palette colors, clean dark outlines, a front-facing "
        "static shot reproduces the chibi Chinese immortal cultivator of Picture 1 exactly — "
        f"{desc}. Picture 1 is the only authority on appearance: the face, hairstyle, "
        "headwear, every garment, every colour, every prop and the whole silhouette are "
        "copied unchanged from Picture 1 and must never be redesigned, recoloured, "
        "simplified or restyled; the wording above only restates what is already visible "
        "in Picture 1. Both feet on the ground, his entire body inside the frame. "
        "The flat solid magenta #FF00FF background established by Picture 1, a single "
        "unchanging colour that never shifts, fades or changes hue at any point. "
        "The character's face, hairstyle, headwear, clothing, colours, body proportions and "
        "the background colour never change at any point; the only change is the body "
        "motion described below. "
        f"{MOTION[move]} The overall framing and scale remain unchanged throughout. "
        "The camera holds a completely static shot. The motion progressively returns to the "
        "same action phase, pose, layer positions, and exact composition established by "
        "Picture 2 at the end of the shot. No blinking, no hand gestures, no head turning, "
        "no camera movement, no additional characters, no effects and no new props appear, "
        "no aura, no glow, no halo and no shadow on the ground. Any prop already held stays "
        "in the same hand and keeps its shape. The clip is completely silent.\n\n"
        "overall_soundscape: N/A\n\n"
        "non_diegetic_music: N/A"
    )


def mcode(*args: str) -> str:
    proc = subprocess.run(
        ["mcode-tools.cmd", *args], capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    )
    if proc.returncode != 0:
        raise SystemExit(f"mcode-tools 失败：{' '.join(args)}\n{proc.stderr}")
    return proc.stdout


def parse_json(text: str) -> dict:
    text = text.strip()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        start = text.find("{")
        end = text.rfind("}")
        if start < 0 or end < 0:
            raise SystemExit(f"无法解析 JSON：{text[:300]}")
        return json.loads(text[start:end + 1])


def upload(path: Path) -> str:
    out = parse_json(mcode("upload_temp_url", str(path), "--mime-type", "image/png"))
    return out["temp_url"]


def submit(ref_url: str, prompt: str, tag: Path) -> str:
    args = {
        "model": MODEL,
        "reference_type": "first_frame",
        "input_image": {"url": ref_url, "mime_type": "image/png"},
        "last_frame_image": {"url": ref_url, "mime_type": "image/png"},
        "prompt": prompt,
        "duration": DURATION,
        "resolution": RESOLUTION,
    }
    args_file = tag.with_suffix(".submit.json")
    args_file.write_text(json.dumps(args, ensure_ascii=False, indent=1),
                         encoding="utf-8")
    # 撞上并发上限（429）或服务端抽风时重试。槽位会在任务被确认完成后释放。
    last = ""
    for attempt in range(5):
        raw = mcode(
            "connector", "call", "connector__matrix__submit_video_generation",
            "--args-file", str(args_file),
        )
        out = parse_json(raw)
        if "task_id" in out:
            return out["task_id"]
        last = raw.strip()
        print(f"提交未返回 task_id（第 {attempt + 1} 次）：{last}", flush=True)
        time.sleep(20 * (attempt + 1))
    raise SystemExit(f"提交失败：{last}")


def wait_task(task_id: str) -> dict:
    deadline = time.time() + POLL_TIMEOUT
    while time.time() < deadline:
        out = parse_json(mcode(
            "connector", "call", "connector__matrix__query_video_generation",
            "--args", json.dumps({"task_id": task_id, "model": MODEL}),
        ))
        status = out.get("status")
        if status in ("succeeded", "failed", "cancelled"):
            return out
        time.sleep(POLL_INTERVAL)
    raise SystemExit(f"任务超时：{task_id}")


def download(node_id: str, dest: Path) -> None:
    url = parse_json(mcode("get_asset_url", node_id))["download_url"]
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    dest.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(req, timeout=600) as resp, \
            open(dest, "wb") as fh:
        shutil.copyfileobj(resp, fh)


def extract_first_frame(video: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
         "-i", str(video), "-frames:v", "1", str(dest)],
        check=True,
    )


def run_move(name: str, move: str, ref: Path) -> None:
    base = ANIM / name
    tag = base / move
    prompt = build_prompt(DESC[name], move)
    (base / f"{move}.prompt.txt").write_text(prompt, encoding="utf-8")

    print(f"[{name}/{move}] 上传参考图 {ref.name}")
    ref_url = upload(ref)
    task_id = submit(ref_url, prompt, tag)
    print(f"[{name}/{move}] task_id={task_id}")

    result = wait_task(task_id)
    status = result.get("status")
    if status != "succeeded":
        raise SystemExit(f"[{name}/{move}] 生成失败：{status} {result}")
    node_id = result["node_id"]
    raw = base / "videos" / f"{move}_raw.mp4"
    download(node_id, raw)
    (base / f"{move}.task.json").write_text(
        json.dumps({"task_id": task_id, "node_id": node_id, **result},
                   ensure_ascii=False, indent=1),
        encoding="utf-8",
    )
    # H3 常把键色底换成别的颜色，也可能中途把角色整个重画。
    # prepare_clip 负责归一化底色、剔除脚下地面条、截取外形稳定的窗口。
    prepare(raw, base / "videos" / f"{move}.mp4")
    print(f"[{name}/{move}] 完成 -> {base / 'videos' / f'{move}.mp4'}")


def chain(name: str) -> None:
    base = ANIM / name
    if not (base / "videos" / "idle.mp4").exists():
        run_move(name, "idle", base / "key.png")
    ref = base / "videos" / "idle_ref.png"
    if not ref.exists():
        # 从原始视频取首帧：FL2VA 会把首帧钉在参考图上，所以它就是「原始姿势」。
        # 处理后的 idle.mp4 首帧已经被切掉了冻结段，是动作帧，不能当参考图。
        extract_first_frame(base / "videos" / "idle_raw.mp4", ref)
    run_move(name, "walk", ref)


def pick_ref(name: str, move: str) -> Path:
    """walk 的参考图优先用 idle 的首帧，锁住同角色的渲染风格。"""
    base = ANIM / name
    idle_ref = base / "videos" / "idle_ref.png"
    if move == "walk" and idle_ref.exists():
        return idle_ref
    return base / "key.png"


def main() -> None:
    if len(sys.argv) < 3:
        raise SystemExit(
            "用法：py -3 scripts/h3_pipeline.py run|chain <角色名> [<动作>]")
    cmd, name = sys.argv[1], sys.argv[2]
    if cmd == "run":
        move = sys.argv[3]
        run_move(name, move, pick_ref(name, move))
    elif cmd == "chain":
        chain(name)
    else:
        raise SystemExit(f"未知子命令：{cmd}")


if __name__ == "__main__":
    main()
