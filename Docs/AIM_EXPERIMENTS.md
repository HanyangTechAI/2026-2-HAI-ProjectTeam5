# 조준 보조·보상 실험 기능

기본 동작은 기존과 같다: 조준 보조 끔, 정렬/시야 보상 켬, 보상 배율 1.
관측·행동 스키마(`Turn / Move / Fire`)는 바꾸지 않는다. 이번 기능 추가로 자동 학습을 시작하지 않는다.

## 설정

레벨의 `SH_CombatTrainer` 액터 Details → `SH / RL / AimExperiment`에서 설정하거나 실행 인자를 사용한다.
실행 인자가 레벨 값을 우선한다. 실행 중 전환은 지원하지 않으며, 다른 조건은 별도 실행으로 비교한다.

| 실행 인자 | 기본값 | 의미 |
|---|---|---|
| `-SHAimAssist=0/1` | 0 | 학습 정책이 제어하는 유닛의 조준 보조 on/off |
| `-SHAimAssistStrength=0..1` | 1 | 0: 정책 회전, 1: 보조 회전, 중간: 두 각속도 선형 혼합 |
| `-SHAimReward=0/1` | 1 | 중앙 정렬 보상(`AlignWeight`) on/off |
| `-SHAimRewardScale=<0 이상>` | 1 | 정렬 보상 배율. 스위치 off이면 배율과 관계없이 0 |
| `-SHSeeReward=0/1` | 1 | 적이 보이는 보상(`SeeWeight`) 독립 on/off |
| `-SHSeeRewardScale=<0 이상>` | 1 | 시야 보상 배율 |
| `-SHExperiment=<이름>` | 빈 문자열 | 기록용 이름. 파일 경로로 사용하지 않음 |
| `-SHSeed=<0 이상 정수>` | 1234 | 정책/상대 혼합 시드. 평가의 `SHEvalSeed`가 있으면 그것이 우선 |

보조는 같은 시야 스캔에서 보이는 가장 가까운 적의 현재 각도를 사용한다. 시야각·거리·차폐 판정은
관측과 같다. 목표 각도 / 결정 주기를 계산하고 `MaxTurnRateDeg`로 제한한 뒤 정책 회전과 혼합한다.
실제 회전은 기존 캐릭터 Tick에서 이루어지며 순간 회전이나 명중 보장은 없다.
적이 보이지 않으면 탐색은 정책의 `Turn`에 맡긴다. 이동과 발사 행동, 총 쿨다운은 유지한다.
보조는 스냅샷 상대와 스크립트 봇에는 적용하지 않으며, self-play에서는 학습 정책을 공유하는 양 팀에 적용한다.

조준 방향은 시야와 이동 방향의 기준이기도 하므로 보조는 이 두 부분에도 간접적으로 영향을 준다.
보조 중에도 정책이 뽑은 행동 기록은 덮어쓰지 않는다. 조준 헤드를 별도로 동결하거나 모방학습시키지 않는다.
보조 100%에서 정책 조준이 실전에 영향을 주지 않는 동안 조준 능력이 학습된다고 가정하면 안 된다.

## 기록 파일

조립 성공 경로에서 `Saved/SHExperiments/<시각>-<고유 ID>/`를 만든다. 학습·평가 모두 기록한다.

- `settings.json`: 실험 이름, 조준 설정, 모드/맵/시드, 시작 스냅샷, 스키마 크기, PPO·게임·프로세스 설정,
  보상 가중치·램프·감쇠, 아레나별 팀 크기·구역·라운드 규칙 등 적용된 설정.
- `rewards.csv`: 기존 `SHReportInterval`(기본 15초)마다 **그 구간의 부호 있는 보상 합계**를 추가한다.
  정상 종료 시 마지막 구간도 기록한다. 샘플이 없으면 행을 쓰지 않는다.
- Unreal 로그의 `[SH_Experiment]`: 적용 설정과 기록 경로.
- Unreal 로그의 `[SH_Rewards]`: 보상 수집 1,000회당 항목별 값과 보조 적용 결정 수.

학습 스냅샷에는 `<이름>_experiment.json`을 복사하고 `_meta.txt`에 실험 경로와 조준 설정을 남긴다.
날짜별 스냅샷 Markdown 머리말에도 조준 설정과 실험 경로를 남긴다.
설정 기록은 실행 입력을 복원하기 위한 자료이며 모든 게임 난수의 결정론이나 소스 전체의 보존을 보장하지 않는다.

