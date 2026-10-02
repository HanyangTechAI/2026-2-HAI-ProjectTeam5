# RL 학습 TODO

> HAI 2026-1 TEAM 5 / UE 5.8 / LearningAgents 0.2 (experimental)
> 최종 갱신: **2026-09-29**
> 상세 설계(스키마·보상·커리큘럼·출고)는 [PLAN.md](PLAN.md) 참조.
> 이 문서는 **무엇이 끝났고 다음에 뭘 하는지**만 적는다.

---

## 0. 한 줄 요약

**슈터 정책이 학습된다.** 시야 제한 + 엄폐물 아레나에서 90분 학습으로 명중률이
**무작위 조준 기준선 1.3% → 16.0%** 가 됐다. 기준점은 스크립트 봇 65% 다.
곡선은 실행이 끝날 때까지 **한 번도 꺾이지 않았다** — 더 돌리면 더 오른다.

**학습되는 것은 조준과 발사 타이밍 둘이다.** 이동은 아직 균등분포에 가깝고(1A.6c),
엄폐는 후방 관측이 없어 원리적으로 불가능했다 — 2026-10-01 에 `Hit` 관측을 넣어
가능하게 만들었고(1A.8c) **검증은 아직 안 됐다.**

스키마는 **동결됐다** — 관측 42 / 행동 12 / 모디파이어 16. 이제 바꾸면 NN 에셋 4개를 버려야 한다.
5장의 "어디까지 RL로 돌릴지"는 **이동 · 조준 · 발사** 로 확정됐고 그대로 구현됐다.

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

## 1A. 2026-09-29 — 슈터 RL 전환

> 계획서: [PLAN.md](PLAN.md). 이 절은 **무엇이 실제로 돌았고 무엇이 틀렸는지**만 적는다.

### 1A.1 핵심 설계 한 줄

**시야 레이를 총알과 같은 충돌 채널로 쏜다** (`ECC_GameTraceChannel1` = "Bullet",
`DefaultResponse = ECR_Block`). 그래서 "레이가 적을 본다" ≡ "그 방향으로 쏜 총알이 맞는다" 가
되고, 엄폐 판정이 따라온다. `MaxRange` = `AGun::Range` = 6000,
`EyeHeight` = `AGun::MuzzleHeight` = 60 — 어긋나면 "보이는데 안 맞는" 구간이 생기고
정책이 그걸 학습한다. `ASH_RLCharacter::BeginPlay` 가 둘을 검사해 Error 로그를 낸다.

### 1A.2 새로 만든 것

| 파일 | 무엇 |
|---|---|
| [SH_VisionComponent](../Source/HAI_Project_TEAM5/RL/SH_VisionComponent.h) | 부채꼴 16레이 센서. **RL 유닛과 스크립트 봇이 같은 것을 쓴다** — 한쪽만 제한하면 정책이 비대칭을 학습한다 |
| [SH_RLCharacter](../Source/HAI_Project_TEAM5/RL/SH_RLCharacter.h) | `ABase_Character` 상속. base 는 `virtual` 한 단어만 고쳤다 |
| [SH_RLBotController](../Source/HAI_Project_TEAM5/RL/SH_RLBotController.h) | 행동은 안 짜고 회전 모드만 세팅 |
| [SH_ShooterInteractor](../Source/HAI_Project_TEAM5/RL/SH_ShooterInteractor.h) | 관측/행동 스키마 |
| [SH_ShooterTrainingEnv](../Source/HAI_Project_TEAM5/RL/SH_ShooterTrainingEnv.h) | 보상 · 완료 · 리셋 |
| [SH_CombatTrainer](../Source/HAI_Project_TEAM5/RL/SH_CombatTrainer.h) | 8단계 조립. **첫 Tick 에서 조립한다** — BeginPlay 순서가 정해져 있지 않아 아레나가 아직 없을 수 있다 |
| `Content/RL/BP_SHArena` | 엄폐물 7개(Cube, `BlockAll`, CastShadow off). `GymsManager` 가 이 **클래스**를 16번 스폰한다 |
| `Content/RL/BP_Unit_Team0` / `BP_Unit_Team1` | 둘 다 부모 `SH_RLCharacter`. 컨트롤러만 다르다 (Team0 = RL, Team1 = 스크립트) |

### 1A.3 동결된 스키마 — 관측 42 / 행동 12

```
관측 Root {
  Fan:    StaticArray(16 x Struct{ RayDepth:Float, RayHostile:Bool })   = 32
  Enemy:  Optional( Struct{ EnemyAngle:Angle, EnemyDepth:Float }, 32 )  =  5
  Memory: Struct{ TimeSince:Float(Scale 3.0), LastAngle:Angle }         =  3
  Self:   Struct{ HPRatio:Float, CanFire:Bool }                         =  2
}
행동 ActRoot { Turn:ExclusiveDiscrete(5), Move:ExclusiveDiscrete(5), Fire:Bool(prior 0.3) }
```

`Angle` 은 sin/cos 로 **2 float** 으로 인코딩된다. `data-config.json` 과 정확히 일치했다.

> ### ★ `Conv1d` 를 쓰지 않는다 (계획에서 변경)
> `SpecifyConv1dObservation` 은 C++ 에 있고 JSON 으로도 내려가지만
> (`LearningTrainer.cpp:272`), **파이썬 트레이너가 구현하지 않았다.**
> `train_common.py` 의 obs_type 디스패처는 `And / OrExclusive / OrInclusive / Array / Set /
> Encoding` 만 분기하고 `else: raise Exception('Not Implemented')` 로 끝난다.
> `Conv1d` 는 "정규화 없음 / 분포 없음" 제외 목록 네 곳(1223, 1431, 1568, 1632)에만 이름이 있다.
> 덤으로 `FConv1dObservationParams` 기본값이 전부 0 이고 `CreateConv1d` 는
> `InputLength × InChannels` 가 자식 크기와 맞는지 **검사하지 않는다.**
> `StaticArray` 는 JSON 타입 `Array` 로 내려가고 파이썬이 완전히 처리한다.

### 1A.4 ★ 오늘 잡은 버그 5개 — 전부 "에러 없이 조용히 망가지는" 종류다

