"""Inference-only Unreal integration checks; no training or model updates."""
import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", required=True, type=Path)
    parser.add_argument("--seconds", type=int, default=15)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    snapshot = project / "Snapshots/2026-10-15/cum008h00m"
    output = project / "Saved/AimExperimentVerification" / time.strftime("%Y%m%d-%H%M%S")
    output.mkdir(parents=True, exist_ok=False)
    runs = project / "Saved/SHExperiments"
    cases = [
        ("baseline", False, 1, True, True),
        ("no-align", False, 1, False, True),
        ("no-shaping", False, 1, False, False),
        ("assist-zero", True, 0, True, True),
        ("assist-half", True, 0.5, False, True),
        ("assist-full", True, 1, False, True),
    ]
    summaries = []
    for name, assist, strength, align, see in cases:
        before = set(runs.glob("*/settings.json"))
        command = [
            str(args.editor), str(project / "HAI_Project_TEAM5.uproject"),
            "/Game/RL/Lvl_SHTrain", "-game", "-nullrhi", "-unattended", "-nosound",
            "-NoSplash", "-NoSourceControl", "-DisablePython", "-GymsCountOverride=1",
            f"-SHEval={snapshot}", "-SHSeed=1234", "-SHEvalNoise=0",
            f"-SHTrainSeconds={args.seconds}", "-SHReportInterval=3",
            f"-SHExperiment=verify-{name}", f"-SHAimAssist={int(assist)}",
            f"-SHAimAssistStrength={strength}", f"-SHAimReward={int(align)}",
            f"-SHSeeReward={int(see)}", f"-abslog={output / (name + '.log')}",
        ]
        print(f"Running {name} (inference only)...", flush=True)
        with (output / (name + "-stdout.log")).open("w", encoding="utf-8") as stdout:
            result = subprocess.run(command, cwd=project, stdout=stdout,
                                    stderr=subprocess.STDOUT, timeout=240)
        assert result.returncode == 0, (name, result.returncode)
        created = set(runs.glob("*/settings.json")) - before
        matches = [p for p in created if json.loads(p.read_text(encoding="utf-8"))["experiment"] == f"verify-{name}"]
        assert len(matches) == 1, (name, created)
        settings_path = matches[0]
        settings = json.loads(settings_path.read_text(encoding="utf-8"))
        assert settings["mode"] == "evaluation"
        assert settings["aim_assist"] == assist
        assert settings["aim_assist_strength"] == strength
        assert settings["aim_reward"] == align and settings["see_reward"] == see
        assert settings["seed"] == 1234
        with settings_path.with_name("rewards.csv").open(encoding="utf-8-sig", newline="") as stream:
            rows = [{key: float(value) for key, value in row.items()} for row in csv.DictReader(stream)]
        assert rows and sum(row["samples"] for row in rows) > 0, name
        for row in rows:
            assert all(math.isfinite(v) for v in row.values()), (name, row)
            applied = sum(row[k] for k in ("individual_applied", "team_applied", "outcome", "clock"))
            assert math.isclose(applied, row["total"], rel_tol=1e-5, abs_tol=1e-4), (name, applied, row["total"])
            # A complete team is registered: mixing must preserve its reward sum.
            raw = sum(value for key, value in row.items() if key.startswith("raw_"))
            mixed = row["individual_applied"] + row["team_applied"]
            assert math.isclose(raw, mixed, rel_tol=1e-5, abs_tol=1e-4), (name, raw, mixed)
            if not align:
                assert row["raw_align"] == 0, (name, row)
            if not see:
                assert row["raw_see"] == 0, (name, row)
            assert 0 <= row["assisted_decisions"] <= row["live_decisions"]
        assisted = sum(row["assisted_decisions"] for row in rows)
        if assist and strength > 0:
            assert assisted > 0, (name, "No visible targets exercised assistance")
        else:
            assert assisted == 0, name
        if align:
            assert sum(row["raw_align"] for row in rows) > 0, name
        if see:
            assert sum(row["raw_see"] for row in rows) > 0, name
        log = (output / (name + ".log")).read_text(encoding="utf-8-sig", errors="replace")
        assert "[SH_Rewards]" in log and "[SH_Experiment]" in log, name
        assert "Begin Training" not in log and "Traceback (most recent call last)" not in log, name
        summaries.append({"case": name, "settings": str(settings_path), "rows": len(rows),
                          "samples": sum(row["samples"] for row in rows), "assisted_decisions": assisted})
        print(f"Passed {name}: {summaries[-1]}", flush=True)
    (output / "results.json").write_text(json.dumps(summaries, indent=2), encoding="utf-8")
    print(f"All {len(cases)} inference checks passed: {output}", flush=True)


if __name__ == "__main__":
    main()
