# RL 학습 TODO

> HAI 2026-1 TEAM 5 / UE 5.8 / LearningAgents 0.2 (experimental)
> 최종 갱신: **2026-09-17**
> 상세 설계는 별도 계획 문서 참조. 이 문서는 **무엇이 끝났고 다음에 뭘 하는지**만 적는다.

---

## 0. 한 줄 요약

**RL 배관은 오늘 처음으로 끝까지 돌았다.** 남은 일은 그 배관에 슈터를 연결하는 것이다.
그 전에 **"어디까지 RL로 돌릴지"를 확정해야 한다** (5장). 스키마 해시가 그 결정에 묶여 있어서
나중에 바꾸면 학습한 가중치를 버려야 한다.

---

## 1. 오늘 한 것 (2026-09-17)

### 1.1 GPU 학습 활성화 — RTX 5080

이전 판정("LearningAgents는 RTX 50 시리즈에서 GPU 학습 불가")을 **철회**했다.

엔진이 `torch==2.5.1+cu124`를 핀으로 박는데 cu124 휠에는 Blackwell(**sm_120**) 커널이 없다.
`PythonMLPackages` / `PythonFoundationPackages` 두 플러그인을 **프로젝트로 복사**해서
(`Plugins/` — 둘 다 C++ 모듈이 0개라 컴파일 비용 없음) cu128로 갈아끼웠다.

| 항목 | 이전 | 지금 |
|---|---|---|
| torch | `2.5.1+cu124` | `2.8.0+cu128` |
| torchvision | `0.20.1+cu124` | `0.23.0+cu128` |
| torchaudio | `2.5.1+cu124` | `2.8.0+cu128` |
| sympy | `1.13.1` | `1.13.3` |
| 인덱스 | `whl/cu124/` | `whl/cu128/` |

검증: `2.8.0+cu128` / `cuda True` / `NVIDIA GeForce RTX 5080` / capability `(12, 0)` /
`arch_list`에 `sm_120` 포함 / 4096³ matmul 6.9 ms/iter.

> ⚠️ **보안 저하 사항 — 팀 전체가 알아야 한다.**
> 새 버전의 해시를 직접 채우지 않았으므로 `Config/DefaultEngine.ini`에
> `bPipStrictHashCheck=False`를 넣었다. pip가 받아오는 패키지를 더 이상 검증하지 않는다.
> 지킬 것 두 가지:
> 1. **`OverrideIndexURL`을 절대 설정하지 말 것.**
> 2. 인덱스는 공식 `download.pytorch.org`만 쓸 것.
>
> 보고서에 보안 항목을 쓸 거라면 `pip download` + `pip hash`로 휠 3개분 해시를 직접
> 채워 넣고 이 설정을 되돌리는 쪽을 권한다 (약 15분).

### 1.2 아레나

[SH_Arena.h](../Source/HAI_Project_TEAM5/RL/SH_Arena.h) / [.cpp](../Source/HAI_Project_TEAM5/RL/SH_Arena.cpp)
— `ALearningAgentsGymSimple` 상속. 바닥·팔각형 벽 8개를 **전부 C++에서** 생성하므로
에디터에서 수동 배치할 게 없다. 모든 치수가 `ArenaRadius`(1750)에서 파생된다.

**NavMesh 함정을 우회했다.** `ALearningAgentsGymSimple::ProjectPointToGym`이
`ProjectPointToNavigation`의 bool 반환을 검사하지 않아서 (`LearningAgentsGymSimple.cpp:56-62`),
**NavigationSystem은 있는데 NavMesh가 없으면 모든 스폰 좌표가 `(0,0,0)`이 된다.** 에러도 경고도 없다.
두 함수를 오버라이드해 순수 수학 클램프로 바꿨다.

실측 (16개 아레나): 피치 5000 cm = `GymsSpacing 1500 + 바닥폭 3500`, 4×4 격자,
`(0,0,0)` ~ `(15000,15000,0)`, 겹침 0, **월드 원점에 쌓인 아레나 0개**.

### 1.3 전투 — 실제 판정

