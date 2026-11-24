"""
테스트용 패키지 빌드 — Lvl_Battle(인게임 전투)를 Windows 실행 파일로 만들고 학습된 모델을 같이 넣는다.

    python Tools/package_test_build.py                 # E:\\UnrealBuild 에 만든다
    python Tools/package_test_build.py --out D:\\Build  # 다른 곳에
    python Tools/package_test_build.py --models-only   # 패키징 없이 모델만 다시 복사

하는 일
  1. RunUAT BuildCookRun — Development(디버그 선이 보여야 배치 칸 · 레이가 그려진다), Lvl_Battle 만 쿡, pak, 아카이브.
  2. 모델 복사 — Training/<이름>/cum… · Snapshots/<날짜>/cum… 의 정책 파일(*.bin, *_meta.txt)을
     <빌드>/Windows/HAI_Project_TEAM5/ 아래 같은 경로로. 인게임 모델 목록(SHPolicyCatalog)은 FPaths::ProjectDir() 기준으로
     이 두 폴더를 훑는데, 패키지 빌드의 ProjectDir 가 바로 <빌드>/Windows/HAI_Project_TEAM5/ 다.
     ★ pak 이 아니라 낱개 파일로 두는 이유: 정책 로더(Learning Agents)가 일반 파일 경로로 읽는다.
  3. <빌드>/Windows/README_테스트빌드.txt 에 실행 방법과 들어간 모델 목록을 쓴다.

★ 실행 전에 에디터 타깃을 빌드해 둘 것(쿡이 에디터 DLL 을 쓴다). 에디터는 켜져 있어도 된다 — 저장된 에셋으로 쿡한다.
"""
from __future__ import annotations

import argparse
import datetime as dt
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UPROJECT = ROOT / "HAI_Project_TEAM5.uproject"
ENGINE = Path(r"C:\Program Files\Epic Games\UE_5.8")
RUN_UAT = ENGINE / "Engine" / "Build" / "BatchFiles" / "RunUAT.bat"
MAP = "/Game/Battle/Lvl_Battle"
MODEL_ROOTS = ("Training", "Snapshots")
MODEL_PATTERNS = ("*.bin", "*_meta.txt")


def package(out: Path) -> None:
    command = [str(RUN_UAT), "BuildCookRun",
               f"-project={UPROJECT}", "-noP4", "-platform=Win64", "-clientconfig=Development",
               "-build", "-cook", f"-map={MAP}", "-stage", "-pak", "-prereqs",
               "-archive", f"-archivedirectory={out}", "-utf8output", "-unattended", "-nocompileeditor"]
    print(">", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode != 0:
        raise SystemExit(f"BuildCookRun 실패 (코드 {result.returncode}) — 위 로그를 볼 것.")


def copy_models(game_dir: Path) -> list[str]:
    copied = []
    for root_name in MODEL_ROOTS:
        source_root = ROOT / root_name
        if not source_root.is_dir():
            continue
        for snapshot in sorted(source_root.glob("*/cum*")):
            files = [f for pattern in MODEL_PATTERNS for f in snapshot.glob(pattern)]
            if not any(f.name.endswith("policy.bin") for f in files):
                continue
            target = game_dir / root_name / snapshot.parent.name / snapshot.name
            target.mkdir(parents=True, exist_ok=True)
            for f in files:
                shutil.copy2(f, target / f.name)
            copied.append(f"{root_name}/{snapshot.parent.name}/{snapshot.name}")
    return copied


def write_readme(windows_dir: Path, models: list[str]) -> None:
    commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    lines = [
        "HAI TEAM5 테스트 빌드",
        f"만든 때: {dt.datetime.now():%Y-%m-%d %H:%M}   코드: {commit}{' + 커밋 안 된 변경' if dirty else ''}   구성: Development",
        "",
        "실행: HAI_Project_TEAM5.exe (창 모드로: HAI_Project_TEAM5.exe -windowed -ResX=1600 -ResY=900)",
        "로그: HAI_Project_TEAM5\\Saved\\Logs\\HAI_Project_TEAM5.log",
        "",
        "화면 좌측 상단: 맵 · 배속 · 시야/조준 레이 · 팀별 조종(RL 모델 / 행동 트리) · 팀별 모델.",
        "맵과 모델은 배치 단계에서만 바꿀 수 있다. 맵을 바꾸면 놓아 둔 아군은 없어지고 새 맵의 적이 선다.",
        "",
        f"들어 있는 모델 {len(models)}개 (HAI_Project_TEAM5\\Training · Snapshots 아래):",
        *[f"  {m}" for m in models],
        "",
        "모델을 더 넣으려면 같은 구조(Training\\<이름>\\cum…\\*.bin, *_meta.txt)로 HAI_Project_TEAM5 폴더 아래에 복사하면 된다.",
    ]
    (windows_dir / "README_테스트빌드.txt").write_text("\r\n".join(lines) + "\r\n", encoding="utf-8-sig")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=Path(r"E:\UnrealBuild"), help="아카이브 폴더")
    parser.add_argument("--models-only", action="store_true", help="패키징 없이 모델만 다시 복사")
    args = parser.parse_args()

    if not args.models_only:
        package(args.out)
    windows_dir = args.out / "Windows"
    game_dir = windows_dir / "HAI_Project_TEAM5"
    if not game_dir.is_dir():
        raise SystemExit(f"{game_dir} 이 없다 — 패키징이 끝났는지 확인할 것.")
    models = copy_models(game_dir)
    write_readme(windows_dir, models)
    print(f"모델 {len(models)}개 복사 → {game_dir}")
    print(f"실행 파일: {windows_dir / 'HAI_Project_TEAM5.exe'}")


if __name__ == "__main__":
    sys.exit(main())