| # | 무엇 | 왜 조용한가 |
|---|---|---|
| 1 | **`RunTraining` 을 `IsTraining()` 뒤에 뒀다** | `RunTraining` 이 **첫 호출에서 `BeginTraining` 을 한다.** 시작 전에는 `IsTraining()` 이 false 이므로 영원히 안 불린다. 로그 한 줄도 안 나온다. 증거는 `Intermediate/LearningCore/SHCombat0` 이 아예 생기지 않은 것이었다. 해결: `bTrainingStarted` 플래그로 "아직 시작 안 함" 과 "끝남" 을 구분 |
| 2 | **`ACharacter` 기본 스폰 정책이 `AdjustIfPossibleButDontSpawnIfColliding`** | `LearningAgentsEntitiesManagerComponent.cpp:158-170` 이 스폰 트랜스폼을 루프 **밖에서** 한 번 계산해 N개를 같은 점에 스폰하고, `SpawnActor` 결과를 **널 검사 없이** `Execute_InitializeEntity` 에 넘긴다(`:164-168`) → `Assertion failed: O != 0`. 해결: 유닛 BP 둘 다 `ALWAYS_SPAWN` |
| 3 | **`ABase_Character::ResetForNewMatch()` 가 `virtual` 이 아니었다** | `ResetEntity_Implementation` 이 이걸 부르므로(`Base_Character.cpp:101`) 자식의 `Pending*` 클리어와 `Vision->ResetMemory()` 가 영영 안 돈다. 지난 판의 "마지막으로 본 방향" 이 다음 판으로 샌다 |
| 4 | **셰이핑 감쇠 단위를 잘못 읽었다** | `RewardStepCount` 는 결정마다 **에이전트별로** 증가한다. 160 에이전트면 `400,000` 에 **228초**만에 도달한다 — 의도의 30배. `3,000,000` 으로 올렸다 (약 33분) |
| 6 | **`MissWeight` 가 발사를 산술적으로 손해로 만들었다** | 아래 1A.4a 에서 따로 다룬다. **에러도 경고도 없고, 정책은 완벽히 옳게 행동한다.** 보상이 틀렸을 뿐이다 |
| 5 | **아레나 리셋 시 생존자에게 완료 신호가 없었다** | `LearningAgentsPPOTrainer.cpp:668` 이 `GetAgentCompletions()` 가 Running 이 아닌 에이전트만 리셋 대상으로 잡는다. 죽은 1명만 완료를 냈고, 같은 아레나의 9명은 **아레나가 통째로 순간이동·회복되는 전이를 완료 표시 없이** 자기 버퍼에 담았다. 해결: 생존자에게 `Truncation` |

### 1A.4a ★★ 보상 산술 — 발사가 손해가 되는 문턱

**이번 프로젝트에서 가장 비싼 교훈이다.** 벌점 하나의 크기가 정책을 통째로 뒤집었다.

`AGun::Damage = 10`, `MaxHP = 100`, `DamageDealtWeight = 1.0`, `KillBonus = 0.5` 이므로

| | 값 |
|---|---|
| 명중 1발 | `1.0 × 10/100` = **+0.10** |
| 킬 보너스 분할 (10발에 1킬) | **+0.05** |
| 빗맞힘 1발 | `-MissWeight` |

발사의 기대 보상은 명중률 p 에 대해

```
E[발사] = (0.10 + 0.05) p - MissWeight (1 - p) = 0.15 p - MissWeight (1 - p)
손익분기 p = MissWeight / (0.15 + MissWeight)        (킬 보너스를 빼면 / (0.10 + MissWeight))
```

| MissWeight | 손익분기 명중률 |
|---|---|
| 0.05 (최초값) | **25.0%** |
| 0.02 | 11.8% |
| **0.005 (현재)** | **3.2%** |

**무작위 조준이 1.3% 다.** 손익분기가 25% 면 정책이 절대 도달할 수 없는 문턱이고,
그 아래에서는 **쏘지 않는 것이 옳은 답**이다. 실측이 정확히 그 모양이었다 (2026-09-29):

| 셰이핑 | 3회차 (MissWeight 0.05) | 4회차 (0.005) |
|---|---|---|
| x0.97 → x0.83 | 사격 14,639 → 명중률 1.1% | 사격 15,833 → 1.4% |
| x0.40 → x0.45 | 사격 5,088 → **7.4%** | 사격 21,818 → **7.4%** |
| x0.15 이하 | 사격 **1,447** → **3.7%** | 사격 22,167 → **10.1%** |

3회차는 셰이핑이 빠지자 **발사량이 10배 줄고** 명중률이 7.7% → 3.7% 로 무너졌으며
시야 유지율까지 29.1% → 18.2% 로 같이 내려갔다 — 쏠 이유가 없으면 적을 볼 이유도 없다.
4회차는 같은 구간에서 발사량이 유지되고 명중률이 **10% 를 넘겼다.**

> **읽는 법 두 가지.**
> 1. **명중률과 피해 처리량은 다른 지표다.** 선택 발사 정책은 "자신 있을 때만 쏴서" 명중률을
>    올릴 수 있다. 상시 발사 정책의 명중률은 **조준선이 적 위에 있는 시간 비율**이고 부풀릴 수 없다.
>    비교는 **결정 1,000회당 명중 수**로 해야 한다 — 3회차 최고 2.9 vs 4회차 23.2 (8배).
> 2. **`Avg Rew` 의 절대값으로 판단하지 말 것.** 1단계 상대가 명중률 65% 스크립트 봇이라
>    RL 팀은 주는 피해보다 받는 피해가 구조적으로 많다. 전투가 격해질수록 `Avg Rew` 는
>    **내려간다.** 실제로 4회차는 명중률이 오르는 내내 -0.006 → -0.015 로 내려갔다.

**셰이핑을 0 까지 내리지 않는다.** `ShapingFloor = 0.15`. 0 이면 "적을 향해 돈다" 를 유지할
신호가 하나도 남지 않는다. 0.15 배면 완벽 조준 시 스텝당 최대 `(0.02 + 0.05) × 0.15 = 0.0105`
이고 명중 1발(+0.10)보다 한 자리 작으므로, "보기만 하고 안 쏘는" 반대 실패 모드는 오지 않는다.

**버그 5 는 정확성 문제인 줄 알았는데 처리량 문제였다.** `bResetAgentsOnUpdate = true` 가
게더 경계마다 전 에이전트를 리셋하고 그때 **열려 있던 에피소드는 버려진다.** 고치기 전에는
생존자의 에피소드가 계속 열린 채 그 경계에서 폐기됐다. 실측:

| | 128 이터레이션 소요 | 기록된 step/s |
|---|---|---|
| 고치기 전 | 92.0초 | 715 |
| 고친 후 | **39.5초** | **1,675** |

시뮬레이션 속도는 그대로다 (결정 631 → 621). 늘어난 것은 **버퍼에 실제로 들어가는 경험**이다.

### 1A.5 교착을 측정으로 풀었다 — 추측 두 번 틀렸다

초기에 전투가 얼어붙었다 (사망 55~71, 생존 249~265 고정). 진행:

1. FOV 90 → 360 으로 열어도 그대로 → **FOV 가 아니라 엄폐물이 구속 조건**
2. `ReportCombat` 추가 → **표적 보유 봇 0/256 (0.0%)**
3. `ReportVisionFailures` 추가 → 시야각밖 240 / 가려짐 16 / **보임 0**
4. 선회 각도 샘플 → 494 → 1695 → 2898 → 4097 = 정확히 120°/s. 시야각 검사는 정상
5. 차단 **컴포넌트 이름**까지 찍음 → `차이 2.0도 → 가려짐 Cover_N` = 마주 본 시선을 엄폐물이 전부 막는다
6. **고침 A — 중심이 아니라 아레나 대척점으로 전진**: 사망 64 → **224**
7. **고침 B — 봇별 선회 속도 비동기화** (`GetUniqueID() % 97`): 동기화되면 상대 방위가 고정되어 영원히 못 만난다