[Gun.h](../Source/HAI_Project_TEAM5/Gun.h) / [.cpp](../Source/HAI_Project_TEAM5/Gun.cpp) —
`PullTrigger()`가 `UE_LOG` 한 줄이었던 것을 실제 히트스캔으로 바꿨다.
반환 구조체 `FGunFireResult`가 보상에 필요한 것을 전부 담는다
(`bFired` / `bHitSomething` / `bHitHostile` / `DamageApplied` / `ImpactPoint` / `HitActor`).

[CombatStatsComponent](../Source/HAI_Project_TEAM5/Combat/CombatStatsComponent.h) —
HP · 팀 · 사망 + **RL 보상 누산기**(`DamageDealtAccum` / `DamageTakenAccum` /
`ShotsFiredAccum` / `ShotsHitAccum` / `KillsAccum` / `bDiedThisInterval`).

**결정론을 위해 의도적으로 한 것:**
- 트레이스가 **캡슐만** 맞는다 (메시는 Bullet 채널 Ignore) — 애니메이션 포즈가 명중에 영향을 주지 않게
- 총구 원점을 **애니메이션 소켓이 아니라 고정 오프셋**(`MuzzleHeight`)으로
- `SpreadDegrees = 0` — `FMath::VRandCone`이 전역 RNG를 써서 재현이 깨진다
- **오버킬 클램프** (`FMath::Min(Amount, CurrentHP)`) — 안 하면 "죽은 적을 계속 쏘기"가 이득이 된다

### 1.4 스크립트 봇 — 1단계 상대역

[SH_ScriptBotController](../Source/HAI_Project_TEAM5/RL/SH_ScriptBotController.h) —
난이도 손잡이 3개(`MaxTurnRateDeg` / `ReactionDelay` / `AccuracyConeDeg`)를 커리큘럼 사다리로 쓴다.

**함정 2개를 실제로 밟고 고쳤다:**
- `AAIController::bSetControlRotationFromPawnOrientation`이 기본 `true`라서
  `SetControlRotation`을 매 프레임 조용히 덮어쓴다. **RL 정책이 조준할 때도 똑같이 걸린다.**
- `bOrientRotationToMovement`와 `bUseControllerDesiredRotation`은 **동시에 켜면 안 된다.**
  `CharacterMovementComponent::PhysicsRotation`이 나중에 돌면서 이긴다.

실측 (봇 vs 봇 1v1): 팀0 HP 10 / 가한 100 / 받은 90 / 10발 10명중 / 1킬 생존,
팀1 HP 0 / 가한 90 / 받은 100 / 9발 9명중 / 사망. 양쪽 가한 값 = 받은 값, 오버킬 없음.

### 1.5 관전 모드

[SH_SpectatorPawn](../Source/HAI_Project_TEAM5/RL/SH_SpectatorPawn.h) +
[SH_SpectatorGameMode](../Source/HAI_Project_TEAM5/RL/SH_SpectatorGameMode.h) —
학습 중 화면을 날아다니며 볼 수 있게. `PlayerStart`를 놓으면 아레나 소속이 아닌 유닛이
섞이므로 GameMode가 직접 스폰 위치를 정한다.

### 1.6 풀링 · 누출 차단 · 처리량 계측

- `ABase_Character`가 `ILearningAgentsEntityInterface` 구현 → **엔진 제공 풀링**을 쓴다.
  직접 만든 풀링 코드는 0줄. `DisableEntity`는 **절대 `Destroy()`를 부르지 않는다.**
- `AGun::IsSameGymAsOwner()` + `CrossGymHitCount` — 사거리 6000이 피치 5000보다 커서
  옆 아레나를 맞힐 수 있다. 벽 차단(1차)과 소속 검사(2차)로 이중 방어.
- 팀 구분을 **클래스 비교가 아니라 `CombatStats.TeamId` 데이터**로. 두 팀 BP 사이의
  상속 제약이 사라졌다. 새 실패 모드(양쪽 BP를 `TeamId = 0`으로 두기)는
  `ValidateTeamAuthoring()`이 Error로 잡는다.
