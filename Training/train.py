"""자동 학습 실행기.

학습 이름 하나(예: A)마다 Training/<이름>/ 폴더를 쓴다.
  - 언리얼(UnrealEditor-Cmd, Lvl_SHTrain, 헤드리스)로 학습을 돌린다.
  - 트레이너가 1시간마다 Training/<이름>/cum012h00m/ 에 스냅샷을 저장한다(파일 이름 = 누적 학습 시간).
  - 새 스냅샷이 생기면 그 폴더를 git preMerge 에 커밋하고 push 한다.
  - 같은 이름으로 다시 실행하면 그 이름의 최신 스냅샷부터 이어서 학습한다.
  - 보상 함수 등을 바꿨으면 새 이름(B)으로 실행한다. A 는 건드리지 않는다.

사용법은 Training/README.md. 표준 라이브러리만 쓴다(추가 설치 없음).
"""

from __future__ import annotations

import argparse
import base64
import ctypes
import datetime as dt
import getpass
import json
import os
import re
import signal
import subprocess
import sys
import time
import unicodedata
from contextlib import contextmanager
from pathlib import Path

# --- 고정 값 ----------------------------------------------------------------

ROOT = Path(__file__).resolve().parent.parent          # 저장소 루트
TRAINING_DIR = ROOT / "Training"
UPROJECT = ROOT / "HAI_Project_TEAM5.uproject"
TRAIN_MAP = "/Game/RL/Lvl_SHTrain"
BRANCH = "preMerge"
REMOTE = "origin"

# 2026-10-15 ~ 10-25 의 긴 학습(누적 16시간)과 같은 옵션. run.json 에 저장되어 이어 학습할 때도 그대로 쓰인다.
DEFAULT_UE_ARGS = ["-SHSelfPlay=1", "-SHRoundRules=1", "-SHOutcome=0", "-SHReportInterval=60"]

# 이 실행기가 직접 정하는 옵션. run.json 의 추가 옵션으로 덮어쓰면 실행기가 망가지므로 막는다.
RESERVED_PREFIXES = ("-SHSnapshotRoot=", "-SHSnapshotFlat=", "-SHSnapshotInterval=", "-SHResume=",
                     "-SHTrainSeconds=", "-SHEval=", "-abslog=")

SNAPSHOT_DIR_RE = re.compile(r"^cum(\d{3})h(\d{2})m(?:_\d+)?$")
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_\-]{0,63}$")
RESERVED_NAMES = {"logs", "README", "train", "__pycache__"}

POLL_SECONDS = 5            # 로그를 읽는 주기
PUBLISH_EVERY_SECONDS = 30  # 새 스냅샷을 찾는 주기
STABLE_SECONDS = 20         # 스냅샷 폴더의 마지막 파일이 이만큼 지나야 "저장 끝" 으로 본다
QUICK_CRASH_SECONDS = 180   # 언리얼이 이보다 빨리 죽으면 설정 문제로 보고 재시작 횟수를 센다
MAX_QUICK_CRASHES = 3
RESTART_DELAY_SECONDS = 30

# --- 출력 -------------------------------------------------------------------

_log_file: Path | None = None


def say(message: str) -> None:
    line = f"[{dt.datetime.now():%H:%M:%S}] {message}"
    print(line, flush=True)
    if _log_file is not None:
        with _log_file.open("a", encoding="utf-8") as f:
            f.write(line + "\n")


# --- 언리얼 위치 -------------------------------------------------------------

def find_unreal_cmd() -> Path:
    """UnrealEditor-Cmd.exe 를 찾는다. 환경 변수 UE_ROOT → 레지스트리 → 기본 설치 경로 순서."""
    association = json.loads(UPROJECT.read_text(encoding="utf-8")).get("EngineAssociation", "5.8")
    candidates: list[Path] = []
    if os.environ.get("UE_ROOT"):
        candidates.append(Path(os.environ["UE_ROOT"]))
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, rf"SOFTWARE\EpicGames\Unreal Engine\{association}") as key:
            candidates.append(Path(winreg.QueryValueEx(key, "InstalledDirectory")[0]))
    except OSError:
        pass
    candidates.append(Path(rf"C:\Program Files\Epic Games\UE_{association}"))

    for engine_root in candidates:
        exe = engine_root / "Engine" / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe"
        if exe.is_file():
            return exe
    raise SystemExit(f"UnrealEditor-Cmd.exe 를 찾지 못했다(엔진 {association}). 환경 변수 UE_ROOT 에 엔진 폴더를 지정할 것.")


# --- 스냅샷 -----------------------------------------------------------------

def read_meta(snapshot_dir: Path) -> dict[str, str]:
    meta: dict[str, str] = {}
    for meta_file in snapshot_dir.glob("*_meta.txt"):
        for line in meta_file.read_text(encoding="utf-8", errors="replace").splitlines():
            key, sep, value = line.partition("=")
            if sep:
                meta[key.strip()] = value.strip()
    return meta


def cumulative_seconds(snapshot_dir: Path) -> float:
    """meta 의 cumulative_seconds. 없으면 폴더 이름(cum012h30m)에서 계산한다."""
    try:
        return float(read_meta(snapshot_dir)["cumulative_seconds"])
    except (KeyError, ValueError):
        match = SNAPSHOT_DIR_RE.match(snapshot_dir.name)
        return (int(match.group(1)) * 3600 + int(match.group(2)) * 60) if match else 0.0