> **내가 두 번 틀렸다.** (a) `Cover_C`(중앙 블록)가 원인이라고 예측했지만 콜리전을 꺼도
> 아무 변화가 없었다. (b) 봇의 **매 프레임 재탐색**(프레임당 약 1,600 트레이스)이 처리량
> 병목이라고 봤지만, `ReactionDelay` 를 지키게 해서 15배 줄여도 결정 수가 631 → 621 로
> 사실상 동일했다. `Pull Experience` 77% 의 원인은 다른 곳이다 — 다음 프로파일에서
> 스켈레탈 메시 · 캐릭터 무브먼트 · 공유메모리 왕복을 본다.

### 1A.6 실측 (16 아레나 × 유닛 20 = 320, RL 에이전트 160)

- 아레나 격자: 최소 중심 간 거리 **5112 cm** > 필요치 3500, 겹침 없음, 원점에 쌓이지 않음
- **`CROSS-GYM 누적 = 0`** — `AGun::Range 6000` 이 격자 피치 5112 보다 큰데도 0 이다 (벽이 막는다)
- 풀링 320/320 평평, 성장 경고 없음, 슬롯 차단 Error 0건, 기하 불일치 Error 0건
- 정상 종료: 300초 자연 종료, exit 0, **고아 `python.exe` 0개**
- 프로파일: `Pull Experience` 약 17,500 ms (**77%**) vs `PPO train` 약 5,200 ms (23%),
  GPU 메모리 0.26 GiB / 16 GB → **병목은 GPU 가 아니라 언리얼 시뮬레이션이다**
- 학습(4회차, 90분 = 5,400초, MissWeight 0.005 / ShapingFloor 0.15):

  | 시각 | 명중률 | 구간 사격 | 시야에 적 | 셰이핑 |
  |---|---|---|---|---|
  | 60초 | 1.4% | 15,833 | 22.7% | x0.97 |
  | 921초 | 5.8% | 19,952 | 25.7% | x0.58 |
  | 1,842초 | 9.6% | 21,349 | 28.1% | x0.18 |
  | 2,768초 | 11.0% | 21,008 | 26.8% | x0.15 (하한) |
  | 3,693초 | 13.1% | 20,797 | 26.8% | x0.15 |
  | 4,626초 | 16.0% | 18,065 | 27.5% | x0.15 |
  | 5,378초 | **16.0%** | 19,143 | 26.9% | x0.15 |

  **발사량이 줄지 않았다** (15,833 → 19,143). 3회차는 같은 구간에서 10배 줄었다.
  셰이핑이 하한에 고정된 뒤(1,900초 이후)에도 10.3% → 16.0% 로 계속 올랐으므로,
  이 상승은 셰이핑이 아니라 **주 보상(데미지·킬)이 만든 것이다.**

- 처리량(4회차): 이터레이션 13,696 / 게더 428 / **약 7.0 M 스텝**, 평균 1,298 step/s.
  정상 종료 exit 0, 고아 `python.exe` 0개
- 프로파일(4회차): `Pull Experience` 약 6,900 ms (**56%**) vs `PPO train` 약 5,500 ms (44%).
  2회차의 77 / 23 에서 개선됐다 — 에피소드가 자주 닫히면서 같은 시뮬레이션 시간에
  버퍼가 더 빨리 차기 때문이다. GPU 메모리는 여전히 0.26 GiB / 16 GB 다

> **기준점 두 개를 기억할 것.** 무작위 조준 **1.3%** (캡슐 반경 42 가 1000 cm 에서 4.8° 를
> 차지한다 → 4.8/360), 스크립트 봇 **65%**. 이 사이 어디에 있는지가 유일하게 의미 있는 지표다.

### 1A.6b ★ 진단 지표 3개 — 명중률만으로는 알 수 없는 것

**명중률은 조준만 측정한다.** 행동 공간은 조준 · 이동 · 발사 셋인데 나머지 둘을 보는 수단이
없었다. 그래서 리포트를 두 줄로 늘렸다 (2026-10-01).

```
[SH_CombatTrainer]   303초 | 결정 ... 명중 1670 (7.6%) | 셰이핑 x0.82
[SH_CombatTrainer]        피해 받음 1040.9 / 줌 151.3 (결정 1k당, 생존결정 ...) |
                          발사기회 사용 41.3% (22097/53550, 기회비율 48.5%) |
                          이동 정지 19% 전진 18% 후진 21% 좌 22% 우 20% (균등=20%)
```

| 지표 | 무엇을 보는가 | 기준선 |
|---|---|---|
| **받은/준 피해 (결정 1k당)** | **엄폐.** 생존율로는 알 수 없다 — 아레나가 첫 사망에 전체 리셋되므로 그 평균(99.3~99.5%)은 "방금 리셋된 상태" 가 지배한다 | — |
| **발사기회 사용률** | **발사 타이밍.** 분모는 쿨다운이 끝난 결정이다 | `PriorProbability = 0.3` → **30%** |
| **이동 구간 분포** | **이동.** 다섯 구간이 각 20% 면 정책은 이동을 무작위로 뽑고 있다 | **20 / 20 / 20 / 20 / 20%** |

> ### ★ 구현에서 조용히 틀리는 지점 둘
> 1. **피해량은 `ConsumeRewardAccumulators()` 보다 먼저 읽어야 한다.** 그 함수가 누산기를
>    비우므로 순서가 바뀌면 **항상 0 이 나오고 에러는 나지 않는다.**
> 2. **이동/발사는 `PerformAgentAction` 의 조기 반환들을 지난 뒤에 센다.** 죽은 에이전트를
>    분모에 넣으면 이동 분포가 "정지" 쪽으로 쏠려 보인다. 발사 가능 여부는 `PullTrigger()`
>    **전에** 읽는다 — 그래야 `MakeAgentActionModifier` 가 마스킹을 결정할 때 본 값과 같다.

**자기 검산이 된다.** 사용률이 오르면 쿨다운이 더 자주 걸려 **기회비율은 내려간다.**
실측(5분, 신규 네트워크): 사용률 30.3 → 41.3% 일 때 기회비율 55.9 → 48.5%.
두 수가 같은 방향으로 움직이면 카운터가 틀린 자리에서 세고 있는 것이다.

### 1A.6c 첫 측정 결과 (5분, 무작위 초기화에서)

| 시각 | 명중률 | 피해 줌 | 피해 받음 | 발사 사용률 | 이동 (정지/전진/후진/좌/우) |
|---|---|---|---|---|---|
| 60초 | 1.0% | 17.4 | 958.5 | 30.3% | 20/19/19/20/21 |
| 120초 | 3.0% | 55.2 | 1006.3 | 35.0% | 21/17/19/20/23 |
| 180초 | 5.7% | 109.8 | 1042.6 | 38.8% | 21/17/18/20/24 |
| 240초 | 7.0% | 140.8 | 1043.9 | 42.0% | 19/16/20/22/22 |
| 303초 | 7.6% | 151.3 | 1040.9 | 41.3% | 19/18/21/22/20 |

**읽은 것 셋.**

1. **발사 타이밍은 학습된다.** 사전확률 30% 에서 41% 로 단조 상승. 같은 구간에
   피해 처리량이 17.4 → 151.3 (8.7배) 로 올랐으므로 "더 많이 쏴서" 가 아니라
   "더 맞는 때에 쏴서" 늘어난 것이다 (명중률도 1.0 → 7.6%).