- [SH_RLDirector](../Source/HAI_Project_TEAM5/RL/SH_RLDirector.h)가 처리량 리포트를 찍는다:
  fps / 시간압축 배수 / 아레나 수 / 유닛 수 / step/s / 이터레이션 소요 / 누출 누적 / 액터 수 증가 감지.

### 1.7 ★ Hello-World RL 루프 — Gate 0c 통과

[SH_HelloWorldTrainer.h](../Source/HAI_Project_TEAM5/RL/SH_HelloWorldTrainer.h) /
[.cpp](../Source/HAI_Project_TEAM5/RL/SH_HelloWorldTrainer.cpp) (약 330줄).
**전투 코드 0줄.** 32개 에이전트가 1차원 직선 위에서 자기 목표로 이동하는 것이 전부다.

- 관측 1개 (`Target - X`), 행동 3개 (왼쪽/정지/오른쪽), 보상 `-|X-Target|`
- 에피소드 128스텝 (20Hz = 6.4초), Truncation
- 에디터 없이 헤드리스 실행: `UnrealEditor.exe <uproject> /Game/RL/Lvl_SHHello -game`

| gather (Iter) | Avg Rew | Avg Return |
|---|---|---|
| 0   | **-0.741** | -49.3 |
| 96  | -0.221 | -36.0 |
| 288 | -0.137 | -16.8 |
| 512 | -0.093 | -7.7 |
| 832 | **-0.064** | **-4.2** |

보상이 `-|오차|`이므로 **에피소드 평균 오차 0.741 → 0.064**. 약 4분, 27 gather.
무작위 정책이 0.667이고 이론적 하한이 약 0.06이므로 사실상 최적.

**이 게이트 하나가 동시에 증명한 것:**

| 항목 | 증거 |
|---|---|
| 플러그인 로드 | 스키마 크기 출력 (관측 1 / 행동 3 / 분포 3 / 모디파이어 4) |
| pip · 파이썬 스폰 | `Subprocess:` 로그 |
| 공유메모리 | `Pushing Experience...` / `Pulling network...` 왕복 |
| 3종 호환성 해시 | 불일치 에러 없음 |
| PPO (모방학습 아님) | `TrainerMethod = PPO` |
| **GPU** | `GPU Usage\| Allocated: 0.02 GiB` |
| Truncation 경로 | `Avg Episode Len: 128.00000` |

처리량 **약 510 step/s** (32 에이전트 × 20Hz, 렌더 오버라이드 미적용, 800×450 창).

### 1.8 오늘 발견한 엔진/계획 오류 3개

| # | 내용 |
|---|---|
| 1 | **틱 루프가 중복이었다.** `RunTraining()`이 내부에서 `GatherCompletions → GatherRewards → ProcessExperience → RunInference(= Gather/Modifiers/Encode/Evaluate/Decode/Perform)`를 전부 부른다 (`LearningAgentsPPOTrainer.cpp:675-719`). 밖에서 또 부르면 같은 스텝을 두 번 수집한다. **학습 대상 정책은 `Trainer->RunTraining(...)` 한 줄이면 된다.** 단, self-play 상대 스택은 `RunTraining`이 모르므로 직접 `RunInference` 해야 한다 |
| 2 | **`Build.cs`에 `LearningTraining` 모듈이 필요하다.** `LearningAgentsTrainer.h`(public)가 `LearningTrainer.h`를 include하는데 `LearningAgentsTraining`은 그걸 **private로만** 의존한다 → `fatal error C1083` |
| 3 | **`CriticWarmupIterations = 8`은 "8번의 gather 동안 정책이 멈춘다"가 아니다.** `train_ppo.py:173`이 `if ti == 0` 이므로 **첫 gather 안에서만** 크리틱 전용 패스를 8회 돈다. 참고로 `IterationsPerGather = 32`는 "32 gather마다"가 아니라 **gather 한 번당 최적화 32회**다 |