def list_snapshots(run_dir: Path) -> list[Path]:
    """run_dir 아래의 스냅샷 폴더(meta 가 있는 cum... 폴더). 한 단계 아래(날짜 폴더)까지 본다 —
    -SHSnapshotFlat 를 모르는 오래된 빌드로 돌리면 날짜 폴더가 생기기 때문이다."""
    found: list[Path] = []
    if not run_dir.is_dir():
        return found
    for child in run_dir.iterdir():
        if not child.is_dir() or child.name == "logs":
            continue
        if SNAPSHOT_DIR_RE.match(child.name):
            found.append(child)
        else:
            found.extend(g for g in child.iterdir() if g.is_dir() and SNAPSHOT_DIR_RE.match(g.name))
    return [d for d in found if any(d.glob("*_meta.txt")) and any(d.glob("*_policy.bin"))]


def latest_snapshot(run_dir: Path) -> Path | None:
    snapshots = list_snapshots(run_dir)
    return max(snapshots, key=cumulative_seconds) if snapshots else None


def is_stable(snapshot_dir: Path) -> bool:
    """트레이너가 이 폴더에 쓰기를 끝냈는가. meta 는 bin 들 뒤에 쓰이고, 그 뒤 몇 초 안에 끝난다."""
    files = [f for f in snapshot_dir.iterdir() if f.is_file()]
    if not files:
        return False
    newest = max(f.stat().st_mtime for f in files)
    return time.time() - newest >= STABLE_SECONDS


def hours_minutes(seconds: float) -> str:
    minutes = int(seconds // 60)
    return f"{minutes // 60:03d}h{minutes % 60:02d}m"


# --- 잠금 -------------------------------------------------------------------

def process_alive(pid: int) -> bool:
    """Windows 에서 pid 가 살아 있는가. ★ os.kill(pid, 0) 을 쓰지 않는다 — Windows 에서는 프로세스를 죽인다."""
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    STILL_ACTIVE = 259
    handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        return code.value == STILL_ACTIVE
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


@contextmanager
def run_lock(run_dir: Path):
    """같은 이름을 두 곳에서 동시에 학습하지 못하게 한다(같은 폴더에 두 트레이너가 쓰면 스냅샷이 섞인다)."""
    lock = run_dir / ".train.lock"
    if lock.exists():
        try:
            other = int(lock.read_text().strip())
        except ValueError:
            other = -1
        if other > 0 and process_alive(other):
            raise SystemExit(f"'{run_dir.name}' 은(는) 이미 다른 창에서 학습 중이다(pid {other}).")
    lock.write_text(str(os.getpid()))
    try:
        yield
    finally:
        lock.unlink(missing_ok=True)


@contextmanager
def git_lock():
    """여러 학습을 동시에 돌릴 때 git 명령이 겹치지 않게 한다(index.lock 충돌 방지)."""
    lock = TRAINING_DIR / ".git.lock"
    deadline = time.time() + 300
    while True:
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, str(os.getpid()).encode())
            os.close(fd)
            break
        except FileExistsError:
            try:
                stale = time.time() - lock.stat().st_mtime > 600
            except FileNotFoundError:
                continue
            if stale or time.time() > deadline:
                lock.unlink(missing_ok=True)   # 10분 넘은 잠금은 죽은 실행기의 것이다
                continue
            time.sleep(2)
    try:
        yield
    finally:
        lock.unlink(missing_ok=True)


# --- GitHub 로그인 ------------------------------------------------------------
#
# 토큰은 Training/.env 에 저장한다(.gitignore 로 git 에 올리지 않는다).
# git 에 넘기는 방법: 이 실행기가 띄우는 git 프로세스의 **환경 변수**로만 설정을 준다(GIT_CONFIG_COUNT/KEY/VALUE).
#   - git 설정 파일(.git/config, ~/.gitconfig)에 토큰이 남지 않는다.
#   - 명령줄 인자가 아니라서 작업 관리자의 명령줄에도 보이지 않는다.
#   - credential.helper 를 비워 Windows 자격 증명 관리자 창이 뜨지 않게 한다(밤새 돌다 창 하나에 멈추지 않게).

ENV_PATH = TRAINING_DIR / ".env"
_git_auth_env: dict[str, str] = {}


def load_env() -> dict[str, str]:
    values: dict[str, str] = {}
    if ENV_PATH.exists():
        for line in ENV_PATH.read_text(encoding="utf-8").splitlines():
            key, sep, value = line.partition("=")
            if sep and not key.strip().startswith("#"):
                values[key.strip()] = value.strip()
    return values


def save_env(user: str, token: str) -> None:
    ENV_PATH.write_text(
        "# Training/train.py 의 GitHub 로그인 정보. git 에 올리지 않는다(Training/.gitignore).\n"
        "# 지우면 다음 실행 때 다시 묻는다.\n"
        f"GITHUB_USER={user}\nGITHUB_TOKEN={token}\n", encoding="utf-8")


def delete_env() -> None:
    ENV_PATH.unlink(missing_ok=True)
    _git_auth_env.clear()


def apply_git_auth(user: str, token: str) -> None:
    basic = base64.b64encode(f"{user}:{token}".encode("utf-8")).decode("ascii")
    _git_auth_env.clear()
    _git_auth_env.update({
        "GIT_CONFIG_COUNT": "2",
        "GIT_CONFIG_KEY_0": "credential.helper",          # 빈 값 = 저장된 도우미 목록을 비운다
        "GIT_CONFIG_VALUE_0": "",
        "GIT_CONFIG_KEY_1": "http.https://github.com/.extraheader",
        "GIT_CONFIG_VALUE_1": f"AUTHORIZATION: basic {basic}",
    })


def mask(token: str) -> str:
    return ("*" * 8 + token[-4:]) if len(token) > 4 else "****"


