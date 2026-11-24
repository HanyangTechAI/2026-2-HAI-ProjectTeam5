# 복도 학습 전후 모델 비교 평가

2026년 11월 23일, 복도 학습 전후 모델을 같은 고정 상대와 로컬에서 각각 30판씩 평가했다. 학습 전 모델은 16승 14패, 학습 후 모델은 30승 0패를 기록했다. 이번 복도 맵과 고정 상대 조건에서는 학습 후 모델이 우세했다.

## 진행 내용

지정한 7번 base Pod에 SSH로 접속해 Unreal 실행 파일과 프로젝트 배포 폴더를 확인했다. 접속 당시 기존 학습이 진행 중이었고 요청한 체크포인트 3개는 확인한 배포 경로에 없었다. 기존 학습을 유지하고, 저장소에 있는 모델과 복도 맵으로 로컬 평가를 준비했다. 이번 작업에서 Pod 생성·시작·중지·재시작이나 새 학습은 수행하지 않았다.

UE 5.8.2로 별도 평가용 프로젝트를 빌드했다. Unity 빌드에서 내부 함수명이 충돌해 평가용 복사본의 `FormatHoursMinutes`를 `FormatTrainerHoursMinutes`로 변경했다. 게임 규칙과 모델 로직은 변경하지 않았다. 수정 내용은 [빌드 호환 패치](Evaluation/20261123-corridor/unity-build-compatibility.patch)에 보관했다.

먼저 모델당 2판으로 맵과 모델 로드를 점검했고 두 모델 모두 2승 0패를 기록했다. 이후 공통 시드 3개로 모델당 30판까지 확대했다. 아래 결과는 확대 평가 60판만 집계한 것이다. 기존 base Pod 학습의 목표 스텝 정상 완료 여부는 이 평가 결과와 별개이며 최종 확인되지 않았다.

## 평가 조건

| 항목 | 조건 |
|---|---|
| 실행 환경 | Windows 로컬 UE 5.8.2, `UnrealEditor-Cmd.exe`, 화면 없이 실행 |
| 레벨과 아레나 | `Lvl_SHTrain`, `BP_SHArena_Corridor` |
| 복도 구조 | ㄱ자, 다리 길이 2200 cm, 폭 1100 cm |
| 전투 규모 | 아레나 1개, 팀당 10유닛 |
| 평가 모델과 상대 | 아래 체크포인트 3개 고정 |
| 경기 수 | 모델당 시드별 10판, 총 30판씩 |
| 공통 시드 | `1234`, `2345`, `3456` |
| 진영 | 매 라운드 교대, 모델당 각 진영에서 15판씩 |
| 평가 모드 | `-SHEval`, `-SHEvalOpponent`, 행동 노이즈 0, 조준 보조 끔 |
| 시간 진행 | 고정 델타타임, `-SHEvalRealtime=0` |

| 역할 | 저장소 기준 체크포인트 경로 |
|---|---|
| 학습 전 | `Training/START_1116_NEW_OPPONENT/cum020h06m` |
| 학습 후 | `Training/START_1120_corridor/cum026h06m` |
| 고정 상대 | `Training/START_11_13/cum017h06m` |