**추가로, 계측 지표를 한 번 잘못 읽었다.** 게임 쪽에서 `|오차|`를 **순간값**으로 로깅했더니
계속 0.5~0.67이 나와 "학습이 안 된다"처럼 보였다. 원인은 `bResetAgentsOnUpdate = true` —
gather 경계마다 전 에이전트가 리셋되고 리포트가 그 프레임에 겹쳤다.
**결정마다 누산해 구간 평균**을 찍도록 고쳤다. 앞으로 슈터 쪽 지표도 전부 같은 방식이어야 한다.

---

## 2. 현재 게이트 상태

| 게이트 | 내용 | 상태 |
|---|---|---|
| **0a** | GPU 배관 (`torch.cuda`) | ✅ 통과 |
| **0c** | Hello-World RL 루프 | ✅ 통과 (**정상 종료 경로 제외**) |
| **1** | 히트스캔 · HP · 팀 · 사망 | ✅ 통과 (봇 vs 봇 1v1 실측) |
| **2** | 16 아레나 봇 전투 30분 | 🟡 **부분** — 배치만 통과. 스폰·누출·풀링·리셋·종료 미검증 |
| **3a** | 스키마 동결 | ⬜ 미착수 |
| 4~7 | 커리큘럼 · self-play · 3v3 · 출고 | ⬜ 미착수 |

---

## 3. 즉시 할 일 — Gate 2 마무리

> 이게 막혀 있는 이유는 하나다. **아레나가 스폰할 유닛 BP 2개가 아직 없다.**
> 엔진 풀이 `EntityClass->GetFName()`으로 키잉되므로 팀마다 **별개 클래스**가 필요하다.

- [ ] **`BP_Unit_Team0` / `BP_Unit_Team1` 생성** (`BP_BaseCharacter` 상속, 서로 다른 두 클래스)
  - `CombatStats.TeamId` = `0` / `1` ← **이걸 빠뜨리면 조용히 전투가 안 일어난다**
  - `AIControllerClass = SH_ScriptBotController`
  - `AutoPossessAI = Placed in World or Spawned` ← **`Placed in World`(엔진 기본값)면 아레나가 스폰한 유닛에 AI가 안 붙는다. 에러 없음**
  - ⚠️ **BP 기본값(CDO)을 바꿔도 이미 배치된 인스턴스에는 전파되지 않는다.** 인스턴스도 확인할 것
- [ ] `BP_SHArena` 생성 → `Team0UnitClass` / `Team1UnitClass` 지정, `UnitsPerTeam = 1`
- [ ] `EntitiesManager`: `EpisodeEntitySpawnCountMin == Max` (**둘이 다르면 에피소드 도중에 새 액터가 스폰되고, 그 폰은 `AddAgent`를 안 거쳐서 정책이 영원히 무시한다**), `EntitySpawnZOffset = 100`
- [ ] `SHGymsManager.GymTemplates`를 `BP_SHArena`로 교체, 수동 배치한 `TestUnit_*` 제거
- [ ] **누출 검증** — `CROSS-GYM 누적 : 0`. ⚠️ **반드시 아레나가 스폰한 유닛으로.**
      수동 배치 유닛은 `!OwnerGym && !OtherGym` 폴백에 걸려 **항상 통과해버린다**
- [ ] **풀링 검증** — 30분간 유닛 액터 수가 평평한지
- [ ] **리셋 검증** — 죽었다 리셋된 유닛이 **다시 죽을 수 있는지** (캡슐 콜리전 재활성화 확인)
- [ ] 관전 모드 비행 조작 실검증 (실제 키 입력 필요)
- [ ] `bApplyTrainingRenderSettings = true`로 처리량 재측정

> **에피소드 리셋을 거는 주체가 아직 없다.** 1v1은 10초 안에 끝나고 그 뒤로 아무 일도 안 일어난다.
> `SHTrainingEnvironment`가 생기기 전까지는 디렉터에 "N초마다 전체 Gym Reset" 임시 옵션이 필요하다.

### 그 외 정리