def verify_git_auth() -> tuple[str, str]:
    """('ok' | 'auth' | 'network', 메시지). push 권한까지 본다 — dry-run push 는 쓰기(receive-pack) 인증을 거친다."""
    result = git("push", "--dry-run", REMOTE, BRANCH)
    text = (result.stdout + result.stderr).strip()
    if result.returncode == 0 or "rejected" in text:     # rejected = 인증은 통과, 원격이 앞서 있을 뿐
        return "ok", ""
    lowered = text.lower()
    if any(k in lowered for k in ("authentication failed", "invalid username", "403", "401",
                                  "could not read username", "permission", "denied")):
        return "auth", text[-400:]
    if any(k in lowered for k in ("could not resolve host", "timed out", "unable to access", "failed to connect")):
        return "network", text[-400:]
    return "auth", text[-400:]


TOKEN_HELP = """
  GitHub 개인 액세스 토큰이 필요하다. (비밀번호가 아니다 — GitHub 은 git 에서 비밀번호 로그인을 막았다.)
    GitHub → Settings → Developer settings → Personal access tokens
      · Fine-grained tokens: Resource owner = HanyangTechAI, Repository access = 2026-2-HAI-ProjectTeam5 만,
                             Permissions → Contents: Read and write, 만료일 지정
      · 또는 Tokens (classic): 'repo' 권한
    조직에 SSO 가 걸려 있으면 토큰 목록에서 'Configure SSO → Authorize' 도 눌러야 한다.
  입력한 토큰은 화면에 보이지 않는다. Training/.env 에만 저장되고 git 에는 올라가지 않는다."""


def prompt_login() -> bool:
    """사용자 이름 · 토큰을 묻고 확인한 뒤 저장한다. 3번 실패하거나 빈칸이면 False."""
    print(TOKEN_HELP)
    for attempt in range(3):
        user = input("\n  GitHub 사용자 이름 (빈칸=취소): ").strip()
        if not user:
            return False
        token = getpass.getpass("  토큰 (입력이 보이지 않는다): ").strip()
        if not token:
            return False
        apply_git_auth(user, token)
        print("  확인 중… (원격 preMerge 에 push 권한이 있는지 본다)")
        status, detail = verify_git_auth()
        if status == "ok":
            save_env(user, token)
            print(f"  로그인 확인 — {user} ({mask(token)}). {rel(ENV_PATH)} 에 저장했다.")
            return True
        if status == "network":
            print(f"  네트워크 문제로 확인하지 못했다. 저장은 하고 진행한다.\n    {detail}")
            save_env(user, token)
            return True
        print(f"  실패({attempt + 1}/3) — 토큰이 틀렸거나, 만료됐거나, 이 저장소에 쓰기 권한이 없다.\n    {detail}")
    _git_auth_env.clear()
    return False


def ensure_github_login(force: bool = False) -> bool:
    """학습 전에 GitHub 로그인을 준비한다. .env 가 유효하면 묻지 않는다."""
    env = load_env()
    user = env.get("GITHUB_USER") or os.environ.get("GITHUB_USER", "")
    token = env.get("GITHUB_TOKEN") or os.environ.get("GITHUB_TOKEN", "")
    if user and token and not force:
        apply_git_auth(user, token)
        status, detail = verify_git_auth()
        if status == "ok":
            print(f"  GitHub 로그인 — {user} ({mask(token)})")
            return True
        if status == "network":
            print(f"  GitHub 에 연결하지 못해 로그인을 확인하지 못했다. 저장된 정보로 진행한다.\n    {detail}")
            return True
        print(f"  저장된 GitHub 로그인이 더 이상 통하지 않는다(만료 또는 권한 변경).\n    {detail}")
    return prompt_login()


# --- git --------------------------------------------------------------------

def git(*args: str, check: bool = False) -> subprocess.CompletedProcess:
    # 비밀번호 창을 띄우고 멈추지 않게 + 로그인 정보(있으면)
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0", **_git_auth_env)
    result = subprocess.run(["git", *args], cwd=ROOT, env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    if check and result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} 실패:\n{result.stdout}{result.stderr}")
    return result


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def git_ready() -> str | None:
    """커밋해도 되는 상태인가. 안 되면 이유를 돌려준다."""
    branch = git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    if branch != BRANCH:
        return f"지금 브랜치가 '{branch}' 다 — '{BRANCH}' 로 바꾸면 그때 커밋한다"
    git_dir = Path(git("rev-parse", "--git-dir").stdout.strip())
    git_dir = git_dir if git_dir.is_absolute() else ROOT / git_dir
    if (git_dir / "MERGE_HEAD").exists() or (git_dir / "rebase-merge").exists() or (git_dir / "rebase-apply").exists():
        return "병합/리베이스가 진행 중이다 — 끝나면 커밋한다"
    return None


