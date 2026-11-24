"""Compare frozen models locally without opening an editor window or starting training."""
from datetime import datetime, timezone, timedelta
from pathlib import Path
import hashlib
import json
import re
import subprocess
import time
import argparse
import math

base = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--editor", type=Path, required=True, help="Path to UnrealEditor-Cmd.exe (Windows)")
parser.add_argument("--project", type=Path, required=True, help="Path to the built HAI_Project_TEAM5.uproject")
parser.add_argument("--inputs", type=Path, help="Directory containing Training/ (default: project directory)")
parser.add_argument("--output-root", type=Path, help="Default: <project>/Saved/CorridorEvaluation")
parser.add_argument("--rounds", type=int, default=10, help="Rounds per model and seed")
parser.add_argument("--seeds", type=int, nargs="+", default=[1234, 2345, 3456])
parser.add_argument("--dry-run", action="store_true", help="Verify inputs and print commands without starting Unreal")
args = parser.parse_args()
if args.rounds < 1 or any(seed < 0 for seed in args.seeds) or len(set(args.seeds)) != len(args.seeds):
    parser.error("Use positive rounds and distinct nonnegative seeds")
editor = args.editor.resolve()
project_file = args.project.resolve()
project = project_file.parent
inputs = args.inputs.resolve() if args.inputs else project
output_root = args.output_root.resolve() if args.output_root else project / "Saved/CorridorEvaluation"
if not editor.is_file() or not project_file.is_file() or project_file.suffix != ".uproject":
    parser.error("--editor and --project must point to existing Unreal executable and .uproject files")
if not args.dry_run and __import__("os").name != "nt":
    parser.error("The headless process launcher supports Windows only")
stamp = datetime.now(timezone(timedelta(hours=9))).strftime("%Y%m%d-%H%M%S-%f")
output = output_root / (f"local-{args.rounds}x{len(args.seeds)}seeds-" + stamp)
experiment_root = project / "Saved/SHExperiments"
opponent = inputs / "Training/START_11_13/cum017h06m"
cases = [
    ("before-20h06m", inputs / "Training/START_1116_NEW_OPPONENT/cum020h06m"),
    ("after-26h06m", inputs / "Training/START_1120_corridor/cum026h06m"),
]
jobs = [(name, snapshot, seed)
        for index, seed in enumerate(args.seeds)
        for name, snapshot in (cases if index % 2 == 0 else list(reversed(cases)))]
manifest = json.loads((base / "corridor_inputs_manifest.json").read_text(encoding="utf-8"))
for entry in manifest["files"]:
    data = (inputs / entry["path"]).read_bytes()
    actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    if actual != entry["sha"] or len(data) != entry["size"]:
        raise RuntimeError("Input changed: " + entry["path"])

def normalized(value):
    return str(value).replace("\\", "/").rstrip("/").casefold()

results = []
summary_path = output / "results.json"

def write_summary(status, error=None):
    aggregated = []
    for name, _ in cases:
        rows = [row for row in results if row["model"] == name]
        if not rows:
            continue
        n = sum(row["rounds"] for row in rows)
        wins = sum(row["learner_wins"] for row in rows)
        p = wins / n
        z = 1.959963984540054
        denominator = 1 + z * z / n
        center = (p + z * z / (2 * n)) / denominator
        half = z * math.sqrt((p * (1 - p) + z * z / (4 * n)) / n) / denominator
        metric_count = sum(row["round_metric_count"] for row in rows)
        duration_sum = sum(row["round_duration_sum_from_reports"] for row in rows)
        shots = sum(row["shots_fired"] for row in rows)
        hits = sum(row["shots_hit"] for row in rows)
        aggregated.append({"model": name, "rounds": n, "wins": wins,
                           "losses": sum(row["opponent_wins"] for row in rows),
                           "timeouts": sum(row["timeouts"] for row in rows),
                           "mutual_wipes": sum(row["mutual_wipes"] for row in rows),
                           "win_rate_percent": round(100 * p, 2),
                           "wilson_95_percent": [round(100 * max(0, center - half), 2), round(100 * min(1, center + half), 2)],
                           "mean_round_seconds_approx": round(duration_sum / n, 2) if metric_count == n else None,
                           "shots_fired": shots, "shots_hit": hits,
                           "hit_rate_percent": round(100 * hits / shots, 2) if shots else None})
    document = {"status": status, "source_commit": manifest["commit"],
                "protocol": {"rounds_per_model_per_seed": args.rounds, "seeds": args.seeds,
                             "planned_rounds_per_model": args.rounds * len(args.seeds),
                             "arena_count": 1, "alternate_sides": True, "action_noise": 0,
                             "run_order_alternates_between_models": True},
                "results": results, "aggregate": aggregated}
    if error is not None:
        document["error"] = str(error)
    summary_path.write_text(json.dumps(document, ensure_ascii=False, indent=2), encoding="utf-8")