- [ ] **git 커밋** — `Plugins/`, `Config/DefaultEngine.ini`, `Source/` 전부 미커밋 상태다
- [ ] `Intermediate/PipInstall`(~3GB)이 `.gitignore`에 들어 있는지 확인
- [ ] Tensorboard 설치: `Intermediate\PipInstall\Scripts\python.exe -m pip install tensorboard`
- [ ] **정상 종료 경로 검증** — `MaxWallClockSeconds`로 Hello-World를 자연 종료시켜
      `EndPlay` → `EndTraining()`이 도는지, 떠도는 `python.exe`가 남지 않는지 확인
      (오늘은 강제 종료해서 `python.exe` 2개를 수동 정리했다)

---

## 4. 학습까지 남은 일

### 4.1 스키마 설계 → 동결 (Gate 3a)

**5장의 범위 결정이 먼저다.** 행동 집합이 해시를 결정한다.

- [ ] `SHRLTypes.h` — 키 이름을 `static const FName` 상수로 **한 곳에만** 선언.
      Specify / Make / Get / Modifier 네 군데가 전부 그 상수를 쓴다.
      **순서가 어긋나면 해시는 통과하면서 의미만 뒤바뀐다** (보상 0, 에러 없음)
- [ ] `SHInteractor` 구현 — 관측/행동
- [ ] **`Scales` 설정** — `Location`/`Velocity`의 기본 `Scales = (1,1,1)`에
      언리얼 단위 ±1500 cm를 넣으면 첫 레이어가 포화한다. **UE에서 RL이 조용히 실패하는 1위 원인**
- [ ] `SCHEMA.md`에 5개 크기 + 3종 해시를 날짜와 함께 기록·커밋 → **이후 변경 금지**