def publish(run_dir: Path, git_mode: str) -> None:
    """저장이 끝난 스냅샷과 run.json · 날짜별 기록(.md)을 커밋하고 push 한다. 실패해도 학습은 계속한다.
    git_mode: "push" = 커밋 + push, "commit" = 커밋만, "off" = 아무것도 안 함."""
    if git_mode == "off":
        return
    stable = [d for d in list_snapshots(run_dir) if is_stable(d)]
    if not stable:
        return
    with git_lock():
        reason = git_ready()
        if reason:
            say(f"[git] 커밋 보류: {reason}")
            return

        new = [d for d in stable if not git("ls-files", "--", rel(d)).stdout.strip()]
        paths = [rel(d) for d in stable] + [rel(p) for p in run_dir.glob("*.md")] + [rel(run_dir / "run.json")]
        git("add", "--", *paths, check=True)
        if git("diff", "--cached", "--quiet", "--", *paths).returncode == 0:
            return   # 바뀐 것 없음

        latest = max(stable, key=cumulative_seconds)
        title = f"학습 스냅샷: {run_dir.name} 누적 {hours_minutes(cumulative_seconds(latest))}"
        body = "새 스냅샷: " + (", ".join(d.name for d in sorted(new, key=cumulative_seconds)) or "없음(기록만 갱신)")
        # ★ 경로를 지정해 커밋한다 — 사용자가 따로 스테이지해 둔 다른 파일은 이 커밋에 섞이지 않는다.
        git("commit", "-m", title, "-m", body + "\n\nTraining/train.py 가 자동으로 커밋했다.", "--", *paths, check=True)
        say(f"[git] 커밋: {title}")

        if git_mode == "push":
            push_branch()


def push_branch() -> None:
    result = git("push", REMOTE, BRANCH)
    if result.returncode == 0:
        say(f"[git] push 완료 → {REMOTE}/{BRANCH}")
        return
    # 원격에 다른 사람의 커밋이 있으면 거부된다 → 병합(pull) 후 다시 push.
    # ★ rebase 가 아니라 merge 로 받는다. 사용자의 병합 커밋을 펴 버리지 않는다.
    say("[git] push 거부 — 원격 변경을 받아 병합한 뒤 다시 시도한다")
    pulled = git("pull", "--no-rebase", "--no-edit", REMOTE, BRANCH)
    if pulled.returncode != 0:
        git("merge", "--abort")
        say("[git] 병합 실패(충돌 또는 작업 중인 파일과 겹침). 커밋은 로컬에 남겼다 — 손으로 pull 한 뒤 push 할 것.\n"
            + (pulled.stdout + pulled.stderr).strip()[-800:])
        return
    again = git("push", REMOTE, BRANCH)
    if again.returncode == 0:
        say(f"[git] 병합 후 push 완료 → {REMOTE}/{BRANCH}")
    else:
        say("[git] push 실패. 커밋은 로컬에 있다 — 다음 스냅샷 때 다시 시도한다.\n" + again.stderr.strip()[-800:])


# --- 언리얼 실행 -------------------------------------------------------------

class LogTail:
    """언리얼 로그 파일에서 새 줄만 읽어 학습 리포트 · 에러를 화면에 보여 준다."""

    KEEP = re.compile(r"\[SH_CombatTrainer\]|\[SH_Experiment\]|Error:|Fatal")

    def __init__(self, path: Path) -> None:
        self.path = path
        self.offset = 0

    def pump(self) -> None:
        if not self.path.exists():
            return
        with self.path.open("rb") as f:
            f.seek(self.offset)
            chunk = f.read()
        complete = chunk.rfind(b"\n") + 1      # 마지막 줄이 덜 쓰였으면 다음에 읽는다
        self.offset += complete
        for raw in chunk[:complete].decode("utf-8", errors="replace").splitlines():
            if self.KEEP.search(raw):
                print("    " + re.sub(r"^\[[^\]]*\]\[[^\]]*\]", "", raw).strip(), flush=True)


def build_command(ue_cmd: Path, run_dir: Path, config: dict, resume: Path | None,
                  session_seconds: float | None, log_path: Path) -> list[str]:
    interval = int(config["interval_minutes"] * 60)
    command = [str(ue_cmd), str(UPROJECT), TRAIN_MAP, "-game", "-log", f"-abslog={log_path.as_posix()}",
               "-unattended", "-nosplash", "-nullrhi", "-nosound",
               f"-SHSnapshotRoot={run_dir.as_posix()}", "-SHSnapshotFlat=1", f"-SHSnapshotInterval={interval}"]
    command += config["ue_args"]
    if resume is not None:
        command.append(f"-SHResume={resume.as_posix()}")
    if session_seconds is not None:
        command.append(f"-SHTrainSeconds={int(session_seconds)}")
    return command


def stop_unreal(process: subprocess.Popen) -> None:
    """Ctrl+Break 로 정상 종료를 요청하고, 1분 안에 안 끝나면 강제 종료한다."""
    if process.poll() is not None:
        return
    try:
        process.send_signal(signal.CTRL_BREAK_EVENT)
        process.wait(timeout=60)
    except (subprocess.TimeoutExpired, OSError, KeyboardInterrupt):
        # 기다리는 중에 Ctrl+C 를 한 번 더 눌러도 언리얼이 남지 않게 강제 종료한다.
        process.kill()
        process.wait()