소스와 모델 기준 커밋은 [`23a0da0df2162adcc5c2e50f27abac64c7562f27`](https://github.com/HanyangTechAI/2026-2-HAI-ProjectTeam5/commit/23a0da0df2162adcc5c2e50f27abac64c7562f27)이다. 학습 후 체크포인트의 메타데이터는 학습 전 `cum020h06m`에서 이어 학습했음을 기록한다. 세 체크포인트 모두 관측 크기는 176이다. 모델 바이너리는 기존 저장소 자료를 사용하며 이번 보고에 중복 추가하지 않는다.

## 평가 결과

| 지표 | 학습 전 20h06m | 학습 후 26h06m |
|---|---:|---:|
| 경기 수 | 30 | 30 |
| 승패 | 16승 14패 | 30승 0패 |
| 승률 | 53.33% | 100.00% |
| 평균 게임 내 라운드 시간 | 약 14.42초 | 약 8.79초 |
| 발사 수 | 4,778 | 6,837 |
| 명중 수 | 2,715 | 3,000 |
| 명중률 | 56.82% | 43.88% |
| 시간 초과 | 0 | 0 |
| 동시 전멸 | 0 | 0 |

| 시드 | 학습 전 | 학습 후 |
|---|---:|---:|
| 1234 | 4승 6패 | 10승 0패 |
| 2345 | 6승 4패 | 10승 0패 |
| 3456 | 6승 4패 | 10승 0패 |

학습 후 모델의 승률은 46.67%p 높았고 평균 라운드 시간은 약 39% 짧았다. 라운드 시간은 로그에 반올림되어 출력되는 구간 평균을 가중 집계한 근삿값이며, 엔진 시작 시간을 포함하지 않는다. 명중률은 낮아졌으므로 승률 향상을 사격 정확도 향상으로 해석할 수는 없다. 발사량이 증가한 원인과 전술 변화는 추가 분석이 필요하다.

이 결과는 해당 복도 맵에서 고정 상대 `17h06m`와 진행한 3개 시드의 비교다. 다른 맵이나 상대에 대한 성능은 평가하지 않았다. 시드를 맞췄더라도 엔진 전체의 완전한 결정론을 보장하지 않는다.

## 로드 검증과 기록

6개 실행 모두 로그와 실행 후 설정에서 평가 모드, 두 정책의 체크포인트, 복도 클래스와 난수 시드를 확인했다. 총 60판이 지정한 라운드 수까지 종료됐다. 평가 전후 입력 파일 18개의 Git blob SHA와 크기가 동일했다.

- [집계 결과와 실행별 결과](Evaluation/20261123-corridor/results.json)
- [완료 및 입력 유지 검증](Evaluation/20261123-corridor/verification.json)
- [맵과 모델 로드 및 경기 결과 로그 발췌](Evaluation/20261123-corridor/evidence.log)
- [입력 파일별 크기와 Git blob SHA](../Tools/corridor_inputs_manifest.json)

공유용 결과에서는 개인 PC 절대 경로를 제거했다. 로그 발췌에는 원본 파일명, 원본 줄 번호, 원본 전체 로그의 SHA-256을 남겼고 로컬 경로 접두사만 치환했다. 전체 원본 로그와 실행 명령 및 설정 경로는 평가 PC의 저장소 기준 `Saved/RunPodEvaluation/local-10x3seeds-20261123-184522/`에 보관한다. `Saved` 폴더는 Git 추적 대상이 아니다.

## 로컬 재현 방법

Python 3.9 이상과 UE 5.8.2 빌드 환경이 필요하다. 별도 프로젝트를 위 기준 커밋으로 준비하고 [빌드 호환 패치](Evaluation/20261123-corridor/unity-build-compatibility.patch)를 해당 프로젝트에 적용한다. 세 체크포인트는 프로젝트 아래 `Training/`에 둔다. 다른 위치에 보관했다면 `--inputs`에 `Training/`의 상위 폴더를 지정한다.

아래 명령은 이 보고서가 있는 저장소 루트에서 실행한다. `$engineRoot`와 `$projectRoot`는 각 PC의 실제 위치로 바꾼다. `$projectRoot`는 기준 커밋으로 준비한 별도 프로젝트 폴더다.

```powershell
$engineRoot = 'D:\UE_5.8'
$projectRoot = 'D:\work\corridor-evaluation'
$projectFile = Join-Path $projectRoot 'HAI_Project_TEAM5.uproject'
$editorExe = Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

git -C $projectRoot apply --check "$PWD\Docs\Evaluation\20261123-corridor\unity-build-compatibility.patch"
git -C $projectRoot apply "$PWD\Docs\Evaluation\20261123-corridor\unity-build-compatibility.patch"
& "$engineRoot\Engine\Build\BatchFiles\Build.bat" HAI_Project_TEAM5Editor Win64 Development "-Project=$projectFile" -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2

python Tools/run_corridor_comparison.py --editor "$editorExe" --project "$projectFile" --inputs "$projectRoot" --rounds 10 --seeds 1234 2345 3456 --dry-run
python Tools/run_corridor_comparison.py --editor "$editorExe" --project "$projectFile" --inputs "$projectRoot" --rounds 10 --seeds 1234 2345 3456
```

[평가 스크립트](../Tools/run_corridor_comparison.py)는 입력 해시를 검증하고 모델을 하나씩 순서대로 실행한다. `--dry-run`은 입력을 검증하고 명령만 출력하며 Unreal을 시작하지 않는다. 일반 실행은 평가 전용이며 학습을 시작하지 않는다. 새 결과는 기본적으로 평가 프로젝트의 `Saved/CorridorEvaluation/`에 저장한다.

실행 인자 `-SHTrainSeconds=720`은 기존 학습·평가 공용 실행 시간 상한이다. `-SHEval`로 평가 모드를 사용하며 10라운드에 도달하면 종료한다. 공유용 재현 스크립트는 원래 평가 스크립트에서 경로를 지정할 수 있도록 정리한 것이다. 공유 준비 시에는 `--dry-run`으로 확인했으며 60판을 다시 실행하지 않았다.