해시를 바꾸는 것: struct 필드 추가/삭제/**순서변경**, `FName Tag` 변경, `bActive` 변경,
`MaxNum`, `Size`, `AttentionEncodingSize`/`HeadNum`/`ValueEncodingSize`.

### 4.2 학습 파이프라인 이식

Hello-World에서 검증된 조립 순서를 그대로 슈터에 옮긴다.

```
1. GymsManager->Start()            ← 아무도 대신 안 불러줌
2. Manager->SetMaxAgentNum(N)      ← 모든 리스너 생성 전에
3. MakeInteractor
4. MakePolicy   5. MakeCritic   6. MakeTrainingEnvironment
7. MakeSharedMemoryTrainingProcess({TrainerFileName="learning_core.train_ppo", Timeout=60})
8. MakePPOTrainer
9. (self-play 단계에서) 상대 스택 구성
★10. AddAgent(Controller)          ← 맨 마지막. 폰이 아니라 컨트롤러를 등록
```

> **리스너가 생기기 전에 추가된 에이전트는 그 리스너에게 영원히 보이지 않고 에러도 없다.**

- [ ] `SHTrainingEnvironment` — 보상 누산기 배출 + 완료 조건
      (전멸 = `Termination`, 타임아웃 = `Truncation`. **타임아웃에 `Termination`을 쓰면
      "시간이 다 되는 건 미래 가치 0인 진짜 끝"이라고 가르쳐 가치 함수가 왜곡된다**)
- [ ] `SH_RLDirector`에 20Hz 결정 루프 + `Trainer->RunTraining(...)` 한 줄
- [ ] `EndPlay`에서 **반드시** `EndTraining()`
- [ ] `HasTrainingFailed()` 매 틱 폴링 — **이게 유일한 에러 신호다**

### 4.3 보상

- [ ] 누산기 배출 (`GatherAgentReward`가 읽고 **0으로 비운다**)
- [ ] ⚠️ **이 PPO는 나쁜 행동을 직접 벌하지 않는다.** `MinimumAdvantage = 0.0`이 기본값이라
      **음수 advantage가 전부 0으로 잘린다** (`ppo.py:130`). 벌점 항은 해당 상태의 `V(s)`를
      낮춰 *다른* 행동의 advantage를 양수로 만드는 간접 경로로만 작동한다.
      → **벌점을 키우는 것보다 보상 셰이핑을 정교하게 하는 쪽이 훨씬 효과적이다**
- [ ] 무승부 벌점 필수 — 없으면 "30초 아무것도 안 하기"가 승리와 같은 가치가 된다

### 4.4 커리큘럼

| 단계 | 상대 | 성공 기준 |
|---|---|---|
| 0 | 고정 더미 (거리 800) | 명중률 ≥ 70% |
| 0.5 | 이동 더미 | 명중률 ≥ 50% |
| 1 | 스크립트 봇 (하) | 승률 ≥ 60% |
| 1.5 | 스크립트 봇 (상) | 승률 ≥ 50% |
| 2 | 동결 스냅샷 self-play | v1이 v0을 ≥ 70% |
| 3 | 3v3 → 10v10 파인튜닝 | 3v3 튜닝본이 1v1 복제본을 ≥ 65% |

- [ ] warm start — `MakePolicy(..., bReinitialize*=false)`. 같은 플래그가 전 단계를 구동한다
- [ ] self-play는 **최근 5개 스냅샷에서 균등 랜덤**. 항상 최신만 로드하면 순환/파국적 망각

---

## 5. ★ 어디까지 RL로 돌릴 것인가 — **결정 필요**

유닛의 행동은 4개 축으로 쪼갤 수 있다. 각 축을 **RL이 결정**할지 **스크립트가 결정**할지 고른다.

| 축 | 스크립트로 하면 | RL로 하면 |
|---|---|---|
| **발사** (언제 쏘나) | 조준 오차 < 콘 && 쿨다운 끝 | 사격 타이밍·탄약 관리 학습 |
| **조준** (어디를 겨누나) | 오토 에임 (표적 위치로 즉시) | 리드 사격·각속도 제약 하의 추적 학습 |
| **이동** (어디로 가나) | 거리 유지 + 좌우 선회 | 포지셔닝·카이팅·엄폐 학습 |
| **표적 선택** (누굴 노리나) | 가장 가까운 적 | 집중사격·우선순위 학습 |

### 5.1 조합별 비용과 얻는 것

처리량 가정: **400 step/s** (오늘 토이가 510 step/s였고 슈터 시뮬은 더 무거우므로 보수적으로).
렌더 오버라이드 + 아레나 증설로 올릴 여지가 있다.

| 안 | RL이 맡는 것 | 예상 스텝 | 벽시계 @400 step/s | 시연에서 티가 나나 |
|---|---|---|---|---|
| **A** | 발사만 | 0.3~1 M | **15분 ~ 40분** | 거의 안 남 |
| **B** | 발사 + 조준 | 2~5 M | **1.5 ~ 3.5시간** | 별로 안 남 |
| **C** | 발사 + 이동 + 표적 (조준은 오토 에임) | 2~5 M | **1.5 ~ 3.5시간** | **확실히 남** |
| **D** | 전부 (조준 포함) | 5~20 M | **3.5 ~ 14시간** | 남 |
| **E** | D + 3v3/10v10 | +30~50% | **하룻밤 ~ 이틀** | 가장 많이 남 |

> **계획 문서의 현재 결정은 D/E다** ("정책이 조준까지", 행동 = `AimDir` + `MoveDir` + `Fire` + `Target`).
> **조준 학습은 다른 것들보다 한 자릿수 이상 어렵다.** 그래서 오토 조준 스위치를 병행하기로 했다.

### 5.2 판단 재료 — 정직하게

**조준 학습이 비싼 이유.** 나머지 셋은 "어떤 선택이 좋은가"의 이산/저차원 문제인데,
조준은 **연속 제어 루프**다. `MaxTurnRateDeg = 180`/초에서 20Hz면 결정당 9도씩 끊어서
움직이는 표적을 따라가야 한다. 이걸 배우는 데 전체 학습 예산의 대부분이 들어간다.

**그런데 오토배틀러에서 조준 정확도는 화면에 잘 안 보인다.** 관객이 "똑똑하다"고 느끼는 것은
**포지셔닝 · 카이팅 · 집중사격**이지 "빗맞히지 않는다"가 아니다. 시연 영상과 보고서 기준으로는
**C가 시간당 효과가 가장 크다.**

**반대로 조준을 RL로 하면 얻는 것**: 보고서에 쓸 "오토 조준 vs 정책 조준" 비교 실험이 나온다.
같은 스키마·같은 시드로 두 번 돌려 명중률 곡선을 겹쳐 그리면 그 자체가 결과물이다.

**발사만(A) 하면 안 되는 이유**: 행동 공간이 사실상 1비트라 "RL을 썼다"는 말이 무색해진다.
교차검증할 baseline도 없다. **A는 단독 목표로는 부적합하고, 파이프라인 연기 테스트로만 쓸 값어치가 있다.**

### 5.3 권고 — 스키마는 D로 동결하고, 실행 스위치로 단계를 올린다

**행동 집합을 나중에 바꾸면 해시가 깨져서 학습한 가중치를 전부 버려야 한다.**
그래서 "지금은 A로 하고 나중에 D로 늘린다"는 **불가능하다** — 처음부터 다시 학습해야 한다.

해결책: **행동 헤드는 처음부터 전부 만들어 동결하고**, 그 값을 **실행부에서 쓸지 말지**를
런타임 스위치로 고른다. 스위치는 스키마에 없으므로 해시에 영향이 없다.

```cpp
// 실행부 스위치 — 스키마가 아니라 실행 코드가 읽는다. 해시 불변.
UPROPERTY(EditAnywhere) bool bPolicyControlsAim    = false;  // false면 오토 에임
UPROPERTY(EditAnywhere) bool bPolicyControlsMove   = true;
UPROPERTY(EditAnywhere) bool bPolicyControlsTarget = true;
// 발사는 항상 정책이 한다 — 이게 없으면 학습할 게 없다
```

| 단계 | 스위치 | 목적 |
|---|---|---|
| 1 | Aim `false` (오토 에임) | **C를 먼저 확실히 돌린다.** 포지셔닝·집중사격이 시연 가치가 가장 높고 빨리 나온다 |
| 2 | Aim `true`, 나머지 그대로 | 조준 헤드를 켜서 **D**로. 1단계 가중치에서 warm start |
| 3 | 3v3 → 10v10 | `MaxNum`을 처음부터 10으로 잡아두면 해시가 안 바뀐다 |

> **주의.** 꺼둔 헤드도 정책은 계속 샘플링한다. 보상에 영향이 없으니 그래디언트가 노이즈라
> 엔트로피 최대(≈균등) 쪽으로 흘러간다. 무해하지만, **1단계 벽시계의 일부를 헛되이 쓴다.**
> 그래도 해시를 두 번 동결하는 것보다는 훨씬 싸다.

### 5.4 어떤 경우에도 RL이 아닌 것

| 항목 | 이유 |
|---|---|
| **유닛 보드 배치** | 플레이어가 하는 게임의 핵심 조작이다. 오토배틀러의 재미 자체 |
| **매치 진행 · 승패 판정** | 규칙이다 |
| **각속도 상한 적용** | **정책은 "원하는 방향"을 내고, 실행부가 상한을 걸어 추종한다.** Δyaw 적분은 드리프트가 생기고 정책이 절대 조준을 기억해야 한다 |
| **쿨다운 중 발사 차단** | 행동 마스킹으로 처리. **단, 조준 오차로는 마스킹하지 않는다** — 그러면 빗맞히는 게 나쁘다는 걸 영영 못 배운다 |
| **근접 아키타입** | 별도 스키마 · 별도 정책. 슈터 스키마를 건드리면 해시가 깨진다 |

### 5.5 결정해야 할 것 — 팀 논의 필요

- [ ] **5.3의 단계별 스위치 방식을 채택할 것인가?** (아니라면 처음부터 D로 직행)
- [ ] **최종 목표를 C에서 멈출 것인가, D까지 갈 것인가?**
      → 남은 일정과 "보고서에 조준 비교 실험을 넣을 것인가"가 기준
- [ ] **표적 선택을 RL에 넣을 것인가?** 1v1에선 의미가 없고 3v3부터 의미가 생긴다.
      단, `Target` 헤드를 나중에 추가하면 해시가 깨지므로 **지금 넣을지 결정해야 한다**
- [ ] **`Enemies` 배열의 `MaxNum`** — 최종 10v10이면 처음부터 **10**으로.
      나중에 늘리면 해시가 깨진다
- [ ] 결정 주기 20Hz 확정? (조준을 RL로 하면 20Hz가 필요하고, 안 하면 10Hz로 충분해서
      **수집 속도가 2배가 된다**)

---

## 6. 알려진 위험

| # | 위험 | 대책 |
|---|---|---|
| 1 | 조준 학습이 수렴하지 않음 | 오토 조준과 병렬 비교. 4시간 후 명중률 10% 미만이면 `DirInAimFrame`의 RelativeTransform이 틀린 것 |
| 2 | 인터넷 자료가 전부 5.3~5.5 API | experimental 0.2, 엔진에 문서·샘플 0. **헤더와 `ppo.py`가 유일한 문서** |
| 3 | 엔진 업그레이드 | 프로젝트 중 금지. **1.1의 GPU 패치가 날아간다** |
| 4 | GPU 패치가 팀원마다 다름 | 프로젝트 로컬 복사라 git으로 전파된다. **4명 전원이 Gate 0a를 통과해야** |
| 5 | 해시 검사 OFF = 공급망 위험 | `OverrideIndexURL` 절대 금지, 공식 인덱스만 |
| 6 | GPU/CPU 결과 불일치 | 같은 시드도 디바이스가 다르면 궤적이 다르다. **비교 실험은 동일 디바이스에서만** |
| 7 | `EndTraining()` 누락 | 에디터가 고정 타임스텝에 남고 `python.exe`가 떠돈다. **오늘 실제로 발생** |
| 8 | 경로에 한글·공백 금지 | 파이썬 서브프로세스 argv 문제 |
| 9 | 시드 5개 (PPO / Policy / Critic / Gym / GymsManager) | 전부 설정하고 **로깅**. 없으면 보고서를 못 쓴다 |

---

## 7. 파일 위치

| 무엇 | 어디 |
|---|---|
| 아레나 | [Source/HAI_Project_TEAM5/RL/SH_Arena.h](../Source/HAI_Project_TEAM5/RL/SH_Arena.h) |
| 디렉터 | [Source/HAI_Project_TEAM5/RL/SH_RLDirector.h](../Source/HAI_Project_TEAM5/RL/SH_RLDirector.h) |
| 스크립트 봇 | [Source/HAI_Project_TEAM5/RL/SH_ScriptBotController.h](../Source/HAI_Project_TEAM5/RL/SH_ScriptBotController.h) |
| Hello-World 학습기 | [Source/HAI_Project_TEAM5/RL/SH_HelloWorldTrainer.h](../Source/HAI_Project_TEAM5/RL/SH_HelloWorldTrainer.h) |
| HP · 팀 · 보상 누산기 | [Source/HAI_Project_TEAM5/Combat/CombatStatsComponent.h](../Source/HAI_Project_TEAM5/Combat/CombatStatsComponent.h) |
| 히트스캔 | [Source/HAI_Project_TEAM5/Gun.h](../Source/HAI_Project_TEAM5/Gun.h) |
| 학습 맵 (슈터) | `/Game/RL/Lvl_SHTrain` |
| 학습 맵 (Hello-World) | `/Game/RL/Lvl_SHHello` |
| GPU 패치 | `Plugins/PythonMLPackages/`, `Plugins/PythonFoundationPackages/` |
| 트레이너 설정 (파이썬이 실제로 읽는 파일) | `Intermediate/LearningCore/<TaskName>0/Configs/trainer-config.json` |

### 헤드리스 학습 실행

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\HAI_Project_TEAM5\HAI_Project_TEAM5.uproject" /Game/RL/Lvl_SHHello -game -windowed -resx=800 -resy=450 -nosplash
```