def train(name: str, config: dict, git_mode: str, session_hours: float | None) -> None:
    run_dir = TRAINING_DIR / name
    ue_cmd = find_unreal_cmd()
    session_end = time.time() + session_hours * 3600 if session_hours else None
    quick_crashes = 0

    with run_lock(run_dir):
        while True:
            resume = latest_snapshot(run_dir)
            if resume is None and config.get("initial_snapshot"):
                resume = ROOT / config["initial_snapshot"]
            remaining = None if session_end is None else max(0.0, session_end - time.time())
            if remaining is not None and remaining < 60:
                say("정한 학습 시간을 다 썼다.")
                break

            stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
            log_path = run_dir / "logs" / f"unreal_{stamp}.log"
            log_path.parent.mkdir(parents=True, exist_ok=True)
            command = build_command(ue_cmd, run_dir, config, resume, remaining, log_path)
            say(f"학습 시작 — '{name}', " + (f"이어서: {rel(resume) if resume.is_relative_to(ROOT) else resume} "
                                              f"(누적 {hours_minutes(cumulative_seconds(resume))})"
                                              if resume else "처음부터"))
            say(f"언리얼 로그: {rel(log_path)}")

            started = time.time()
            # 새 프로세스 그룹 — 콘솔의 Ctrl+C 가 언리얼에 바로 가지 않고 이 실행기가 먼저 받는다.
            process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                       creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            tail = LogTail(log_path)
            last_publish = time.time()
            try:
                while process.poll() is None:
                    time.sleep(POLL_SECONDS)
                    tail.pump()
                    if time.time() - last_publish >= PUBLISH_EVERY_SECONDS:
                        last_publish = time.time()
                        publish(run_dir, git_mode)
            except KeyboardInterrupt:
                say("중지 요청 — 언리얼을 종료한다. 마지막 정각 스냅샷 이후의 학습은 저장되지 않는다.")
                stop_unreal(process)
                tail.pump()
                wait_and_publish(run_dir, git_mode)
                return

            tail.pump()
            ran = time.time() - started
            say(f"언리얼 종료(코드 {process.returncode}, {hours_minutes(ran)} 동안 실행).")
            wait_and_publish(run_dir, git_mode)

            if session_end is not None and time.time() >= session_end - 60:
                say("정한 학습 시간을 다 썼다.")
                break
            # 시간 한도 없이 돌다 끝났다 = 비정상 종료. 최신 스냅샷부터 다시 시작한다.
            quick_crashes = quick_crashes + 1 if ran < QUICK_CRASH_SECONDS else 0
            if quick_crashes >= MAX_QUICK_CRASHES:
                say(f"언리얼이 {MAX_QUICK_CRASHES}번 연속으로 {QUICK_CRASH_SECONDS}초 안에 끝났다 — 설정 문제로 보고 멈춘다. "
                    f"위 언리얼 로그를 확인할 것(빌드가 최신인지, 파이썬 학습 환경이 있는지).")
                break
            say(f"{RESTART_DELAY_SECONDS}초 뒤 최신 스냅샷부터 다시 시작한다. 멈추려면 Ctrl+C.")
            try:
                time.sleep(RESTART_DELAY_SECONDS)
            except KeyboardInterrupt:
                return


def wait_and_publish(run_dir: Path, git_mode: str) -> None:
    """종료 직전에 저장된 스냅샷(실행 종료 스냅샷 포함)이 다 쓰일 때까지 기다렸다가 올린다."""
    time.sleep(STABLE_SECONDS + 2)
    publish(run_dir, git_mode)


# --- 설정(run.json) ----------------------------------------------------------

def resolve_initial(value: str) -> str:
    """시작 정책: 다른 학습 이름(그 최신 스냅샷) 또는 스냅샷 폴더 경로. 저장소 기준 상대 경로로 돌려준다.
    잘못된 값이면 ValueError — 메뉴에서는 다시 묻고, 명령줄에서는 종료한다."""
    by_name = TRAINING_DIR / value
    if NAME_RE.match(value) and (by_name / "run.json").exists():
        snapshot = latest_snapshot(by_name)
        if snapshot is None:
            raise ValueError(f"'{value}' 에 아직 스냅샷이 없다.")
        return rel(snapshot)
    path = Path(value.strip('"'))
    path = (path if path.is_absolute() else ROOT / path).resolve()
    if not path.is_dir() or not any(path.glob("*policy.bin")):
        raise ValueError(f"'{value}' 는 학습 이름도, policy.bin 이 있는 스냅샷 폴더도 아니다.")
    return rel(path) if path.is_relative_to(ROOT) else path.as_posix()


def load_config(name: str) -> dict | None:
    path = TRAINING_DIR / name / "run.json"
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None


def create_config(name: str, initial: str | None, note: str, extra_args: list[str], interval_minutes: float) -> dict:
    bad = [a for a in extra_args if a.startswith(RESERVED_PREFIXES)]
    if bad:
        raise ValueError(f"이 옵션은 실행기가 정한다: {' '.join(bad)}")
    config = {
        "name": name,
        "created_at": dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "git_commit": git("rev-parse", "HEAD").stdout.strip(),   # 어떤 코드(보상 함수)로 시작했는지
        "ue_args": DEFAULT_UE_ARGS + list(extra_args),
        "interval_minutes": interval_minutes,
        "initial_snapshot": initial,
        "note": note,
    }
    run_dir = TRAINING_DIR / name
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "run.json").write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return config


def check_name(name: str) -> str | None:
    """쓸 수 없는 이름이면 이유를 돌려준다."""
    if not NAME_RE.match(name) or name in RESERVED_NAMES:
        return "영문 · 숫자 · _ · - 만, 64자 이내로 적는다(예: A, dist_x2, aim-assist)."
    return None


# --- 현황 -------------------------------------------------------------------

def run_dirs() -> list[Path]:
    if not TRAINING_DIR.is_dir():
        return []
    return sorted((p for p in TRAINING_DIR.iterdir() if (p / "run.json").exists()), key=lambda p: p.name.lower())


def is_running(run_dir: Path) -> bool:
    lock = run_dir / ".train.lock"
    try:
        return process_alive(int(lock.read_text().strip()))
    except (FileNotFoundError, ValueError):
        return False


def git_file_sets(run_dir: Path) -> tuple[set[str], set[str]]:
    """(로컬에 커밋된 파일, 원격 preMerge 에 있는 파일). 원격은 마지막 fetch · pull · push 기준이다."""
    local = set(git("ls-files", "--", rel(run_dir)).stdout.split())
    remote = set(git("ls-tree", "-r", "--name-only", f"{REMOTE}/{BRANCH}", "--", rel(run_dir)).stdout.split())
    return local, remote