2. **이동은 아직 학습되지 않았다.** 다섯 구간이 전부 16~24% 안에 있다.
   일관된 편차는 **전진이 19 → 16% 로 내려간 것** 하나뿐이고(4%p),
   상대가 명중률 65% 봇이라 전진하면 받는 피해가 늘어 `DamageTakenWeight 0.7` 이
   벌하는 것으로 설명된다. 긴 실행에서 이 격차가 벌어지는지가 판단 기준이다.
   > 180초에 "우 24% vs 좌 20% = 우측 선호, 아레나 비대칭 의심" 으로 읽었는데
   > 240초에 22/22 로 대칭이 됐다. **노이즈였다.** 3%p 편차에 의미를 붙이면 안 된다.
3. **엄폐는 전혀 작동하지 않는다.** 받은 피해가 958 → 1041 로 **오히려 늘었다.**
   받는 피해가 주는 피해의 **6.9배**다 (1041 vs 151). 당연하다 — 엄폐 보상도,
   후방 관측도 없다. 1A.8b 참조.

### 1A.8b 엄폐가 학습될 수 없는 구조적 이유 셋

1. **관측에 후방이 없다.** 부채꼴 16레이가 `FOVDegrees = 90` 안만 본다.
   뒤나 옆에서 쏘는 적은 관측에 전혀 나타나지 않는다 —
   **누가 자기를 쏘는지 모르는데 그 방향을 피할 수는 없다.**
2. **엄폐 보상이 없다.** 보상 항 7개 중 위치·차폐에 관한 것이 하나도 없다.
   `DamageTakenWeight 0.7` 만이 간접 압력이고, 1단계 상대가 명중률 65% 봇이라
   받는 피해가 구조적으로 많아 이 신호가 묻힌다.
3. **기억이 적 하나에 대한 숫자 2개뿐이다** (`TimeSince`, `LastAngle`).
   엄폐물 위치를 기억하는 수단이 없다. 정책망에 64차원 순환 메모리 셀은 있지만
   (`MemoryStateSize = 64`, 은닉층 1 x 128) 그걸로 맵 구조를 외우기를 기대할 근거가 없다.

**→ 2026-10-01 에 `Hit` 관측 그룹을 넣었다. 1A.8c 참조.**