### 보상 CSV 해석

`raw_damage_dealt`, `raw_damage_taken`, `raw_kill`, `raw_miss`, `raw_death`, `raw_time`,
`raw_see`, `raw_align`은 가중치·감쇠·on/off를 적용한 **팀 혼합 전 개인 보상**이다.
벌점은 음수다. `raw_align=0`은 정렬 보상 off 또는 배율 0에서 보장된다.

최종적으로 정책에 전달한 보상은 다음처럼 분리해 기록한다.

```text
individual_applied = Σ (1 - τ) × 개인 보상
team_applied       = Σ τ × 해당 아레나·팀의 개인 보상 평균
total             ≈ individual_applied + team_applied + outcome + clock
```

합산에는 float 반올림 오차가 있다. `raw_*`와 `team_applied`를 함께 더하면 중복 계산이 된다.
`samples`는 죽어서 라운드 종료를 기다리는 에이전트까지 포함한 보상 수집 횟수다.
CSV 값에 `1000 / samples`를 곱하면 콘솔의 `/1k` 값이다.
`assisted_decisions / live_decisions`는 살아서 행동한 결정 중 실제 보조 대상이 있었던 비율이다.
샘플 분모가 다르므로 이 비율을 보상 수집 횟수로 나누지 않는다.

## 실행 예시

아래 인자는 기존 프로젝트·맵 실행 명령 뒤에 추가한다. 이 문서 작성 과정에서 장기 학습은 실행하지 않는다.

```text
# 기존 조건
-SHExperiment=baseline -SHAimAssist=0 -SHAimReward=1

# 정렬 보상만 제거 (시야 보상은 유지)
-SHExperiment=no-align -SHAimAssist=0 -SHAimReward=0

# 50% 보조 + 정렬 보상 제거
-SHExperiment=assist-half -SHAimAssist=1 -SHAimAssistStrength=0.5 -SHAimReward=0

# 정렬·시야 보상 모두 제거
-SHExperiment=no-shaping -SHAimReward=0 -SHSeeReward=0
```

추후 비교 시 학습 예산·시작 정책·상대 조건을 맞추고 여러 시드를 사용한다. 최종 평가는
`-SHEval=<스냅샷 폴더> -SHAimAssist=0`으로 별도 실행한다. 이동 비율 변화만으로 positioning 향상을 판정하지 않는다.

## 학습 없는 동작 검증

빌드된 에디터와 저장된 정책으로 `Tools/verify_aim_experiments.py`를 실행한다.
이 스크립트는 `-SHEval` 추론만 사용하며 PPO/Python 학습기를 기동하지 않는다.

```powershell
& '.\Intermediate\PipInstall\Scripts\python.exe' Tools/verify_aim_experiments.py --editor 'D:\unreal engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
```

기본/정렬 off/전체 셰이핑 off/보조 강도 0·0.5·1 조건의 실제 실행 기록과 보상 합산을 검증한다.
결과는 `Saved/AimExperimentVerification/` 아래에 보존한다. 이는 기능 검증이며 학습 성능 검증은 아니다.

### 2026-11-09 검증 결과

- UE 5.8.2, Win64 Development Editor C++ 빌드 성공.
- 위 6개 조건 모두 추론 실행 정상 종료. 각 조건의 JSON 설정과 CSV 5개 구간(총 30행)을 확인했다.
- 정렬 off에서 `raw_align=0`, 시야 off에서 `raw_see=0` 확인.
- 보조 끔/강도 0에서는 적용 0회, 강도 0.5에서는 635회, 강도 1에서는 758회 적용 확인.
  실행별 게임 진행이 다르므로 이 횟수를 강도별 성능 비교로 해석하지 않는다.
- 개인 원보상 합과 팀 혼합 후 합, 적용 항목 합과 최종 보상 합의 일치를 반올림 허용 오차 내에서 확인.
- 검증 결과: `Saved/AimExperimentVerification/20261109-002500/results.json`.
- 에디터 Python 도구 플러그인의 기존 초기화 오류를 피하도록 검증 실행에는 `-DisablePython`을 사용했다.
  검증은 저장된 정책의 추론만 수행했으며 PPO 학습·성능 평가·패키징은 실행하지 않았다.