def snapshot_state(snapshot: Path, local: set[str], remote: set[str]) -> str:
    policy = next(snapshot.glob("*_policy.bin"), None)
    key = rel(policy) if policy else ""
    if key in remote:
        return "push 됨"
    if key in local:
        return "커밋됨(로컬)"
    return "커밋 전"


def saved_at(snapshot: Path) -> str:
    value = read_meta(snapshot).get("saved_at")
    if value:
        return value[:16]
    return dt.datetime.fromtimestamp(snapshot.stat().st_mtime).strftime("%Y-%m-%d %H:%M")


def text_width(text: str) -> int:
    """콘솔에서 차지하는 칸 수. 한글은 2칸이라 len() 으로 맞추면 표가 어긋난다."""
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in text)


def print_table(headers: list[str], rows: list[list[str]]) -> None:
    widths = [max(text_width(str(r[i])) for r in [headers] + rows) for i in range(len(headers))]

    def line(cells: list[str]) -> str:
        return "  " + "  ".join(str(c) + " " * (w - text_width(str(c))) for c, w in zip(cells, widths))

    print(line(headers))
    print("  " + "  ".join("-" * w for w in widths))
    for row in rows:
        print(line(row))


RUN_HEADERS = ["번호", "이름", "상태", "누적", "스냅샷", "마지막 저장", "push", "메모"]


def run_rows(runs: list[Path]) -> list[list[str]]:
    rows = []
    for index, run_dir in enumerate(runs, start=1):
        config = load_config(run_dir.name) or {}
        snapshots = list_snapshots(run_dir)
        latest = max(snapshots, key=cumulative_seconds) if snapshots else None
        local, remote = git_file_sets(run_dir)
        pushed = sum(1 for s in snapshots if snapshot_state(s, local, remote) == "push 됨")
        rows.append([
            str(index), run_dir.name,
            "학습 중" if is_running(run_dir) else "대기",
            hours_minutes(cumulative_seconds(latest)) if latest else "-",
            str(len(snapshots)),
            saved_at(latest) if latest else "-",
            f"{pushed}/{len(snapshots)}" if snapshots else "-",
            (config.get("note") or "")[:30],
        ])
    return rows


def show_runs(runs: list[Path]) -> bool:
    if not runs:
        print("\n  아직 학습이 없다. 메뉴 1번으로 새 학습을 시작한다.")
        return False
    print()
    print_table(RUN_HEADERS, run_rows(runs))
    return True


def show_detail(run_dir: Path) -> None:
    config = load_config(run_dir.name) or {}
    print(f"\n  ■ {run_dir.name}  ({'학습 중' if is_running(run_dir) else '대기'})")
    print(f"    만든 때     : {config.get('created_at', '-')}")
    print(f"    시작 코드   : {config.get('git_commit', '-')[:10]}  (이 커밋의 보상 함수로 시작했다)")
    print(f"    시작 정책   : {config.get('initial_snapshot') or '처음부터'}")
    print(f"    언리얼 옵션 : {' '.join(config.get('ue_args', []))}")
    print(f"    스냅샷 주기 : {config.get('interval_minutes', 60):g}분")
    print(f"    메모        : {config.get('note') or '-'}")
    print(f"    로그        : {rel(run_dir / 'logs')}")

    snapshots = sorted(list_snapshots(run_dir), key=cumulative_seconds)
    if not snapshots:
        print("\n    스냅샷 없음")
        return
    local, remote = git_file_sets(run_dir)
    print()
    print_table(["누적", "저장 시각", "폴더", "git"],
                [[hours_minutes(cumulative_seconds(s)), saved_at(s), rel(s), snapshot_state(s, local, remote)]
                 for s in snapshots])


# --- 대화형 메뉴 -------------------------------------------------------------

class Cancel(Exception):
    """메뉴에서 빈칸 · 0 으로 뒤로 가기."""


def ask(prompt: str, default: str | None = None) -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"  {prompt}{suffix}: ").strip()
    return value if value else (default or "")


def ask_number(prompt: str, count: int) -> int:
    """1..count 중 하나. 빈칸 또는 0 이면 뒤로."""
    while True:
        value = ask(f"{prompt} (1~{count}, 0=뒤로)")
        if value in ("", "0"):
            raise Cancel
        if value.isdigit() and 1 <= int(value) <= count:
            return int(value)
        print("    번호를 다시 입력한다.")


def ask_yes(prompt: str, default: bool = True) -> bool:
    value = ask(f"{prompt} ({'Y/n' if default else 'y/N'})").lower()
    return default if not value else value in ("y", "yes", "ㅛ")


def ask_hours() -> float | None:
    while True:
        value = ask("이번 실행 시간(시간 단위, 빈칸=Ctrl+C 로 멈출 때까지)")
        if not value:
            return None
        try:
            hours = float(value)
            if hours > 0:
                return hours
        except ValueError:
            pass
        print("    0 보다 큰 숫자로 적는다(예: 8, 1.5).")


GIT_MODE_TEXT = {"push": "preMerge 에 커밋 + push", "commit": "커밋만", "off": "하지 않음"}


def ask_git_mode() -> str:
    print("\n  스냅샷 올리기:")
    print("    1) preMerge 에 커밋 + push  (기본)")
    print("    2) 커밋만 (push 는 나중에 손으로)")
    print("    3) 하지 않음 (시험용)")
    value = ask("선택", "1")
    return {"2": "commit", "3": "off"}.get(value, "push")