> ### ★ 스키마를 바꾸는 비용이 지금은 0 이다
> [SH_CombatTrainer.cpp:213-222](../Source/HAI_Project_TEAM5/RL/SH_CombatTrainer.cpp#L213) 가
> 네트워크 4개 전부에 `nullptr` + `bReinit = true` 를 넘긴다 —
> **매 실행이 무작위 초기화에서 시작한다.** 가중치를 보존하기 시작할 때까지는
> 스키마를 바꿔도 잃는 것이 없다. 위험 10번은 그 이후에 적용된다.

### 1A.8c `Hit` 관측 그룹 — 후방 위협 (2026-10-01)

관측 벡터 **42 → 45**. 파이썬 `data-config.json` 과 일치 확인:
`Hit: And size=3` (`HitAngle: Continuous size=2` = sin/cos, `TimeSinceHit: Continuous size=1`).

```
Hit: Struct{ TimeSinceHit:Float(Scale = HitMemoryHorizonSeconds), HitAngle:Angle }
```

| | 어디 |
|---|---|
| 데이터 | `ASH_RLCharacter::LastHitSourceLocation` / `LastHitTime`, `CombatStats->OnDamaged` 에 등록 |
| 접근자 | `GetTimeSinceLastHit()` / `GetLastHitRelativeAngleDeg()` |
| 스키마 | `USH_ShooterInteractor` 의 `Tag_Hit` / `Tag_TimeSinceHit` / `Tag_HitAngle` |
| Scale 동기화 | `ASH_CombatTrainer::Assemble` → `Interactor->HitMemoryHorizonSeconds = Units[0]->HitMemoryHorizonSeconds` |
| 검증 | `ValidateUnitSchema` 가 모든 유닛의 지평값이 같은지 검사 |

**설계 판단 셋.**

1. **방향이 아니라 위치를 저장한다.** 피격 순간 가해자의 **월드 위치**를 넣고 관측할 때마다
   현재 자기 위치에서 방위를 다시 계산한다. 방향만 저장하면 엄폐물 뒤로 **이동한 뒤**
   기억된 위협 방향이 틀린 값이 된다 — **엄폐를 배우게 하려는 관측이 엄폐 행동 때문에
   망가진다.** (`ASH_ScriptBotController::LastKnownAimPoint` 와 같은 방식.)
2. **거리는 주지 않는다.** 방위와 경과 시간만이다. 적 좌표를 주면 조준이 닫힌 형태 해가
   된다는 것이 PLAN.md Part 4 를 폐기한 이유다.
3. **`Optional` 이 아니라 평범한 Struct 다.** "한 번도 안 맞았다" 는
   `TimeSinceHit = 지평값`(포화)으로 표현된다. `Memory` 그룹과 같은 방식이고,
   `Optional` 은 유니온 오버헤드로 2 float 을 더 쓴다 (`Enemy` 가 3 대신 5 를 쓰는 이유).

> ### ★ 조용히 틀릴 수 있던 지점 넷
> 1. **기준 회전은 `GetControlRotation()` 이다.** 몸통 회전은 컨트롤 회전을 뒤늦게
>    따라오므로 그걸 쓰면 관측 방위가 실제 조준선 · 부채꼴과 어긋난다.
> 2. **`Causer == nullptr` 이면 지난 기억을 유지한다.** 0 으로 덮으면
>    "정면에서 맞았다" 가 되어 틀린 방향을 가르친다.
> 3. **죽은 에이전트는 `(지평값, 0도)`** 를 낸다. `0초` 를 넣으면
>    "방금 정면에서 맞았다" 는 반대 뜻이 된다.
> 4. **`ResetForNewMatch()` 에서 비운다.** 안 비우면 지난 판의 피격 방향이 다음 판으로
>    샌다. 유닛은 고정 슬롯으로 되돌아가므로 그 방향은 완전히 무의미한 값이다
>    (버그 3번과 같은 종류).

지평값은 **5초** — 시야 기억(3초)보다 길게 둔다. 맞았다는 사실은 적을 놓친 것보다 오래
유효하고, 엄폐물 뒤로 돌아가 자리를 잡는 시간을 덮어야 한다.

**4분 검증 런**: 스키마 45 확인, 파이썬 `Not Implemented` 0건, 실제 에러 0건,
`ValidateUnitSchema` 에러 0건, 정상 종료 exit 0, 고아 `python.exe` 0개.

> **아직 엄폐가 학습된다는 증거는 없다.** 4분으로는 알 수 없다.
> **(B) 의 성공 판정은 장시간 실행에서 `피해 받음` 이 결정 1k당 1,040 아래로 꺾이는 것이다.**
> 꺾이지 않으면 다음 후보는 보상 쪽이다 — 현재 엄폐 압력은 `DamageTakenWeight 0.7` 하나뿐이고,
> 받는 피해가 주는 피해의 6.9배라 그 신호가 묻혀 있다.

### 1A.7 보상 설계에서 반드시 기억할 것

**`MinimumAdvantage = 0.0`** (`ppo.py:130`) 이라 **음수 advantage 가 전부 0 으로 잘린다.**
벌점은 행동을 직접 억제하지 못한다 — "덜 좋은 것" 을 만들 뿐이다. 원하는 행동은 반드시
**양의 보상**으로 끌어야 한다. 시야 제한 + 엄폐물은 보상이 희소해지는 방향이라 이 문제가 정면으로 나온다.

셰이핑 두 개(`SeeWeight 0.02`, `AlignWeight 0.05` × 정렬도⁴)는 **감쇠시킨다.**
감쇠하지 않으면 "적을 화면에 두기만 하고 쏘지 않는" 정책이 나온다 — 보기만 해도 보상이
들어오는데 쏘면 빗맞힐 위험이 있으니 합리적인 선택이 된다.

### 1A.8 남은 에디터 작업 (팀이 직접)

- [ ] C++ 팔각형 벽 8개의 트랜스폼을 `BP_SHArena` 의 `StaticMeshComponent` 로 옮기고
      `bAutoBuildBoundary = false` 로 끈다 (Collision Preset **반드시 `BlockAll`**).
      트랜스폼 표는 [PLAN.md](PLAN.md) 의 Part 1-5 (b) 에 있다
- [ ] 엄폐물 조정 루프: 레벨 인스턴스에서 옮긴 뒤 Details 패널
      **"Apply Instance Changes to Blueprint"** → 스폰되는 16개 전부에 반영된다

---

## 1B. 2026-10-02 — 10대10 라운드 시스템 (Phase 0 · 1)

> 계획: `C:/Users/c/.claude/plans/5-8-rustling-lagoon.md` (사용자 승인 2026-10-02).
> 목표: 모델 대 모델에서 "서로 숨어서 버티기" 대치가 생기지 않는 구조로, 정식 10대10 라운드를 학습한다.

### 1B.1 사용자 결정

| 항목 | 결정 |
|---|---|
| 시간 초과 판정 | **양쪽 모두 패배** (TFT 와 같다) |
| 강제 규칙 | **축소 구역** |
| 팀 정보 | **아군 위치 + 양 팀 생존 수** (적 위치는 여전히 자기 시야로만) |

### 1B.2 무엇이 바뀌었나

| 항목 | 이전 | 지금 |
|---|---|---|
| 에피소드 | 아레나 안 첫 사망까지 (13~34스텝) | **라운드** = 한 팀 전멸 또는 45초 (평균 170~200스텝) |
| 사망 | Termination | **사망 마스크** — 라운드 끝까지 남고 행동은 무효, 팀 몫·결과 보상은 받는다 |
| 정책 갱신 시 | 전원 리셋, 열린 에피소드 폐기 | 리셋 안 함 (`bResetAgentsOnUpdate = false`) |
| 개인 보상 | 받은 피해 0.7, 사망 1.0 | **제로섬**: 받은 피해 1.0, 사망 0.5 (= 킬) |
| 팀·라운드 보상 | 없음 | 결과 ±2.0, 팀 스피릿 0.3 — **0 에서 램프** (1B.4) + **시계 비용** (1B.6) |
| 관측 | 45 | **50** — `Team{아군 생존, 적 생존, 라운드 진행, 구역 반경, 구역 경계 여유}` |
| 규칙 | 없음 | 상한 45초, 시간 초과 = 양쪽 패배, 20초부터 15초 동안 1,750 → 600 cm 축소, 밖은 초당 10 피해 |
| 스폰 | 고정 슬롯 | 지터 ±3° / ±100 cm (이웃 겹침·엄폐물 겹침 없는 상한) |
| 정책 저장 | **안 됨** | `bSaveSnapshots = true` → `Intermediate/LearningCore/SHCombat<N>/Snapshots/*.bin` |

리포트 셋째 줄: `라운드 n (팀0 승 / 팀1 승 / 시간초과 (%) / 동시전멸) | 평균 길이 | 첫 피해 | 최근접 적`.
대조 실험 스위치: `-SHRoundRules=0|1`, `-SHRoundTime=`, `-SHTeamSpirit=`, `-SHOutcome=`, `-SHRoundRamp=`.

> **`Intermediate/LearningCore/SHCombat<N>` 은 실행마다 새 번호가 생긴다.** 엔진은 기존 폴더를
> 덮어쓰지 않는다. 설정·스냅샷을 볼 때는 로그에 찍힌 경로의 번호를 볼 것.

### 1B.3 ★ 결과 보상을 처음부터 켜면 학습이 멈춘다

같은 라운드 시스템, 10분씩:

| 시점 | 결과 ±2.0 · τ 0.3 (처음부터) | **결과 0 · τ 0** | 5회차 (첫 사망 리셋) |
|---|---|---|---|
| 약 120초 | 2.0% | 1.7% | 3.2% |
| 약 250초 | 1.9% | **6.2%** | 4.7% |
| 약 310초 | 1.8% | **7.6%** | 5.5% |
| 약 440초 | 2.0% | **10.5%** | 약 9% |

학습 전 정책은 매 라운드 지고, 크리틱은 −2.0 이 **언제** 들어올지 맞히지 못한다. 그 오차가 이점의 분산을
키우고 엔진이 이점을 표준편차로 나누므로(`ppo.py`) 명중 1발 같은 작은 신호가 묻힌다.
**라운드 구조 자체는 문제가 아니었다** — 끄자 첫 사망 리셋 방식보다 빨리 배웠다.

> 중간에 "초반 교전이 라운드당 한 번이라 7배 드물어져서" 라는 가설을 세웠는데 이 대조 실험으로 **틀렸다.**

### 1B.4 처방 1 — 램프

`RoundRewardRampSteps = 3,000,000` (기본). 결과 보상과 팀 스피릿을 0 에서 목표값까지 선형으로 올린다.
셰이핑 감쇠와 같은 일정, 에이전트 160 에서 약 30분. 리포트 첫 줄 `라운드 보상 x0.00 → x1.00`.
램프 아래에서 명중률은 대조군과 비슷하게 올랐다(약 300초 8.8%, 545초 11.2%).

### 1B.5 ★★ 엔진 크래시 — 이산 행동을 선택지 하나로 마스킹하면 언젠가 죽는다

`Assertion failed: MaxIdx != INDEX_NONE` (`LearningCore/.../LearningRandom.cpp:497`).

- `DistributionMultinoulliMasked` 는 검벨-최대 샘플링이다: 값 = 로짓 − log(−log u).
- u 는 `Float()` 가 해시 상위 23비트로 만드는 [0, 1) 이라 **정확히 0 일 확률이 2⁻²³** 이다. 그러면 값이 −∞ 다.
- 허용 선택지가 **하나**면 그 하나가 −∞ 일 때 `MaxIdx` 가 정해지지 않아 프로세스가 죽는다.
- 사망 마스크가 회전·이동을 선택지 하나로 막았었다. 죽은 에이전트 약 80 × 행동 2 × 초당 결정 11 = 초당 1,760 샘플
  → 약 1.3시간에 한 번. 세 실행 합계 기대 0.42회, 실제 1회.
- NaN 이 아님을 확인했다: 로그에 NaN 이 없고, 새 가중치를 받은 뒤 결정 17회를 정상 샘플링한 다음에 죽었다.

**고침 1**: 죽은 에이전트에 선택지를 **둘** 남긴다(행동은 어차피 버려진다). 확률 (2⁻²³)² 로 사실상 0.

**고침 1 뒤에도 self-play 에서 4회 더 크래시했다(3~13분 간격).** 원인은 **발사 마스크**였다.
- `SpecifyBoolAction` 은 내부적으로 `SpecifyExclusiveDiscreteAction(2, …)` — 선택지 2개짜리 배타적 이산 행동이다.
- `MakeBoolActionModifier(false)` 는 "참" 인덱스를 막는 마스크라 **허용 선택지가 하나만 남는다.**
- 이 마스크를 쿨다운 중인 살아 있는 에이전트(발사 간격 0.15초 > 결정 간격 0.05초라 연사 중이면 결정 대부분이 쿨다운)와
  죽은 에이전트 전원에 매 결정 걸고 있었다. 에이전트 320명 self-play 에서는 초당 수천 번이다.
- 관측·인코딩·정책 메모리에 비유한 값이 없음을 계측으로 확인했다(`ScanForNonFinite`, 정책 출력 가드 — 한 번도 발동 안 함).
  즉 NaN 이 아니라 검벨 u = 0 사건이다.

**고침 2 (2026-10-02)**: 발사에는 **마스크를 걸지 않는다**(`MakeNullActionModifier`). 쿨다운 중에는
`AGun::PullTrigger` 가 `CanFire()` 확인에서 바로 돌아가므로(`Gun.cpp:119`) 게임 동작은 같다.
정책 출력 가드와 `RunTraining` 수동 분해는 전제가 틀렸으므로 제거하고 엔진 경로로 되돌렸다.
**규칙: 어떤 이산 행동이든 허용 선택지가 하나만 남는 마스크를 만들지 않는다 — 불리언 포함.**

크래시는 학습 python 을 고아로 남긴다 — 실행 후 `Intermediate\PipInstall\Scripts\python.exe` 프로세스를 확인할 것.

### 1B.6 시계 비용 — 유도상 필요하지만 봇 상대로는 효과가 측정되지 않았다

처음에는 램프가 0.35 를 넘자 교전이 줄었다고 읽었다 (30분 실행, 크래시 수정 후):

| 시각 | 램프 | 명중률 | 적 시야 (살아 있는 유닛 기준) | 준 피해 | 받은 피해 | 라운드 길이 |
|---|---|---|---|---|---|---|
| 545초 | 0.33 | **11.2%** | 35% | **103** | 385 | 12.9초 |
| 914초 | 0.55 | 8.7% | 26% | 80 | 235 | 22.9초 |
| 1,519초 | 0.90 | 9.6% | 27% | — | — | 19.1초 |

**원인은 할인이다.** 라운드 끝의 −W 는 γ^d 로 할인되므로 **질 것이 확실하면 늦게 질수록 보상이 크다.**
γ = 0.99 에서 100스텝(5초) 늦추면 벌점이 63% 줄어든다. 봇에게 늘 지는 지금은 교전을 피해 패배를 미루는 것이
실제로 이득이다 — **사용자가 걱정한 대치 그 자체**다. 계획서의 "시간 초과 = 양쪽 패배면 대치는 균형점이 아니다"
논리는 할인을 빼먹었다. 지는 쪽에게는 45초 뒤의 시간 초과 −W 도 할인된다.

**처방**: 라운드 진행 중 **모든 에이전트(죽은 쪽 포함)** 에게 스텝마다 c = W·(1 − γ) 를 뺀다
(`bRoundClockCost`, γ 는 학습 설정에서 동기화). 그러면

```
d스텝 뒤 패배:  −c(1 − γ^d)/(1 − γ) − W γ^d = −W            (시점과 무관)
d스텝 뒤 승리:   W(2γ^d − 1)                                 (빨리 이길수록 크다)
승률 p 로 밀기:  W(2pγ^d − 1) > −W   for p > 0               (버티기보다 항상 낫다)
```

죽은 에이전트에게도 매기는 이유: 산 쪽만 내면 "빨리 죽어서 비용을 피하기" 가 이득이 된다.

> **그런데 실측으로는 확인되지 않았다.** 전체 30분을 다시 보니 시계 비용 없는 실행은 800초 이후
> **꺾인 것이 아니라 정체**였다(명중률 8.2~9.9%, 살아 있는 기준 시야 24~29%, 라운드 16~23초).
> 위 표의 "하락" 은 정체가 시작된 지점을 잘못 읽은 것이다. 시계 비용을 켠 25분 실행은 오히려
> 더 수동적으로 갔다(라운드 11.8 → 28.2초, 시야 36% → 18%). 실행이 하나씩이라 분산과 구분할 수 없다.
>
> 시계 비용은 **결과 보상**의 할인 이득만 상쇄한다. 사망 벌점과 받은 피해도 확정된 미래 손실이면
> 늦출수록 할인되므로, 거의 확실히 지는 상대 앞에서는 "숨어서 죽음을 미루기" 가 여전히 이득이다.
> 이 설명은 검증하지 않았다. 시계 비용은 유도가 맞으므로 켜 둔다(`bRoundClockCost = true`).
> **효과를 시험할 곳은 승패가 실제로 갈리는 self-play 의 A/B 실험이다.**

### 1B.7 ★ 현재의 진짜 문제 — 상대가 너무 강하다

| | 5회차 (첫 사망 리셋) | 라운드 시스템 (10대10 끝까지) |
|---|---|---|
| 명중률 | 30분 약 11%, 90분 이상 22% 까지 상승 | 5~10분에 약 9% 후 **정체** |
| 이동 분포 | 20~35분에 좌 66% 로 깨짐 | 30분 내내 **20/20/20/20/20** |
| 라운드 승리 | — | 5개 실행 2,979 라운드 **전패** |

명중률 65% 스크립트 봇 10명에게 RL 팀은 한 번도 이기지 못했다. 그러면 결과 보상은 늘 −W 라서
이기는 행동과 지는 행동을 구별해 주지 못하고, 팀 스피릿은 "다 같이 지는" 신호만 섞는다.
양상: RL 유닛 대부분이 일찍 죽고(생존 39~45%) 남은 몇 명이 오래 버텨 라운드가 길어진다.

**확인된 것**: 라운드·구역·사망 마스크·제로섬·램프·시계 비용·스냅샷이 모두 기계적으로 동작한다
(관측 50 일치, 엔진 경고는 256스텝 경계 1회뿐, 크래시 수정 후 25분 무사고, 정상 종료, 고아 0).

### 1B.8 self-play 대치 A/B (2026-10-02) — 결론 보류, 비교 조건이 섞였다

같은 코드, 25분씩. 대조 `sp_A4_off` = 규칙 끔(시간 초과 무승부, 구역 없음, 시계 비용 없음). 처치 `sp_B2_on` = 규칙 켬.
처치는 발사 마스크 수정(1B.5 고침 2) 뒤 처음으로 25분 무사고·정상 종료.

| 지표 (15~25분 평균) | 대조 (규칙 끔) | 처치 (규칙 켬) |
|---|---|---|
| 시간 초과 | 라운드의 **94~100%** | **0~6%** |
| 라운드 길이 | 45.0초 (상한) | 38~40초 (구역이 다 줄어드는 35초 직후) |
| 받은 피해 중 구역 비율 | — | 31% (7분) → **47~54%** |
| 교전 피해 (결정 1k당 준 피해) | 85~94 | 87 (7분) → **60~67** |
| 최근접 적 거리 | 1,340~1,520 cm | 1,380 (7분) → **1,760~1,900 cm** |
| 명중률 | 6.0~7.3% | 6.7% (7분) → **4.1~4.5%** |
| 이동 분포 | 전진 26~29% (학습됨) | 전진 27% (3분) → **20/20/20/20/20** |

- 규칙은 라운드를 **끝내기는** 한다. 하지만 끝내는 건 교전이 아니라 **구역**이다.
- 처치가 나빠진 시점이 라운드 보상 램프가 1.0 에 닿은 시점(약 11분)과 겹친다. 이동이 균등 분포로
  되돌아간 것은 1B.7(봇 상대 라운드 시스템, 이동 30분 내내 균등)과 같은 양상이다.
- **해석 (검증 안 됨)**: 대조는 거의 모든 라운드가 무승부라 결과 보상이 사실상 0 이다. 처치는 매 라운드
  ±2.0 이 붙는다. 같은 정책끼리의 승패는 개별 행동과 상관이 약해 어드밴티지에 잡음만 더하고(1B.3 과 같은 기전),
  이동 헤드의 신호가 묻혀 엔트로피 항이 분포를 균등으로 끈다. 그러면 "대치"가 아니라 **무작위 이동**이라
  거리가 벌어지고 구역에 죽는다.
- 즉 이 A/B 는 "규칙" 과 "결과 보상 잡음" 두 변수를 같이 바꿨다. **대치 방지 효과는 아직 판정할 수 없다.**

**다음 실험 후보**: 규칙 켬 + `-SHOutcome=0 -SHTeamSpirit=0` (구역·시간 초과는 그대로, 결과·팀 보상만 끔).
이동이 학습되고 거리가 줄면 잡음이 원인이다. 그다음 결과 보상을 작게(0.5) 다시 넣어 본다.

## 2. 현재 게이트 상태

| 게이트 | 내용 | 상태 |
|---|---|---|
| **0a** | GPU 배관 (`torch.cuda`) | ✅ 통과 |
| **0c** | Hello-World RL 루프 | ✅ 통과 (**정상 종료 경로 제외**) |
| **1** | 히트스캔 · HP · 팀 · 사망 | ✅ 통과 (봇 vs 봇 1v1 실측) |
| **0c** | 정상 종료 경로 (`EndPlay` → `EndTraining()`) | ✅ 통과 (2026-09-29, 고아 `python.exe` 0개) |
| **2** | 16 아레나 봇 전투 | ✅ 통과 — 스폰·누출(CROSS-GYM 0)·풀링·리셋·종료 전부 실측 |
| **3a** | 스키마 동결 | ✅ **동결** — 관측 42 / 행동 12 / 모디파이어 16 |
| **3b** | 슈터 정책이 기준선을 벗어남 | ✅ 통과 — 명중률 1.3% → 7.7% |
| **4** | 커리큘럼 (FOV 360→180→120→90, 승률 기준 재설정) | ⬜ 미착수 |
| 5~7 | self-play · 3v3 · 출고 | ⬜ 미착수 |

---

## 3. ~~즉시 할 일 — Gate 2 마무리~~ (2026-09-29 완료)

> **이 장은 전부 끝났다.** 아래는 다음에 같은 작업을 할 때를 위한 함정 목록으로 남겨 둔다.
> 유닛 BP 의 부모는 이제 `BP_BaseCharacter` 가 아니라 `SH_RLCharacter` 다.

- [x] **`BP_Unit_Team0` / `BP_Unit_Team1` 생성** (`BP_BaseCharacter` 상속, 서로 다른 두 클래스)
  - `CombatStats.TeamId` = `0` / `1` ← **이걸 빠뜨리면 조용히 전투가 안 일어난다**
  - `AIControllerClass` = `SH_RLBotController`(Team0) / `SH_ScriptBotController`(Team1)
        ← **이 한 줄이 커리큘럼 스위치다.** `ASH_CombatTrainer` 는 컨트롤러 클래스로
        에이전트를 판별하므로, 2단계(self-play)는 Team1 도 RL 컨트롤러로 바꾸면 된다. 코드 수정 없음
  - `AutoPossessAI = Placed in World or Spawned` ← **`Placed in World`(엔진 기본값)면 아레나가 스폰한 유닛에 AI가 안 붙는다. 에러 없음**
  - `SpawnCollisionHandlingMethod = ALWAYS_SPAWN` ← **빠뜨리면 엔진이 크래시한다** (1A.4 의 버그 2)
  - ⚠️ **BP 기본값(CDO)을 바꿔도 이미 배치된 인스턴스에는 전파되지 않는다.** 인스턴스도 확인할 것
- [x] `BP_SHArena` 생성 → `Team0UnitClass` / `Team1UnitClass` 지정, `UnitsPerTeam = 10`
- [x] `EntitiesManager`: `EpisodeEntitySpawnCountMin == Max` (**둘이 다르면 에피소드 도중에 새 액터가 스폰되고, 그 폰은 `AddAgent`를 안 거쳐서 정책이 영원히 무시한다**), `EntitySpawnZOffset = 100`
- [x] `SHGymsManager.GymTemplates`를 `BP_SHArena`로 교체, 수동 배치한 `TestUnit_*` 제거
      (`GymTemplates` 는 `private` + BP 노출 없음 → **Details 패널에서 손으로** 넣어야 한다.
      코드로는 못 채우고, 빠뜨리면 아레나 0개로 조용히 진행된다)
- [x] **누출 검증** — `CROSS-GYM 누적 : 0`. ⚠️ **반드시 아레나가 스폰한 유닛으로.**
      수동 배치 유닛은 `!OwnerGym && !OtherGym` 폴백에 걸려 **항상 통과해버린다**
- [x] **풀링 검증** — 320/320 평평, 성장 경고 없음
- [x] **리셋 검증** — 죽었다 리셋된 유닛이 다시 죽는다 (아레나 전체 리셋 경로)
- [ ] 관전 모드 비행 조작 실검증 (실제 키 입력 필요) ← **아직 안 했다**
- [x] 처리량 재측정 — `ASH_RLCharacter::bTrainingMode` 가 애니메이션 틱과 SpringArm 스윕을 끈다

> **에피소드 리셋 주체는 `USH_ShooterTrainingEnv::ResetAgentEpisode` 다.**
> 같은 프레임에 20개 에이전트가 각자 부르므로 `GFrameCounter` 로 중복 제거한다 —
> 안 하면 한 프레임에 같은 아레나를 20번 리셋한다.

### 그 외 정리

- [x] **git 커밋** — 커밋됨 (`eb9a815 basic` 등). 브랜치 `traintest`
- [ ] `Intermediate/PipInstall`(~3GB)이 `.gitignore`에 들어 있는지 확인
- [ ] Tensorboard 설치: `Intermediate\PipInstall\Scripts\python.exe -m pip install tensorboard`
      ← **아직 안 했다.** `bUseTensorboard = true` 로 둬도 로그가 안 나온다
- [x] **정상 종료 경로 검증** — `MaxWallClockSeconds` 로 자연 종료, exit 0, 고아 `python.exe` 0개

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
| 1 | 조준 학습이 수렴하지 않음 | **기준점 두 개로 판단한다: 무작위 1.3% / 스크립트 봇 65%.** 2026-09-29 실측 11분에 7.7% — 학습은 된다. 4시간 후 20% 미만이면 셰이핑 감쇠 속도(`ShapingDecaySteps`)와 `DiscountFactor` 를 본다 |
| 2 | 인터넷 자료가 전부 5.3~5.5 API | experimental 0.2, 엔진에 문서·샘플 0. **헤더와 `ppo.py`가 유일한 문서** |
| 3 | 엔진 업그레이드 | 프로젝트 중 금지. **1.1의 GPU 패치가 날아간다** |
| 4 | GPU 패치가 팀원마다 다름 | 프로젝트 로컬 복사라 git으로 전파된다. **4명 전원이 Gate 0a를 통과해야** |
| 5 | 해시 검사 OFF = 공급망 위험 | `OverrideIndexURL` 절대 금지, 공식 인덱스만 |
| 6 | GPU/CPU 결과 불일치 | 같은 시드도 디바이스가 다르면 궤적이 다르다. **비교 실험은 동일 디바이스에서만** |
| 7 | `EndTraining()` 누락 | 에디터가 고정 타임스텝에 남고 `python.exe`가 떠돈다. **오늘 실제로 발생** |
| 8 | 경로에 한글·공백 금지 | 파이썬 서브프로세스 argv 문제 |
| 9 | 시드 5개 (PPO / Policy / Critic / Gym / GymsManager) | 전부 설정하고 **로깅**. 없으면 보고서를 못 쓴다 |
| 10 | **스키마 해시 동결** | 관측 42 / 행동 12 로 굳었다. `SHRL::RayCount`, `TurnBins`, `MoveBins`, `EnemyEncodingSize`, 태그 문자열 중 **하나라도** 바꾸면 NN 에셋 4개가 무효가 된다. `USH_VisionComponent::RayCount` 도 같은 값이어야 한다 — `ASH_CombatTrainer::ValidateUnitSchema` 가 검사한다 |
| 11 | **태그 문자열 불일치** | Specify 와 Gather 사이에서 태그가 어긋나면 **해시는 통과하면서 의미만 뒤바뀐다** (보상 0, 에러 없음). `data-config.json` 에 태그 이름이 전혀 안 실려서 파이썬도 못 잡는다. 그래서 태그를 `.cpp` 한 곳에만 둔다 |
| 12 | **`Conv1d` 사용 금지** | 파이썬 트레이너에 구현이 없다. 1A.3 참조 |

---

## 7. 파일 위치

| 무엇 | 어디 |
|---|---|
| **상세 설계 계획서** | [Docs/PLAN.md](PLAN.md) |
| 아레나 | [Source/HAI_Project_TEAM5/RL/SH_Arena.h](../Source/HAI_Project_TEAM5/RL/SH_Arena.h) |
| 디렉터 | [Source/HAI_Project_TEAM5/RL/SH_RLDirector.h](../Source/HAI_Project_TEAM5/RL/SH_RLDirector.h) |
| 스크립트 봇 | [Source/HAI_Project_TEAM5/RL/SH_ScriptBotController.h](../Source/HAI_Project_TEAM5/RL/SH_ScriptBotController.h) |
| 시야 센서 | [Source/HAI_Project_TEAM5/RL/SH_VisionComponent.h](../Source/HAI_Project_TEAM5/RL/SH_VisionComponent.h) |
| 학습용 캐릭터 | [Source/HAI_Project_TEAM5/RL/SH_RLCharacter.h](../Source/HAI_Project_TEAM5/RL/SH_RLCharacter.h) |
| RL 컨트롤러 | [Source/HAI_Project_TEAM5/RL/SH_RLBotController.h](../Source/HAI_Project_TEAM5/RL/SH_RLBotController.h) |
| 스키마 (관측/행동) | [Source/HAI_Project_TEAM5/RL/SH_ShooterInteractor.h](../Source/HAI_Project_TEAM5/RL/SH_ShooterInteractor.h) |
| 보상 · 완료 · 리셋 | [Source/HAI_Project_TEAM5/RL/SH_ShooterTrainingEnv.h](../Source/HAI_Project_TEAM5/RL/SH_ShooterTrainingEnv.h) |
| 슈터 학습기 | [Source/HAI_Project_TEAM5/RL/SH_CombatTrainer.h](../Source/HAI_Project_TEAM5/RL/SH_CombatTrainer.h) |
| Hello-World 학습기 | [Source/HAI_Project_TEAM5/RL/SH_HelloWorldTrainer.h](../Source/HAI_Project_TEAM5/RL/SH_HelloWorldTrainer.h) |
| HP · 팀 · 보상 누산기 | [Source/HAI_Project_TEAM5/Combat/CombatStatsComponent.h](../Source/HAI_Project_TEAM5/Combat/CombatStatsComponent.h) |
| 히트스캔 | [Source/HAI_Project_TEAM5/Gun.h](../Source/HAI_Project_TEAM5/Gun.h) |
| 학습 맵 (슈터) | `/Game/RL/Lvl_SHTrain` |
| 학습 맵 (Hello-World) | `/Game/RL/Lvl_SHHello` |
| GPU 패치 | `Plugins/PythonMLPackages/`, `Plugins/PythonFoundationPackages/` |
| 트레이너 설정 (파이썬이 실제로 읽는 파일) | `Intermediate/LearningCore/<TaskName>0/Configs/trainer-config.json` |

### 헤드리스 학습 실행

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\UnrealProjects\HAI_Project_TEAM5\HAI_Project_TEAM5.uproject" /Game/RL/Lvl_SHTrain -game -log -abslog=C:\temp\train.log -unattended -nosplash -nullrhi -nosound -SHTrainSeconds=5400 -SHReportInterval=60
```

**함정 넷.**

1. **`UnrealEditor.exe` 가 아니라 `UnrealEditor-Cmd.exe` 다.** 그리고
   `HAI_Project_TEAM5.exe`(standalone)로는 안 된다 — 셰이더를 쿠킹하지 않았으므로
   `FShaderCodeLibrary::InitForRuntime` 에서 실패한다
2. **에디터를 닫아야 한다.** Live Coding 이 `UnrealEditor-HAI_Project_TEAM5.dll` 을 잡고 있어
   빌드가 실패한다
3. **Git Bash 에서는 `MSYS_NO_PATHCONV=1` 을 붙여야 한다.** 안 붙이면 MSYS 가
   `/Game/RL/Lvl_SHTrain` 을 `C:/Program Files/Git/Game/RL/Lvl_SHTrain` 으로 바꿔 버린다
4. **실행 길이는 커맨드라인으로 준다** (`-SHTrainSeconds=`, `-SHReportInterval=`).
   레벨 에셋의 `MaxWallClockSeconds` 를 고치려면 에디터를 열어야 하고 `.umap` 이 더러워진다