if not args.dry_run:
    output.mkdir(parents=True, exist_ok=False)
print("OUTPUT=" + str(output), flush=True)
try:
    for name, snapshot, seed in jobs:
        run_id = f"{name}-seed{seed}"
        experiment = "local-corridor-" + run_id + "-" + stamp
        log_path = output / (run_id + ".log")
        stdout_path = output / (run_id + "-stdout.log")
        max_wall_seconds = args.rounds * 60 + 120
        command = [
            str(editor), str(project_file),
            "/Game/RL/Lvl_SHTrain", "-game", "-nullrhi", "-unattended", "-nosound",
            "-NoSplash", "-NoSourceControl", "-DisablePython", "-UTF8Output",
            "-GymsCountOverride=1", f"-GymsManagerRandomSeed={seed}", "-SHArena=BP_SHArena_Corridor",
            "-SHEval=" + str(snapshot), "-SHEvalOpponent=" + str(opponent),
            f"-SHEvalRounds={args.rounds}", "-SHEvalNoise=0", f"-SHSeed={seed}", f"-SHEvalSeed={seed}", "-SHEvalRealtime=0",
            "-SHSelfPlay=0", "-SHOpponentMix=0", "-SHAlternateSides=1", "-SHRoundRules=1",
            "-SHAimAssist=0", "-SHSnapshotInterval=0", f"-SHTrainSeconds={max_wall_seconds}", "-SHReportInterval=5",
            "-SHExperiment=" + experiment, "-abslog=" + str(log_path),
        ]
        if args.dry_run:
            print(json.dumps({"run_id": run_id, "command": command}, ensure_ascii=False), flush=True)
            continue
        (output / (run_id + "-command.json")).write_text(json.dumps(command, ensure_ascii=False, indent=2), encoding="utf-8")
        before = set(experiment_root.glob("*/settings.json"))
        print("RUNNING=" + run_id, flush=True)
        started = time.monotonic()
        with stdout_path.open("wb") as stdout:
            completed = subprocess.run(command, cwd=project, stdout=stdout, stderr=subprocess.STDOUT,
                                       timeout=max_wall_seconds + 240, creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        log = log_path.read_text(encoding="utf-8-sig", errors="replace")
        errors = [line for line in log.splitlines() if re.search(
            r"Fatal error:|Assertion failed|LogLearning: Error:|Failed to load network|Invalid Format|"
            r"상대 정책 조립 실패|를 적용하지 못해 종료한다|Begin Training|Traceback \(most recent call last\)", line)]
        if completed.returncode != 0 or errors:
            raise RuntimeError(json.dumps({"case": name, "returncode": completed.returncode, "errors": errors[:12], "log": str(log_path)}, ensure_ascii=False))
        matches = []
        for path in set(experiment_root.glob("*/settings.json")) - before:
            settings = json.loads(path.read_text(encoding="utf-8-sig"))
            if settings.get("experiment") == experiment:
                matches.append((path, settings))
        if len(matches) != 1:
            raise RuntimeError("Expected one settings record: " + name)
        settings_path, settings = matches[0]
        arenas = settings.get("arenas", [])
        if not (settings.get("mode") == "evaluation" and settings.get("observation_size") == 176
                and settings.get("evaluation_max_rounds") == args.rounds
                and settings.get("seed") == seed
                and settings.get("map") == "Lvl_SHTrain"
                and normalized(settings.get("evaluation_snapshot")) == normalized(snapshot)
                and normalized(settings.get("evaluation_opponent")) == normalized(opponent)
                and len(arenas) == 1 and arenas[0]["name"].startswith("BP_SHArena_Corridor_C")):
            raise RuntimeError("Unexpected evaluation settings: " + str(settings_path))
        if "BP_SHArena_Corridor_C" not in log or "ㄱ자" not in log or "스냅샷을 불러왔다" not in log:
            raise RuntimeError("Missing map/model load evidence: " + name)
        if f"GymsManager initialized with seed {seed}!" not in log:
            raise RuntimeError("Requested arena random seed was not verified: " + run_id)
        summaries = re.findall(r"라운드 (\d+) \| 팀0\(RL\) 승 (\d+) / 팀1 승 (\d+) / 시간초과 (\d+) / 동시전멸 (\d+)", log)
        if not summaries or int(summaries[-1][0]) != args.rounds or "cum017h06m" not in log:
            raise RuntimeError("Requested evaluation rounds were not verified: " + run_id)
        rounds, wins, losses, timeouts, mutual = map(int, summaries[-1])
        intervals = re.findall(r"\[SH_CombatTrainer\]\s+라운드 (\d+) \(팀0 승 \d+ / 팀1 승 \d+ / 시간초과 \d+ \([\d.]+%\) / 동시전멸 \d+\).*?평균 길이 ([\d.]+)초", log)
        round_metric_count = sum(int(n) for n, _ in intervals)
        duration_sum = sum(int(n) * float(seconds) for n, seconds in intervals)
        shot_intervals = re.findall(r"\[SH_CombatTrainer\]\s+\d+초 \| 결정 \d+ .*?\| 사격 (\d+) 명중 (\d+)", log)
        result = {"model": name, "snapshot": str(snapshot), "opponent": str(opponent),
                  "rounds": rounds, "learner_wins": wins, "opponent_wins": losses,
                  "timeouts": timeouts, "mutual_wipes": mutual,
                  "map": "Lvl_SHTrain", "arena": arenas[0]["name"], "seed": settings["seed"],
                  "evaluation_noise": settings["evaluation_noise"], "wall_seconds": round(time.monotonic() - started, 2),
                  "round_metric_count": round_metric_count, "round_duration_sum_from_reports": duration_sum,
                  "shots_fired": sum(int(fired) for fired, _ in shot_intervals),
                  "shots_hit": sum(int(hit) for _, hit in shot_intervals),
                  "model_and_opponent_load_verified": True, "corridor_load_verified": True,
                  "log": str(log_path), "stdout": str(stdout_path), "settings": str(settings_path)}
        results.append(result)
        write_summary("running" if len(results) < len(jobs) else "complete")
        print("PASSED=" + json.dumps(result, ensure_ascii=False), flush=True)
except Exception as error:
    write_summary("incomplete", error)
    raise
if args.dry_run:
    print("DRY_RUN_COMPLETE: inputs verified; no Unreal process started and no output files written", flush=True)
else:
    for entry in manifest["files"]:
        data = (inputs / entry["path"]).read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if actual != entry["sha"] or len(data) != entry["size"]:
            write_summary("incomplete", "Input changed after evaluation: " + entry["path"])
            raise RuntimeError("Input changed after evaluation: " + entry["path"])
    print("COMPLETE=" + str(summary_path), flush=True)