def menu_new() -> tuple[str, dict, str, float | None]:
    print("\n[새 학습]  보상 함수 등을 바꿨으면 새 이름으로 학습한다. 기존 학습 폴더는 그대로 남는다.")
    print("  ★ C++ 을 바꿨다면 먼저 빌드했는지 확인할 것 — 이 실행기는 빌드하지 않는다.")
    while True:
        name = ask("학습 이름 (빈칸=뒤로)")
        if not name:
            raise Cancel
        problem = check_name(name)
        if problem:
            print(f"    {problem}")
        elif load_config(name):
            print(f"    '{name}' 은(는) 이미 있다. 이어서 하려면 메뉴 2번, 새로 하려면 다른 이름.")
        else:
            break

    runs = [r for r in run_dirs() if latest_snapshot(r)]
    print("\n  시작 정책:")
    print("    1) 처음부터 (무작위 초기화)")
    print("    2) 기존 학습의 최신 정책에서" + ("" if runs else "  — 스냅샷이 있는 학습이 아직 없다"))
    print("    3) 스냅샷 폴더 경로를 직접 입력 (예: Snapshots/2026-10-25/cum016h00m)")
    initial = None
    while True:
        choice = ask("선택", "1")
        try:
            if choice == "2" and runs:
                show_runs(runs)
                initial = resolve_initial(runs[ask_number("어느 학습에서", len(runs)) - 1].name)
            elif choice == "3":
                initial = resolve_initial(ask("스냅샷 폴더"))
            elif choice != "1":
                print("    1~3 중에서 고른다.")
                continue
            break
        except ValueError as error:
            print(f"    {error}")
        except Cancel:
            continue

    note = ask("메모 — 무엇을 바꿨는지 (빈칸 가능)")
    print(f"\n  기본 언리얼 옵션: {' '.join(DEFAULT_UE_ARGS)}")
    while True:
        extra = ask("추가 옵션 (예: -SHApproach=1, 빈칸=없음)").split()
        bad = [a for a in extra if a.startswith(RESERVED_PREFIXES)]
        if not bad:
            break
        print(f"    이 옵션은 실행기가 정한다: {' '.join(bad)}")
    interval = 60.0
    hours = ask_hours()
    git_mode = ask_git_mode()

    print("\n  ── 확인 ──")
    print(f"    이름        : {name}  →  {rel(TRAINING_DIR / name)}/")
    print(f"    시작 정책   : {initial or '처음부터'}")
    print(f"    메모        : {note or '-'}")
    print(f"    언리얼 옵션 : {' '.join(DEFAULT_UE_ARGS + extra)}")
    print(f"    실행 시간   : {f'{hours:g}시간' if hours else 'Ctrl+C 로 멈출 때까지'}")
    print(f"    스냅샷      : {interval:g}분마다, {GIT_MODE_TEXT[git_mode]}")
    if not ask_yes("시작할까"):
        raise Cancel
    config = create_config(name, initial, note, extra, interval)
    return name, config, git_mode, hours


def menu_resume() -> tuple[str, dict, str, float | None]:
    runs = run_dirs()
    print("\n[기존 학습 이어서]  고른 학습의 최신 스냅샷부터, 처음과 같은 설정으로 이어서 학습한다.")
    if not show_runs(runs):
        raise Cancel
    run_dir = runs[ask_number("이어서 할 학습", len(runs)) - 1]
    if is_running(run_dir):
        print(f"    '{run_dir.name}' 은(는) 이미 다른 창에서 학습 중이다.")
        raise Cancel
    latest = latest_snapshot(run_dir)
    print(f"\n    '{run_dir.name}': " + (f"누적 {hours_minutes(cumulative_seconds(latest))} 부터 이어서" if latest
                                         else "스냅샷이 없어 처음 설정대로 시작"))
    hours = ask_hours()
    git_mode = ask_git_mode()
    if not ask_yes("시작할까"):
        raise Cancel
    return run_dir.name, load_config(run_dir.name), git_mode, hours


def menu_status() -> None:
    runs = run_dirs()
    print("\n[학습 현황]  push 열 = 원격 preMerge 에 올라간 스냅샷 수 / 전체 (마지막 fetch · push 기준)")
    if not show_runs(runs):
        return
    while True:
        try:
            show_detail(runs[ask_number("\n  자세히 볼 학습", len(runs)) - 1])
        except Cancel:
            return


def menu_login() -> None:
    env = load_env()
    print("\n[GitHub 로그인]  스냅샷을 preMerge 에 push 할 때 쓴다.")
    if env.get("GITHUB_USER") and env.get("GITHUB_TOKEN"):
        print(f"    저장됨: {env['GITHUB_USER']} ({mask(env['GITHUB_TOKEN'])})  →  {rel(ENV_PATH)}")
    else:
        print("    저장된 로그인 없음")
    print("\n    1) 확인만 하기 (지금 토큰으로 push 권한이 있는지)")
    print("    2) 새로 입력 / 바꾸기")
    print("    3) 지우기")
    choice = ask("선택 (0=뒤로)", "0")
    if choice == "1":
        if not (env.get("GITHUB_USER") and env.get("GITHUB_TOKEN")):
            print("    저장된 로그인이 없다.")
            return
        apply_git_auth(env["GITHUB_USER"], env["GITHUB_TOKEN"])
        status, detail = verify_git_auth()
        print("    통과 — push 할 수 있다." if status == "ok" else f"    {status}: {detail}")
    elif choice == "2":
        prompt_login()
    elif choice == "3" and ask_yes("저장된 로그인을 지울까", default=False):
        delete_env()
        print("    지웠다.")


def interactive() -> tuple[str, dict, str, float | None] | None:
    while True:
        env = load_env()
        login = env.get("GITHUB_USER") if env.get("GITHUB_TOKEN") else None
        print("\n" + "=" * 56)
        print("  TEAM5 자동 학습   스냅샷 → Training/<이름>/ , git preMerge")
        print(f"  GitHub 로그인: {login or '없음 (push 하는 학습을 시작할 때 묻는다)'}")
        print("=" * 56)
        print("  1) 새 학습 시작")
        print("  2) 기존 학습 이어서")
        print("  3) 학습 현황 보기")
        print("  4) GitHub 로그인 설정")
        print("  0) 종료")
        choice = ask("선택")
        try:
            if choice == "1":
                return menu_new()
            if choice == "2":
                return menu_resume()
            if choice == "3":
                menu_status()
            elif choice == "4":
                menu_login()
            elif choice in ("0", "q", "Q"):
                return None
        except Cancel:
            continue


# --- 시작 -------------------------------------------------------------------

def start(name: str, config: dict, git_mode: str, hours: float | None, force_login: bool = False) -> None:
    global _log_file
    run_dir = TRAINING_DIR / name

    # 학습 전에 GitHub 로그인을 준비한다. 학습 도중(몇 시간 뒤)에 로그인 창이 떠서 멈추는 일이 없게.
    if git_mode == "push":
        print("\n  GitHub 로그인 확인 중…")
        if not ensure_github_login(force=force_login):
            print("  로그인 없이 진행한다 — 스냅샷은 로컬에 커밋만 하고 push 하지 않는다. (메뉴 4번에서 나중에 설정)")
            git_mode = "commit"

    _log_file = run_dir / "logs" / "train_py.log"
    _log_file.parent.mkdir(parents=True, exist_ok=True)

    resume = latest_snapshot(run_dir)
    if resume:
        origin = f"이어서 — {rel(resume)} (누적 {hours_minutes(cumulative_seconds(resume))})"
    elif config.get("initial_snapshot"):
        origin = f"{config['initial_snapshot']} 의 정책에서"
    else:
        origin = "처음부터"
    print("\n" + "-" * 56)
    print(f"  학습 '{name}' 시작")
    print(f"    시작 정책   : {origin}")
    print(f"    실행 시간   : {f'{hours:g}시간' if hours else '제한 없음 — 이 창에서 Ctrl+C 로 멈춘다'}")
    print(f"    스냅샷      : {config.get('interval_minutes', 60):g}분마다 → {rel(run_dir)}/cumXXXhYYm/")
    print(f"    git         : {GIT_MODE_TEXT[git_mode]}")
    print(f"    언리얼 옵션 : {' '.join(config['ue_args'])}")
    print("-" * 56)
    say(f"설정: {json.dumps(config, ensure_ascii=False)} / git={git_mode} / hours={hours}")
    train(name, config, git_mode=git_mode, session_hours=hours)
    say("끝.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="자동 학습 + 정해진 주기마다 스냅샷 커밋/push. 인자 없이 실행하면 메뉴가 뜬다. (Training/README.md)")
    parser.add_argument("name", nargs="?", help="학습 이름(Training/<이름>/). 없으면 새로, 있으면 이어서")
    parser.add_argument("--from", dest="initial", help="새 학습의 시작 정책: 다른 학습 이름 또는 스냅샷 폴더")
    parser.add_argument("--note", default="", help="새 학습의 메모(무엇을 바꿨는지)")
    parser.add_argument("--hours", type=float, help="이번 실행 시간(시간). 생략하면 Ctrl+C 까지 계속")
    parser.add_argument("--interval-minutes", type=float, default=60.0, help="새 학습의 스냅샷 주기(분). 기본 60")
    parser.add_argument("--no-push", action="store_true", help="커밋만 하고 push 하지 않는다")
    parser.add_argument("--no-git", action="store_true", help="커밋도 push 도 하지 않는다(시험용)")
    parser.add_argument("--list", action="store_true", help="학습 현황만 보여 준다")
    parser.add_argument("--login", action="store_true", help="GitHub 로그인(토큰)을 새로 입력한다")
    parser.add_argument("ue_args", nargs="*", help="새 학습에만: 언리얼 추가 옵션(예: -SHApproach=1). '--' 뒤에 적는다")
    args = parser.parse_args()

    # train.bat 이 콘솔을 UTF-8(chcp 65001)로 바꾼다. 출력도 UTF-8 로 맞춰야 한글이 깨지지 않는다.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    TRAINING_DIR.mkdir(exist_ok=True)

    if args.list:
        show_runs(run_dirs())
        return
    if args.login and not args.name:
        prompt_login()
        return

    if not args.name:
        try:
            picked = interactive()
        except (KeyboardInterrupt, EOFError):
            picked = None
        if picked is None:
            print("\n  종료.")
            return
        start(*picked)
        return

    # 명령줄 모드: train.bat A [--from ...] [--hours ...]
    problem = check_name(args.name)
    if problem:
        raise SystemExit(problem)
    config = load_config(args.name)
    if config is None:
        try:
            initial = resolve_initial(args.initial) if args.initial else None
            config = create_config(args.name, initial, args.note, args.ue_args, args.interval_minutes)
        except ValueError as error:
            raise SystemExit(str(error))
    elif args.initial or args.ue_args:
        print("  이미 있는 학습이라 --from / 추가 옵션은 무시하고 처음 설정(run.json)으로 이어서 학습한다. "
              "설정을 바꾸려면 새 이름을 쓴다.")
    git_mode = "off" if args.no_git else ("commit" if args.no_push else "push")
    start(args.name, config, git_mode, args.hours, force_login=args.login)


if __name__ == "__main__":
    main()
