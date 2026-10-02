# 슈터 오토배틀러 RL 구축 계획 (UE 5.8 / LearningAgents)

> **이 문서가 정본이다.** 2026-09-17 에 플랜 모드 개인 디렉터리에서 저장소로 옮겼다.
> 그 전까지는 PC 한 대에만 있어서 팀원이 볼 수 없었다. 앞으로는 여기만 고친다.
> 진행 상황과 다음에 할 일은 [TODO.md](TODO.md) 를 볼 것.

> ## ⚠ 2026-09-29 — Part 4(스키마)는 폐기됐다
>
> **원인은 실측이다.** 스크립트 봇의 명중률이 **100%** 로 나왔다 (팀0 10발 10명중,
> 팀1 9발 9명중). 적 좌표를 관측에 주면 조준 방향이
> `(EnemyLocation - MuzzleLocation).GetSafeNormal()` 한 줄로 나오는 **닫힌 형태 해**이고,
> 히트스캔이라 리드 사격도 필요 없다. 학습된 정책이 이길 여지가 없다.
>
> 그래서 **시야각을 제한하고 적 좌표를 관측에서 뺐다.** 부분 관측이 되면서
> 탐색 · 기억 · 추종 · 엄폐가 실제 학습 내용이 된다.
>
> | | 폐기된 Part 4 | 실제로 동결된 스키마 |
> |---|---|---|
> | 적 정보 | 좌표 · 속도 · `YawError` · `PitchError` (전지적) | **부채꼴 16레이 + Optional(각도, 깊이)** |
> | 행동 | `DirectionAction` (절대 방향) | **`ExclusiveDiscrete(5)` 각속도** — 절대 각도가 아니다 |
> | `Target` 슬롯 | 있음 | **없음** (누구를 노리는지도 정책이 정한다) |
> | 관측 벡터 | 미확정 | **42** (인코딩 71) / 행동 12 / 모디파이어 16 |
>
> 실제 스키마는 [SH_ShooterInteractor.h](../Source/HAI_Project_TEAM5/RL/SH_ShooterInteractor.h) 가
> 정본이고, 요약과 배경은 [TODO.md](TODO.md) 의 1A 장에 있다.
> **Part 5(보상) · Part 6(커리큘럼) · Part 7(처리량)은 그대로 유효하다.**
>
> 계획에서 바뀐 것 하나 더: **`Conv1d` 를 쓰지 않는다.** 파이썬 트레이너에 구현이 없다
> (`train_common.py` 의 obs_type 디스패처가 `And / OrExclusive / OrInclusive / Array / Set /
> Encoding` 만 분기하고 `else: raise Exception('Not Implemented')` 로 끝난다).
> C++ 은 JSON 으로 내려보내지만 파이썬이 모듈을 만들지 않는다. `StaticArray` 를 쓴다.

## Context

`C:\UnrealProjects\HAI_Project_TEAM5`. TFT형 오토배틀러 — 유닛을 배치하면 전투가 자동으로 진행되고,
유닛의 전투 행동을 PPO로 학습시킨다.

**이전 계획은 Epic의 근접 템플릿(`Variant_Combat`) 기준이었고, 그 전제는 폐기됐다.**
팀이 자체 캐릭터(`ABase_Character` + `AGun`, ParagonWraith 메시 + ShooterGame 라이플)를 만들었고
`Variant_Combat`을 참조하는 신규 에셋은 **0개**다. 이 계획은 슈터 기준으로 다시 쓴 것이다.

### 확정된 결정 (재론하지 않음)

| 항목 | 결정 |
|---|---|
| 게임 형태 | **오토배틀러** — 전투 중 플레이어 조작 없음 |
| 구현 순서 | **슈터 먼저**, 근접은 나중에 별도 아키타입 |
| 조준 | **정책이 조준까지** (오토 조준은 스위치로 병행) |
| 명중 판정 | **히트스캔** |
| 스키마 | 근접과 **분리** — 슈터 전용 |
| 학습 아레나 | **빈 원형** — 엄폐물은 3v3 단계에서 |
| 프레임워크 | LearningAgents + PPO, 아키타입별 개별 정책, 1v1 → 3v3 |

### ⚠️ 난이도에 대한 정직한 고지

"정책이 조준까지"는 앞서 고른 "전술 판단만"보다 **한 자릿수 이상 어렵습니다.**
오토 조준이면 1~2M 스텝에 수렴하지만, 조준을 학습하면 **5~20M 스텝**을 봐야 합니다.
벽시계로 1시간 → **하룻밤~이틀**. 그래서 오토 조준 스위치를 병행합니다.

---

## Part 0. 현재 상태 — 무엇이 있고 무엇이 없나

### 있는 것

- `ABase_Character : AHAI_Project_TEAM5Character` — 총 스폰 → `WeaponSocket` 부착, `weapon_r` 본 숨김
- `AGun : AActor` — `SceneRoot` + `Mesh`(`/Game/ShooterGame/Weapons/Rifle`)
- `BP_GameMode` → `DefaultPawnClass = BP_BaseCharacter_C` (이미 `GlobalDefaultGameMode`)
- **ParagonWraith 애니메이션 142개** — `Fire_A_Fast_V1`, `Fire_A_Slow_Montage`, `HitReact_Front/Back/Left/Right`,
  `Death_Forward/Backward`, 조그 세트 전체, `AimOffset/` 9방향 세트
- **`Rifle.uasset`에 `MuzzleFlashSocket` 존재** — 히트스캔 원점 확보 <!-- 검증됨 -->
- **`Wraith_Skeleton`에 `WeaponSocket` 소켓 + `weapon_r`/`weapon_l` 본 존재** <!-- 검증됨 -->
- ShooterGame FX/사운드: `P_AssaultRifle_MF`, `P_AssaultRifle_Trail`, `P_AssaultRifle_IH`, `AssaultRifle_Shot01-06`
- `ACombatDummy` — **진짜 무적** (`HandleDeath()`가 빈 함수, HP 없음). 0단계 표적으로 재사용 <!-- 검증됨 -->
- 모듈 컴파일 클린 (sarif 45개 전부 `results: []`)

### 없는 것 — 전부 새로 만들어야 함

체력 · 데미지 · 사망 · 팀 · 점수 · 탄약 · 에피소드 상태 / 적 · AI 컨트롤러 · 스포너 / NavMesh /
전용 학습 맵 / **`PullTrigger()`의 실제 판정** (현재 `UE_LOG` 한 줄)

### 해결된 것 <!-- 2026-09-17 재검증 -->

- `IA_Focus`, `IA_Shoot` 디스크에 존재. `IMC_Character`가 `IA_Focus→RMB`, `IA_Shoot→LMB`,
  `IA_Look`/`IA_MouseLook`→`Mouse2D` 매핑 보유
- `BP_BaseCharacter`가 `FocusAction`/`ShootAction`/`GunClass(BP_Gun_C)` 전부 지정
- `ABase_Character`에 `bIsFocusing` + `StartFocus`/`StopFocus` 존재 (우클릭 조준 완료)

### ✅ Part 0.5 — 현재 `BP_BaseCharacter`로 RL 학습이 가능한가

**결론: 구조적으로 가능하다. 지금 당장은 불가능하다.** 막는 것은 9개이고 그중 4개는 몇 줄이다.

#### 구조적으로 문제 없는 것 (바꿀 필요 없음)

| 항목 | 왜 괜찮은가 |
|---|---|
| `ACharacter` + `CharacterMovementComponent` 기반 | `AddMovementInput`은 **NavMesh 없이** 동작한다. `MoveTo`/`RequestPathMove`만 NavMesh를 탄다 |
| Enhanced Input을 쓰는 점 | AI가 possess하면 `SetupPlayerInputComponent`가 **호출조차 되지 않는다**. 학습에 무해 |
| `BeginPlay`의 총 스폰 + `HideBoneByName` | 헤드리스(`-nullrhi`)에서도 동작 |
| 부모가 `UCLASS(abstract)` | `BP_BaseCharacter`는 구상 클래스라 스폰 가능 |
| 메시·애니메이션 유무 | 히트스캔은 코드에서 트레이스하므로 학습과 무관 |

#### 차단 요소 — 우선순위 순

| # | 위치 | 문제 | 증상 |
|---|---|---|---|
| **B1** | `ABase_Character` | **`ILearningAgentsEntityInterface` 미구현** | `CheckEntityClasses()`가 Error를 찍고 **유닛을 하나도 스폰하지 않은 채 진행**. 에이전트 0개로 학습이 돈다 |
| **B2** | `BP_BaseCharacter` | **`AutoPossessAI`를 `PlacedInWorldOrSpawned`로** <!-- 2026-09-17 정정 --> | 엔진 기본값은 `Disabled`가 아니라 **`PlacedInWorld`** 다. 레벨에 직접 놓은 유닛은 원래부터 AI가 붙는다. 하지만 **아레나가 런타임에 스폰하는 유닛은 possess되지 않아** 컨트롤러가 없고, `DoMove`의 `GetController() != nullptr`가 실패해 **이동 명령이 전부 무시된다. 에러 없음** |
| **B3** | `.uproject` / `Build.cs` | LearningAgents 4개 플러그인 미활성, 모듈 의존성 없음 | 헤더 자체가 없어 컴파일 불가 |
| **B4** | `AGun::PullTrigger()` | `UE_LOG` 한 줄 | 보상 신호 0. *곧 구현 예정* |
| **B5** | — | HP·데미지·팀·사망 없음 | 완료 조건·보상 항 전부 성립 불가. *곧 구현 예정* |
| **B6** | 부모 생성자 | `bOrientRotationToMovement = true` | 컨트롤러 조준을 `PhysicsRotation()`이 매 프레임 덮어씀 |
| **B7** | `ABase_Character` | `GetMesh()->VisibilityBasedAnimTickOption` 기본값 | 오프스크린/`-nullrhi`에서 포즈 틱 정지 → **소켓 위치가 얼어붙음** |
| **B8** | `Base_Character.h:41` | `AGun* gun` — `UPROPERTY` 없음 | 16 아레나 × N 유닛이면 GC가 확률적으로 총을 수거한다 |
| **B9** | 레벨 | 전용 학습 맵 없음 (`Lvl_ThirdPerson`은 WP + 액터 110개) | Part 3.6 |

**B1·B2가 진짜 관문이다.** 나머지는 이미 계획에 있거나 곧 구현될 것이고, B1·B2는 **둘 다 실패해도
에러를 내지 않는다** — "학습이 안 된다"는 하나의 증상으로 위장한다.

`AutoPossessAI`는 `BP_BaseCharacter` 디테일 → Pawn → **`Placed in World or Spawned`** 로 바꾸면 끝이다.
기본 `AIControllerClass`(`AAIController`)면 충분하다 — StateTree도 BehaviorTree도 붙이지 않는다.

> **MCP 로 BP 기본값을 바꿀 때 주의** <!-- 2026-09-17 실측 -->
> `Default__BP_BaseCharacter_C` 의 CDO 에 `AIControllerClass` 를 설정하고 읽어서 확인까지 했는데,
> **그 뒤에 배치한 액터 인스턴스에는 전파되지 않았다** (기본 `AAIController` 가 붙었다).
> BP 기본값을 바꿨으면 **배치된 인스턴스 쪽도 반드시 확인**하고, 필요하면 인스턴스에 직접 설정할 것.

### 즉시 고칠 것 (Week 1 시작 전)

| 위치 | 문제 | 조치 |
|---|---|---|
| `BP_BaseCharacter` | `AutoPossessAI` (B2) | `Placed in World or Spawned` |
| `Base_Character.h:41` | `AGun* gun` — `UPROPERTY` 없음 (B8) | `UPROPERTY() TObjectPtr<AGun> gun;` |
| `Base_Character.cpp` | `#include "Engine/World.h"` 없음 (SharedPCH 덕에 우연히 컴파일됨) | include 추가 |
| `AHAI_Project_TEAM5Character` 생성자 | `bOrientRotationToMovement = true` (B6) | Part 2.3에서 해결 |

---

## Part 1. 환경 준비 (Week 1) — 게임 코드 없음

**이 파트는 이전 계획에서 그대로 가져온다.** 슈터/근접과 무관하다.

### 1.1 플러그인 + Build.cs

`.uproject`에 추가:
```json
{ "Name": "LearningAgents",     "Enabled": true },
{ "Name": "LearningCore",       "Enabled": true },
{ "Name": "NNERuntimeBasicCpu", "Enabled": true },
{ "Name": "PythonMLPackages",   "Enabled": true, "TargetAllowList": ["Editor"] }
```
> 첫 활성화 시 **PyTorch 약 2.5GB 다운로드**, 에디터 10~20분 정지. 의도적으로 세션 시작에 실행할 것.

`Source/HAI_Project_TEAM5/HAI_Project_TEAM5.Build.cs` — 현재 스톡 그대로이므로 추가 필요:

> **★ `LearningTraining` 을 빼면 컴파일이 안 된다** <!-- 2026-09-17 실측 -->
> `LearningAgentsTrainer.h` 는 `LearningAgentsTraining` 의 **public** 헤더인데 첫 줄이
> `#include "LearningTrainer.h"` 다. 그런데 `LearningAgentsTraining.Build.cs` 는
> `LearningTraining` 을 **private 로만** 의존한다. 그래서 그 헤더를 쓰는 쪽에서
> `fatal error C1083: 'LearningTrainer.h'` 가 난다. 우리가 직접 넣어야 한다.
> (`FLearningAgentsTrainerProcessSettings` / `FLearningAgentsTrainingGameSettings` /
> `ELearningAgentsTrainingDevice` 가 전부 그 헤더에 있으므로 피해갈 수 없다.)

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    /* 기존 유지 */
    "NavigationSystem", "GameplayTags",   // LearningAgentsTraining의 public 의존
    "PhysicsCore",                        // 히트스캔 트레이스 채널
    "LearningAgents",                     // 런타임
    "LearningAgentsTraining",             // Gym / TrainingEnv / PPOTrainer  ※ Type: Runtime 이다
    "LearningTraining",                   // ★ 2026-09-17 추가 — 아래 참조
});
if (Target.Configuration != UnrealTargetConfiguration.Shipping) {
    PublicDependencyModuleNames.Add("LearningAgentsTraining");   // Gym, TrainingEnv, PPOTrainer
    PublicDefinitions.Add("WITH_SHOOTER_RL_TRAINING=1");
} else {
    PublicDefinitions.Add("WITH_SHOOTER_RL_TRAINING=0");
}
PublicIncludePaths.Add("HAI_Project_TEAM5/RL");
```

### 1.1b ★ RTX 50 시리즈 GPU 학습 활성화 <!-- 2026-09-17 추가 -->

**이전 계획의 "GPU 학습 불가" 판정은 철회한다.** 활성화할 수 있다.
(출처: [UE 포럼 #2682865](https://forums.unrealengine.com/t/is-it-possible-to-use-an-gpu-rtx-5000-series-for-learning-agents-there-is-a-problem/2682865/6),
5.8 엔진 파일과 대조 검증함.)

**문제.** `PythonMLPackages.uplugin`의 `Win64` 블록이 `torch==2.5.1+cu124`를 핀으로 박는다.
cu124 휠에는 Blackwell(**sm_120**) 커널이 없다. → cu128 휠로 갈아끼운다.

#### 방법 A — 프로젝트 로컬 복사 (**권장**)

두 플러그인 모두 **C++ 모듈이 0개**인 순수 메타데이터 플러그인이다 (`.uplugin`에 `Modules` 키 자체가 없음).
프로젝트로 복사해도 컴파일 비용이 전혀 없다.

```
Engine/Plugins/Experimental/PythonMLPackages/          →  <Project>/Plugins/PythonMLPackages/
Engine/Plugins/Experimental/PythonFoundationPackages/  →  <Project>/Plugins/PythonFoundationPackages/
```

프로젝트 플러그인이 엔진 플러그인보다 **기본적으로 우선한다** (`PluginManager.cpp:1281`, `:1293`).
로그에서 확인할 것:
```
LogPluginManager: Display: By default, prioritizing project plugin
  (.../Plugins/PythonMLPackages/PythonMLPackages.uplugin)
  over the corresponding engine version (...)
```
복사본의 `"Version"` 숫자를 1 올려둔다 — 버전 비교 분기(`:1302`)에서도 프로젝트본이 이기도록.

> **장점: git에 들어간다.** 4명이 각자 엔진을 고칠 필요가 없고, 엔진 재설치·업데이트에도 살아남는다.
> 엔진을 건드리는 변경을 버전 관리 밖에 두면 "내 PC에서만 된다"가 반드시 생긴다.

#### 방법 B — 엔진 파일 직접 수정 (포럼 원문)

`C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Experimental\...` 를 직접 고친다.
관리자 권한이 필요하고, git에 안 들어가고, 엔진 업데이트에 날아간다. **A가 안 될 때만.**

#### 수정 내용 (A·B 공통)

**1. `PythonMLPackages.uplugin` → `PythonRequirements` 의 `"Platform": "Win64"` 블록**

| 항목 | 현재 | 변경 |
|---|---|---|
| `ExtraIndexUrls` | `https://download.pytorch.org/whl/cu124/` | `https://download.pytorch.org/whl/cu128/` |
| torch | `2.5.1+cu124` | **`2.8.0+cu128`** |
| torchaudio | `2.5.1+cu124` | **`2.8.0+cu128`** |
| torchvision | `0.20.1+cu124` | **`0.23.0+cu128`** |

**`--hash=sha256:...` 는 전부 지운다.** Linux/Mac 블록은 건드리지 않는다.

**2. `PythonFoundationPackages.uplugin`** — `sympy==1.13.1` → **`sympy==1.13.3`**
(torch 2.6+ 요구사항). 여기도 해시 제거.

**3. Project Settings → Plugins → Python → (Advanced) `Pip Strict Hash Check` = OFF**
`ConfigRestartRequired` 이므로 에디터 재시작 필요.

**4. `<Project>/Intermediate/PipInstall` 폴더 삭제** → 에디터 재시작 → 재설치 (~3GB, 10~20분)

#### 왜 해시 검사를 꺼야 하는가

`PipInstall.cpp:746` 이 `bPipStrictHashCheck`가 켜져 있으면 pip 명령에 `--require-hashes`를 붙인다.
pip의 이 모드는 **모든 요구사항에 해시가 있어야** 동작한다 — 엔진 주석도 그렇게 적혀 있다:

```cpp
// PipInstall.cpp:481
// NOTE: Hashes are all-or-nothing so if we are ignoring, just remove them all with the parser
```

즉 새 버전의 해시를 직접 채워 넣지 않는 한, 하나만 해시가 없어도 **전체 설치가 실패한다.**

> ⚠️ **이건 보안 저하다.** `bPipStrictHashCheck=False`는 `Config/DefaultEngine.ini`의
> `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`에 기록되고(`UCLASS(config=Engine, defaultconfig)`),
> pip가 받아오는 패키지를 더 이상 검증하지 않는다. 최소한 이 둘은 지킬 것:
> **`OverrideIndexURL`을 절대 설정하지 말 것**(엔진 설정 주석에도 경고가 붙어 있다),
> 인덱스는 공식 `download.pytorch.org`만 쓸 것.
>
> 해시를 유지하고 싶으면 직접 구해 넣으면 된다 —
> `pip download --no-deps --only-binary=:all: torch==2.8.0+cu128 --index-url https://download.pytorch.org/whl/cu128 -d tmp`
> 후 `pip hash tmp/*.whl`. 휠 3개분이라 15분쯤 걸린다. **보고서에 보안 항목을 쓸 거라면 이쪽을 권한다.**

### 1.2 Git + Git LFS — **반드시 Week 1에**

**현재 git 저장소가 아니다.** 4명이 `.uasset`을 편집하는데 VCS가 없으면 작업을 잃는다.
`.gitignore`: `Binaries/ Intermediate/ Saved/ DerivedDataCache/ .vs/ *.sln *.slnx`
(`Intermediate/PipInstall`은 ~3GB — 반드시 제외, 팀원 각자 설치)

### ✅ Gate 0a — GPU 학습 배관 확인 (20분)

```bash
Intermediate\PipInstall\Scripts\python.exe -c "import torch; print(torch.__version__, torch.cuda.is_available())"
Intermediate\PipInstall\Scripts\python.exe -c "import torch; print(torch.zeros(8).cuda().sum())"
Intermediate\PipInstall\Scripts\python.exe -c "import torch; print(torch.cuda.get_device_capability())"
```

| 줄 | 통과 기준 |
|---|---|
| 1 | `2.8.0+cu128 True` — 버전이 `2.5.1+cu124`면 1.1b가 적용되지 않은 것 |
| 2 | `tensor(0., device='cuda:0')` — **이제 성공해야 한다** |
| 3 | `(12, 0)` — RTX 5080은 sm_120 |

2번이 `CUDA error: no kernel image is available for execution on the device`로 실패하면
**cu124 휠이 그대로 남아 있는 것**이다. `Intermediate/PipInstall`을 지우고 다시.

→ 통과하면 `Device = ELearningAgentsTrainingDevice::GPU`.
**실패해도 CPU로 폴백하면 학습 자체는 된다** — 처리량만 손해다. 여기서 막혀 일정을 태우지 말 것.

Tensorboard도 지금 설치 (`-m pip install tensorboard` — PythonMLPackages 요구사항에 없음).

### 1.3 Hello-World RL 루프 — **가장 중요한 250줄**

빈 맵, 더미 에이전트(`float X, Target`), 관측 1개, 이산 행동 3개, 보상 `−|X−Target|`. 전투 코드 0.

**기본값에서 반드시 바꿀 것:**
```cpp
ProcessSettings.TrainerFileName  = TEXT("learning_core.train_ppo");  // 기본값은 behavior_cloning
SharedMemSettings.Timeout        = 60.0f;                            // 기본 10초는 부족
TrainingSettings.Device          = ELearningAgentsTrainingDevice::GPU;  // Gate 0a 통과 시. 실패하면 CPU
TrainingSettings.ActionEntropyWeight = 0.005f;                       // 기본 0.0은 나쁜 기본값
TrainingSettings.bUseTensorboard = true;
TrainerSettings.MaximumRecordedStepsPerIteration = 4000;             // 디버깅 중 빠른 피드백
```

### ✅ Gate 0c — 초록불 되기 전엔 아무것도 시작하지 않음

PIE → 60초 내 `python.exe` 등장 → 로그에 해시 불일치 없음 → 2분 후 `X`가 `Target`으로 수렴 →
Tensorboard mean return 상승 → PIE 중지 시 `EndTraining()` 호출되고 파이썬 종료.

> 이 게이트가 **플러그인 로드 / pip / 파이썬 스폰 / 공유메모리 / 3종 해시 / PPO / 정상 종료**를
> 한 번에 증명한다. 전부 별개 실패 모드이고, 전투 코드와 섞이면 구분이 불가능해진다.
> 신규 팀 기준 **3~5일**.

#### ✅ 실측 결과 (2026-09-17)

구현: `Source/HAI_Project_TEAM5/RL/SH_HelloWorldTrainer.h/.cpp` (약 330줄).
실행: `UnrealEditor.exe <uproject> /Game/RL/Lvl_SHHello -game` — **에디터 없이 헤드리스**.
레벨 `/Game/RL/Lvl_SHHello` 은 `-run=pythonscript` 커맨드릿으로 생성했고 학습기 액터 하나만 들어 있다.

과제: 32 에이전트 × 1차원. 관측 1개(`Target - X`), 행동 3개(왼쪽/정지/오른쪽), 보상 `-|X-Target|`,
에피소드 128스텝(20Hz = 6.4초) Truncation.

| gather (Iter) | Avg Rew | Avg Return |
|---|---|---|
| 0   | **-0.741** | -49.3 |
| 96  | -0.221 | -36.0 |
| 288 | -0.137 | -16.8 |
| 512 | -0.093 | -7.7 |
| 832 | **-0.064** | **-4.2** |

보상이 `-|오차|` 이므로 **에피소드 평균 오차 0.741 → 0.064**. 약 4분, 27 gather.
`Avg Episode Len: 128.00000` — Truncation 경로 정상.
`GPU Usage| Allocated: 0.02 GiB` — cu128 패치가 실제로 쓰이고 있다.
처리량 약 **510 step/s** (32 에이전트 × 20Hz 기준 실시간의 0.8배 — 렌더 오버라이드 미적용 상태).

**아직 검증 안 된 것:** 정상 종료(`EndPlay` → `EndTraining()`) 경로. 이번엔 프로세스를 강제 종료해서
그 경로를 타지 않았고, 트레이너 `python.exe` 2개를 수동으로 정리해야 했다 (위험요소 5번 그대로).
다음 실행에서 `MaxWallClockSeconds` 로 자연 종료시켜 확인할 것.

**계측 지표 함정:** 게임 쪽에서 `|오차|` 를 **순간값**으로 로깅했더니 계속 0.5~0.67 이 나와
"학습이 안 된다"처럼 보였다. 원인은 `bResetAgentsOnUpdate = true` — gather 경계마다 전 에이전트가
리셋되고 리포트가 그 프레임에 겹쳤다. **결정마다 누산해 구간 평균**을 찍도록 고쳤다.

---

## Part 2. 게임 시스템 구축 (Week 2–4)

이전 계획의 Part 2(`Variant_Combat` 수술 — 팀 태그 교체, EQS 크래시, StateTree 컨트롤러)는
**전부 폐기**한다. 대신 없는 것을 새로 만든다.

### 2.1 `AGun::PullTrigger()` — 히트스캔 구현

```cpp
// Gun.h 에 추가
UPROPERTY(EditAnywhere, Category="Gun") float Damage        = 10.0f;
UPROPERTY(EditAnywhere, Category="Gun") float Range         = 6000.0f;   // 60m
UPROPERTY(EditAnywhere, Category="Gun") float FireInterval  = 0.15f;     // 초당 ~6.7발
UPROPERTY(EditAnywhere, Category="Gun") float SpreadDegrees = 1.0f;
float LastFireTime = -1000.0f;

bool  CanFire() const;              // 쿨다운 체크 — 행동 마스킹이 이걸 읽는다
bool  PullTrigger();                // 반환: 적대 대상에 명중했는가 (헛손질 보상용)
```

- **원점:** `Mesh->GetSocketLocation("MuzzleFlashSocket")` (소켓 존재 확인됨)
- **방향:** 총구 전방이 아니라 **소유 캐릭터 컨트롤러의 조준 방향** — 정책이 조준하므로
- 트레이스: `LineTraceSingleByChannel(ECC_Pawn)`, `FCollisionQueryParams`에 소유자 무시
- 데미지 전달: `ICombatDamageable::ApplyDamage` 패턴 재사용 (프로젝트에 이미 두 번 쓰인 패턴)
- **반환값이 중요하다** — 적대 대상 명중 여부가 보상의 hit/miss 항으로 직결

### 2.2 체력 · 팀 · 사망 — `UCombatStatsComponent` (신규)

근접 때는 인터페이스를 권했지만, 지금은 **HP 자체가 없으므로** 컴포넌트가 맞다.
기존 클래스를 리팩터링하는 게 아니라 처음부터 붙이는 것이라 위험이 없다.

```cpp
UCLASS(ClassGroup=Combat, meta=(BlueprintSpawnableComponent))
class UCombatStatsComponent : public UActorComponent
{
    UPROPERTY(EditAnywhere) float MaxHP = 100.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float CurrentHP = 100.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) uint8 TeamId = 0;

    // RL 보상 누산기 — GatherAgentReward 가 읽고 0으로 비운다
    float DamageDealtAccum = 0.f, DamageTakenAccum = 0.f;
    int32 ShotsFired = 0, ShotsHit = 0, KillsAccum = 0;
    bool  bDiedThisInterval = false;

    void  ApplyDamage(float Amount, AActor* Causer);
    bool  IsAlive() const { return CurrentHP > 0.f; }
    void  ResetForNewMatch();
};
```
팀 판정은 `FGenericTeamId` (`AIModule`이 이미 의존). `FCombatTeams::AreHostile(A, B)` 자유 함수 하나.

### 2.3 `ASH_AIController` + `UAimExecutorComponent` — 조준 실행부

**여기가 이 프로젝트의 핵심 신규 설계다.**

폰이 AI에 possess되므로 `DoLook()`(= `AddControllerYawInput`)은 **무동작**이다.
그리고 부모 생성자의 `bOrientRotationToMovement = true`가 컨트롤러 회전과 충돌한다.

**해결 — `ABase_Character` 생성자에서 전환:**
```cpp
GetCharacterMovement()->bOrientRotationToMovement = false;
bUseControllerRotationYaw = true;    // 몸통이 컨트롤 회전을 따라감
```
그러면 `AIController::SetControlRotation()`이 조준이 되고, 이동은 `AddMovementInput`으로 분리된다.

**조준 적용 — 정책은 "원하는 방향", 실행부는 "최대 회전속도로 추종":**
```cpp
// UAimExecutorComponent (폰에 부착, 매 프레임 60Hz)
UPROPERTY(EditAnywhere) float MaxTurnRateDeg = 180.0f;   // 초당. 커리큘럼으로 조절
FVector DesiredAimDir;                                    // 정책이 10~20Hz로 갱신

void TickComponent(float Dt, ...) {
    const FRotator Current = Controller->GetControlRotation();
    const FRotator Target  = DesiredAimDir.Rotation();
    Controller->SetControlRotation(
        FMath::RInterpConstantTo(Current, Target, Dt, MaxTurnRateDeg));
}
```
> **왜 "델타 적분"이 아니라 "목표 방향 + 추종"인가.** Δyaw를 매 결정 누적시키면
> 적분 드리프트가 생기고 정책이 절대 조준을 기억해야 한다. 목표 방향을 직접 내면 상태가 없고,
> 회전속도 상한이 물리적 제약과 학습 난이도를 동시에 잡는다. 그리고 `MaxTurnRateDeg`를
> 커리큘럼 손잡이로 쓸 수 있다 (초반 720 → 후반 180).

**오토 조준 스위치:**
```cpp
UPROPERTY(EditAnywhere) bool bPolicyControlsAim = true;
// false면 DesiredAimDir = (Target->GetActorLocation() - MuzzleLoc).GetSafeNormal()
```
20줄이면 되고, 발표 때 "정책 조준 vs 오토 조준"을 비교한 결과물이 나온다.

### 2.4 `ASH_ScriptBot` — 1단계 상대역 (신규, ~60줄)

슈터용 스크립트 AI가 없다. 근접 StateTree는 전용되지 않는다. 직접 쓴다.

```
매 틱:  가장 가까운 적대 유닛 선택
        MaxTurnRateDeg 로 그쪽으로 조준 회전
        거리 > DesiredRange 면 접근, < 면 후퇴, 그 외 좌/우 선회
        조준 오차 < AccuracyConeDeg 이고 CanFire() 면 발사
```
난이도 손잡이 3개 — `MaxTurnRateDeg`, `ReactionDelay`, `AccuracyConeDeg`.
이걸 단계적으로 조여서 커리큘럼 사다리로 쓴다.

> **고정 스크립트 상대가 왜 중요한가.** 학습하지 않는 상대는 **정상(stationary) MDP**를 만든다.
> PPO가 가장 잘하는 경우다. self-play로 바로 가면 양쪽이 동시에 변해서
> "보상 곡선이 영원히 진동"하는 고전적 실패가 난다.

### ✅ Gate 1
팀 0 유닛이 팀 1 유닛을 쏴서 HP가 깎이고, 0이 되면 죽는다. 스크립트 봇 둘이 서로 싸워서 승부가 난다.

---

## Part 3. 아레나 · 풀링 (Week 4) — **엔진 소스 검증 후 전면 개정**

> 이 파트는 UE 5.8 `LearningAgentsTraining` 소스를 직접 읽고 다시 썼다. 인용된 동작은 전부
> `Engine/Plugins/Experimental/LearningAgents/Source/LearningAgentsTraining/` 에서 확인한 것이다.

### 3.0 ★ 이전 계획의 수작업 풀링을 폐기한다

**5.8은 엔티티 풀링을 엔진이 제공한다.** `FSHSlot` 구조체, "절대 Destroy 안 함" 규칙,
`ResetForNewMatch` 수동 호출 — 전부 필요 없다.

| 엔진이 주는 것 | 파일 |
|---|---|
| `ULearningAgentsEntitiesManagerComponent` — 풀 관리, 에피소드마다 재사용 | `Public/LearningAgentsEntitiesManagerComponent.h` |
| `ILearningAgentsEntityInterface` — `InitializeEntity` / `ResetEntity` / `GetGym` / `EnableEntity` / `DisableEntity` / `IsEntityEnabled` | `Public/LearningAgentsEntityInterface.h` |
| `ALearningAgentsGymBase::Initialize()` / `Reset()` + 델리게이트 3종 | `Public/LearningAgentsGym.h` |

동작 흐름 (`LearningAgentsGym.cpp:18-44`, `LearningAgentsEntitiesManagerComponent.cpp:11-79`):
```
GymsManager->Start()
  └ SpawnGyms()                         모든 Gym 스폰 + 공용 RandomStream 주입
  └ Gym->Initialize()  각 Gym마다
       └ PopulateLearningComponents()   Gym 액터의 컴포넌트 중 LearningComponentInterface 구현체 수집
       └ EntitiesManager->InitializeLearningComponent()
            └ SpawnEntitiesAtRandomLocations()  → 유닛 스폰 → InitializeEntity(Gym) 호출
       └ OnGymInitialized.Broadcast()   ← ★ 우리가 훅을 거는 지점

Gym->Reset()   (에피소드 끝날 때 우리가 호출)
  └ OnBeginGymReset.Broadcast()
  └ EntitiesManager->ResetLearningComponent()
       └ 각 엔티티: SetActorTransform(랜덤) → ResetEntity(Gym) → EnableEntity()
       └ 초과분: DisableEntity()
  └ OnPostGymReset.Broadcast()          ← ★ 여기서 우리 고정 스폰 슬롯으로 덮어쓴다
```

**따라서 `ABase_Character`가 `ILearningAgentsEntityInterface`를 구현하는 것이 아레나의 전제조건이다.**
구현하지 않으면 `CheckEntityClasses()`가 Error를 찍고 **엔티티를 하나도 스폰하지 않은 채 조용히 진행**한다
(`LearningAgentsEntitiesManagerComponent.cpp:266`).

### 3.1 ⚠️ NavMesh 트랩 — 이걸 모르면 전 유닛이 월드 원점에 쌓인다

`ALearningAgentsGymSimple::ProjectPointToGym` (`LearningAgentsGymSimple.cpp:47-68`):

```cpp
if (NavigationSystem) {
    FNavLocation NavLocation;
    NavigationSystem->ProjectPointToNavigation(InLocation, NavLocation, MaxBounds);
    return NavLocation.Location;      // ★ 반환값(bool)을 검사하지 않는다
}
else { return InLocation; }
```

`ProjectPointToNavigation`이 **실패해도 `NavLocation`을 건드리지 않는다.** `FNavLocation`의 기본값은
`FVector::ZeroVector`다. 즉 **NavigationSystem은 있는데 NavMesh가 없으면 모든 스폰 좌표가 `(0,0,0)`이 된다.**
에러도 경고도 없다. 게다가 `GenerateRandomLocationInGym`이 내부에서 이 함수를 부른다.

프로젝트에 NavMesh가 없으므로 **이 경로에 반드시 걸린다.**

→ **`ASH_Arena`에서 두 함수를 오버라이드해 NavMesh 의존을 제거한다.** NavMesh를 까는 것보다 낫다:
결정론적이고, `GymsManager`가 런타임에 타일링하는 가변 영역을 미리 볼륨으로 덮을 필요가 없다.

### 3.2 `ASH_Arena : ALearningAgentsGymSimple` — 오버라이드 4개

```cpp
UCLASS(Blueprintable)
class ASH_Arena : public ALearningAgentsGymSimple
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="SH") float ArenaRadius   = 1750.f;  // 벽 안쪽 반경
    UPROPERTY(EditDefaultsOnly, Category="SH") float SpawnRadius   = 1200.f;  // 팀 스폰 원 반경
    UPROPERTY(EditDefaultsOnly, Category="SH") int32 SlotsPerTeam  = 10;      // ★ 최종 규모로 고정

    // NavMesh 없는 순수 수학 클램프
    virtual FVector ProjectPointToGym(const FVector& P) const override;
    virtual FVector GenerateRandomLocationInGym() const override;
    virtual FRotator GenerateRandomRotationInGym() const override;   // 중심을 바라보게

    // 학습 아레나 ↔ 출고 보드의 유일한 차이
    UFUNCTION(BlueprintCallable, Category="SH")
    virtual void GetSpawnTransforms(int32 Team, int32 LiveCount, TArray<FTransform>& Out) const;
};
```

```cpp
FVector ASH_Arena::ProjectPointToGym(const FVector& P) const
{
    const FVector C = GetActorLocation();
    FVector D = P - C;  D.Z = 0.f;
    const float R = D.Size();
    if (R > ArenaRadius) { D *= (ArenaRadius / R); }
    return FVector(C.X + D.X, C.Y + D.Y, C.Z);
}

FVector ASH_Arena::GenerateRandomLocationInGym() const
{
    check(RandomStream);
    const float A = RandomStream->FRandRange(0.f, 2.f * PI);
    const float R = ArenaRadius * FMath::Sqrt(RandomStream->FRand());   // ★ sqrt = 원판 위 균등
    const FVector C = GetActorLocation();
    return FVector(C.X + R * FMath::Cos(A), C.Y + R * FMath::Sin(A), C.Z);
}
```

> **`sqrt`가 필요한 이유.** 엔진 원본은 `VRand() * FRandRange(0, R)`을 쓴다. 이건 반지름을 균등
> 추출하므로 **점이 중심 쪽에 몰린다** (게다가 `VRand()`는 3D 구면 벡터라 XY 투영 길이가 `sqrt(1-z²)`로
> 또 줄어든다). 면적 균등을 원하면 반지름에 `sqrt`를 씌워야 한다.

**`GetSpawnTransforms` — 고정 슬롯 (이게 실제로 쓰이는 배치)**

```
팀 0: 중심에서 -X 방향 원호,  각도 = 180° ± (k − (N−1)/2) × ArcStep
팀 1: 중심에서 +X 방향 원호,  각도 =   0° ± (동일)
회전: 항상 중심(상대 팀)을 바라봄
ArcStep = 12°,  SpawnRadius = 1200
```
1v1이면 두 유닛이 정확히 2400cm 떨어져 마주본다. 10v10이면 각 팀이 108° 호에 10명씩 늘어선다.

**랜덤 스폰이 아니라 고정 슬롯을 쓰는 이유 세 가지:**
1. 엔진의 `SpawnEntitiesAtRandomLocations`는 **랜덤 트랜스폼을 한 번만 계산해 N개 전부 같은 자리에 스폰**한다
   (`...ManagerComponent.cpp:89-92`). 10명이 한 점에 겹친다.
2. 랜덤이면 시작 거리가 매 에피소드 달라져 초기 학습의 분산이 커진다. 조준 학습 단계에서 치명적이다.
3. 출고 보드는 그리드 셀에 배치한다. 같은 함수를 오버라이드하면 끝난다.

### 3.3 `ABase_Character`가 `ILearningAgentsEntityInterface`를 구현

```cpp
class ABase_Character : public AHAI_Project_TEAM5Character,
                        public ILearningAgentsEntityInterface
{
    // BlueprintNativeEvent이므로 반드시 _Implementation 접미사
    virtual void InitializeEntity_Implementation(ALearningAgentsGymBase* InGym) override;
    virtual void ResetEntity_Implementation(ALearningAgentsGymBase* InGym) override;
    virtual ALearningAgentsGymBase* GetGym_Implementation() const override { return CachedGym; }
    virtual void EnableEntity_Implementation() override;
    virtual void DisableEntity_Implementation() override;
    virtual bool IsEntityEnabled_Implementation() const override { return bEntityEnabled; }
};
```

| 함수 | 하는 일 |
|---|---|
| `InitializeEntity` | `CachedGym = InGym`, `VisibilityBasedAnimTickOption` 설정, 1회성 세팅 |
| `ResetEntity` | 몽타주 정지 → 누산기 클리어 → 래그돌 해제 → 캡슐 콜리전 재활성화 → `StopMovementImmediately` + `MOVE_Walking` → `CurrentHP = MaxHP` → `LastFireTime` 리셋 |
| `EnableEntity` | `SetActorHiddenInGame(false)`, 콜리전 `QueryAndPhysics`, `SetActorTickEnabled(true)`, `bEntityEnabled = true` |
| `DisableEntity` | 위의 반대. **`Destroy()`는 절대 호출하지 않는다** |

**`ResetEntity`에서 하나라도 빠지면 겉보기엔 멀쩡한데 행동만 이상한 유닛이 나온다.**
특히 **캡슐 콜리전 재활성화**를 빠뜨리면 죽은 유닛이 부활해도 총알이 통과한다.

### 3.4 배치 산수 — `GymsSpacing`은 중심간 거리가 아니다

`LearningAgentsGymsManager.cpp:79-82`:
```cpp
SpawnLocation = FVector(Row * (GymsSpacing + (MaxBounds.X - MinBounds.X)) + Template.X,
                        Col * (GymsSpacing + (MaxBounds.Y - MinBounds.Y)) + Template.Y,
                        Template.Z);
```
`MaxBounds - MinBounds`는 **바닥 메시의 전체 폭**이고, `GymsSpacing`은 그 위에 더해지는 **간격**이다.
즉 `피치 = GymsSpacing + 바닥폭`. 기본값 300이면 간격이 3m라 총알이 옆 아레나로 넘어간다.

그리고 `MinBounds/MaxBounds`는 **첫 번째 Gym의 `GetGymExtents()`** 한 번만 읽는다
(`:75`). `GetGymExtents`는 `SimpleGymFloor->GetLocalBounds() × GetComponentScale()`이다
(`LearningAgentsGymSimple.cpp:22-27`).

| 값 | 설정 |
|---|---|
| 바닥 메시 | `/Engine/BasicShapes/Plane` — 로컬 바운드 ±50 |
| `SimpleGymFloor` 스케일 | **35** → 바운드 ±1750, 전체 폭 **3500** |
| `GymsSpacing` | **1500** → 피치 5000cm |
| 16개 배치 | `GridSize = ceil(sqrt(16)) = 4` → 4×4, 전체 약 185m |

> `SimpleGymFloor`는 `EditDefaultsOnly`다. **BP 클래스 디폴트에서 설정해야 하고, 레벨에 놓인
> 인스턴스에서는 바꿀 수 없다.** 그리고 `GymsManager`가 Gym을 스폰할 때 스케일을 주지 않으므로
> (`SpawnActor(Class, Location, Rotation, Params)`) **액터 스케일이 아니라 컴포넌트 스케일**을 써야 한다.

**총알 누출 차단 — 벽이 `Weapon` 채널을 막는다.**
히트스캔 사거리 6000cm > 피치 5000cm이므로, 벽이 트레이스를 막지 않으면 **옆 아레나 유닛을 맞힌다.**
이중으로 막는다:
1. 아레나 벽 StaticMesh가 `Weapon` 채널을 **Block**
2. 트레이스 결과에 `Gym->IsMemberOfGym(HitActor)` 검사 — `ALearningAgentsGymBase`가 이미 제공한다
   (`LearningAgentsGym.cpp:61-68`, `GetGym()` 구현이 있어야 동작)

### 3.5 `BP_SHArena` 구성 — 컴포넌트 목록

```
ASH_Arena (C++)
 └ RootComponent            (부모가 만듦)
 └ SimpleGymFloor           (부모가 만듦) — Plane, Scale 35, Weapon=Block
 └ Wall_00 .. Wall_07       Cube, Scale (15, 0.5, 4) = 1500×50×400
 └ EntitiesManager          ULearningAgentsEntitiesManagerComponent
```

**팔각형 벽 8개** (정사각 벽은 구석 끼기 실패 모드를 부른다). 안쪽 면 반경 1750, 두께 50 →
배치 반경 1775. `Z = 200` (높이 400의 절반).

| # | Location (X, Y, 200) | Rotation Yaw |
|---|---|---|
| 0 | ( 1775.0,     0.0) | 90 |
| 1 | ( 1255.1,  1255.1) | 135 |
| 2 | (     0.0,  1775.0) | 180 |
| 3 | (−1255.1,  1255.1) | 225 |
| 4 | (−1775.0,     0.0) | 270 |
| 5 | (−1255.1, −1255.1) | 315 |
| 6 | (     0.0, −1775.0) | 0 |
| 7 | ( 1255.1, −1255.1) | 45 |

(`1775 × cos45° = 1255.1`. 정팔각형 한 변 = `2 × 1750 × tan22.5° = 1450` 이므로 1500 세그먼트가 약간 겹친다 — 의도한 것이다.)

**`EntitiesManager` 설정 — 여기가 가장 틀리기 쉽다:**

| 필드 | 값 | 이유 |
|---|---|---|
| `Entities[0].EntityClass` | `BP_Unit_Team0` | **팀마다 별도 BP 클래스** |
| `Entities[1].EntityClass` | `BP_Unit_Team1` | 풀이 `EntityClass->GetFName()`로 키잉되므로 같은 클래스면 두 팀이 한 풀을 공유해 구분 불가 |
| `EntitySpawnZOffset` | `100` | 캡슐 half-height 96 + 여유. 없으면 바닥에 박힌 채 스폰 |
| `EpisodeEntitySpawnCountMin` | **`10`** | **기본값 0이면 아무것도 스폰되지 않는다** |
| `EpisodeEntitySpawnCountMax` | **`10`** | **반드시 Min과 같게** ↓ |

> **Min ≠ Max이면 에이전트 집합이 에피소드마다 바뀐다.** `ResetLearningComponent`가
> `RandRange(Min, Max)`로 매번 새로 뽑고, 모자라면 **에피소드 도중에 새 액터를 스폰**한다
> (`...ManagerComponent.cpp:69`). 그렇게 생긴 폰은 `Manager->AddAgent`를 거치지 않았으므로
> **정책이 영원히 무시한다.** 살아있는 적 수를 바꾸고 싶으면 **10개를 스폰해 두고 우리 코드가
> `DisableEntity`로 줄인다.**

> **엔진 버그 주의.** `SpawnEntities()`는 `SpawnedEntities.SetNumUninitialized(SpawnCount)` 후
> `Add()`로 덧붙인다 (`:124`, `:154`). 반환 배열의 **앞쪽 N개는 초기화되지 않은 쓰레기**다.
> **이 함수의 반환값을 순회하지 말 것.** `SpawnEntityAtProjectedLocation`은 `.Top()`만 돌려주므로 안전하다.

### 3.6 학습 맵 `Lvl_SHTrain` — 제작 절차

`Lvl_ThirdPerson`은 World Partition + StaticMeshActor 110개라 부적합하다. 새로 만든다.

1. **File → New Level → `Empty Level`** (Basic 아님 — Basic은 Floor/Cube가 딸려온다)
   World Partition **비활성** 상태로 만들 것. 런타임에 Gym이 스폰되는데 WP는 스트리밍 셀 밖 액터를 언로드한다.
2. `/Game/RL/Lvl_SHTrain` 으로 저장
3. 배치할 액터는 **4개뿐**:

| 액터 | 설정 |
|---|---|
| `DirectionalLight` | 아무 각도. 시각 확인용 |
| `SkyLight` | Intensity 1.0. **없으면 완전 암흑이라 디버깅 불가** |
| `BP_SHRLDirector` | 위치 무관. `BeginPlay`에서 조립 + 20Hz 루프 |
| `BP_SHGymsManager` | **원점 `(0,0,0)`**. `GymTemplates = [{BP_SHArena, 16}]`, `GymsSpacing = 1500`, `RandomSeed` 기록 |

   - **`NavMeshBoundsVolume`을 놓지 않는다.** 3.1의 오버라이드로 nav 의존을 제거했으므로 불필요하고,
     `GymsManager::SpawnGyms()` 말미의 `NavigationSystem->Build()`가 헛돌 뿐이다.
   - **`PlayerStart`를 놓지 않는다.** 놓으면 `BP_BaseCharacter`가 플레이어로 하나 더 스폰되어
     아레나 소속이 아닌 유닛이 섞인다. 관전은 디렉터가 `SetViewTarget`으로 처리한다.

4. **`GymTemplates`는 `private`이다** (`LearningAgentsGymsManager.h:65`). C++에서 채울 수 없고
   **에디터 디테일 패널에서만** 설정 가능하다. `ALearningAgentsGymsManager`는 생성자도 `private`이라
   **C++ 서브클래싱이 불가능**하다 — BP 서브클래스(`BP_SHGymsManager`)로 만들거나 그냥 레벨에 직접 배치한다.

5. `Start()`는 **아무도 대신 불러주지 않는다.** 디렉터 `BeginPlay`에서 직접 호출하고,
   **그 다음에** Manager/Interactor/Policy를 만든다 (Part 7의 순서표 참조).

6. 프로젝트 설정 → Maps & Modes → **Editor Startup Map만** `Lvl_SHTrain`으로.
   `GlobalDefaultGameMode`는 건드리지 않는다 — 출고 빌드와 공유하는 설정이다.
   레벨별 GameMode는 **World Settings → GameMode Override**로 지정한다.

### ✅ Gate 2

16개 아레나에서 **스크립트 봇 vs 스크립트 봇** 30분. RL 코드 없음.

| 확인 | 통과 기준 |
|---|---|
| 배치 | 4×4 격자, 아레나끼리 겹치지 않음, **월드 원점 (0,0,0)에 쌓인 유닛 없음** ← 3.1 트랩 |
| 스폰 | 각 아레나에 팀0 N명 / 팀1 N명, 마주보는 호 배치 |
| 누출 | 한 아레나의 사격이 **옆 아레나 유닛을 맞히지 않음** (`IsMemberOfGym` 로그로 확인) |
| 풀링 | `stat obj` 액터 수가 30분간 **평평** — 증가하면 리셋이 새 액터를 만들고 있다 |
| 리셋 | **죽었다 리셋된 유닛이 다시 죽을 수 있음** (캡슐 콜리전 재활성화 확인) |
| 종료 | 전멸·타임아웃 **양쪽** 경로로 매치 종료 |
| 성능 | fps 기록. `-nullrhi` 대비 배수 측정 |

---

## Part 4. 스키마 설계 및 동결 (Week 5) — ⚠ **폐기 (2026-09-29)**

> **아래 설계는 쓰지 않았다.** 문서 맨 위의 경고 박스와 [TODO.md](TODO.md) 1A.3 을 볼 것.
> 폐기한 이유는 하나다 — 적 좌표를 주면 조준이 닫힌 형태 해가 되어 학습할 것이 남지 않는다.
> 기록으로 남겨 둔다: 아래의 `Fire` 마스킹 원칙("조준 오차로는 마스킹하지 않는다")과
> "인덱스 0은 절대 마스킹하지 않는다" 두 가지는 **새 스키마에도 그대로 적용됐다.**

**근접과 공유하지 않는다.** 슈터 전용으로 최소·정확하게.

### 4.1 행동 공간

```cpp
Action := StructAction {
  "AimDir" : DirectionAction()                              // 로컬 프레임 조준 방향 (3D 단위벡터)
  "MoveDir": DirectionAction()                              // 로컬 프레임 이동 방향
  "Fire"   : BoolAction(PriorProbability = 0.3f)
  "Target" : ExclusiveDiscrete(Size=3, Priors={})           // 1v1에선 1,2 마스킹
}
```

- **`SpecifyDirectionAction`을 쓰는 이유:** `GetDirectionAction(FVector& Out, ..., FTransform RelativeTransform, ...)`이
  **RelativeTransform을 받는다** <!-- 검증됨 -->. 폰 트랜스폼을 넘기면 로컬 → 월드 변환이 공짜다.
  Δyaw/Δpitch 적분 방식보다 상태가 없고 드리프트가 없다.
- **`Target`을 유지하는 이유:** 조준은 자유롭지만, "누구를 노리는가"를 명시적으로 두어야
  ① 조준 정렬 보상의 기준이 생기고 ② 관측의 `IsMyTarget`으로 루프가 닫히고 ③ 3v3 집중사격이 학습 가능해진다.
- **`Fire` 마스킹:** `!Gun->CanFire()`면 `MakeBoolActionModifier(M, false, "Fire")`로 강제.
  물리적으로 불가능한 것만 막는다. **조준 오차로는 마스킹하지 않는다** — 그러면 빗맞히는 게
  나쁘다는 걸 영영 못 배운다. 보상으로 처리한다.
- **`Target` 마스킹:** 죽은 슬롯만. **인덱스 0은 절대 마스킹하지 않는다** — 전부 마스킹된 이산 분포는
  퇴화해서 NaN 로짓을 만든다.

### 4.2 관측 공간 — 조준을 학습 가능하게 만드는 설계

**가장 중요한 피처는 "현재 조준 오차"다.** 이게 없으면 그래디언트가 보이지 않는다.

```
Observation := Struct {
  "Self" : Struct {
      "HPRatio"      : Float(Scale 1.0)
      "CanFire"      : Bool                                   // 쿨다운 준비 여부
      "CooldownLeft" : Float(Scale = FireInterval)
      "Velocity"     : Velocity(rel=SelfXform, Scales=(500,500,500))
      "AimPitch"     : Float(Scale 45.0)                       // 현재 조준 피치 (수평 대비)
  }
  "Enemies" : Array(EnemyObs, MaxNum=3, AttnEnc=32, Heads=4, ValEnc=32)
  "Allies"  : Array(AllyObs,  MaxNum=2, 32, 4, 32)
  "ArenaEdge" : Location(rel=SelfXform, Scales=(1500,1500,1500))   // 가장 가까운 벽
}

EnemyObs := Struct {
  "DirInAimFrame" : Direction(rel = 조준 프레임)   ★ 핵심 — 조준 오차를 직접 노출
  "YawError"      : Float(Scale 45.0)              ★ 부호 있는 수평 오차(도)
  "PitchError"    : Float(Scale 20.0)              ★ 부호 있는 수직 오차(도)
  "Distance"      : Float(Scale 1500.0)
  "RelLocation"   : Location(rel=SelfXform, Scales=(1500,1500,1500))
  "HPRatio"       : Float(Scale 1.0)
  "IsAlive"       : Bool
  "IsMyTarget"    : Bool
  "HasLOS"        : Bool                            // 빈 아레나에선 항상 true. 3v3 대비 지금 넣어둠
  "Velocity"      : Velocity(rel=SelfXform, Scales=(500,500,500))   // 리드 사격용
}
```

**`DirInAimFrame` / `YawError` / `PitchError`가 이 설계의 전부다.**
셋 다 같은 정보를 다른 형태로 준다 — 중복이지만 값싸고, 정책이 어떤 표현을 쓸지 고르게 한다.
`RelativeTransform`에 **조준 회전을 넣은 트랜스폼**을 넘기면 `(1,0,0)`이 "정확히 조준됨"이 된다.

> **`Scales`는 선택이 아니다.** `FLearningAgentsVectorObservationNormalizationSettings`의 기본값은
> `Scales = (1,1,1)`. 언리얼 단위 ±1500cm를 128폭 ELU MLP 첫 레이어에 넣으면 포화한다.
> **아레나 반경으로 설정한다.** UE에서 RL이 조용히 실패하는 1위 원인이다.

**슬롯 순서 규칙:** `Enemies[k]`는 **고정 아레나 슬롯 id**로 인덱싱. **거리순 정렬 금지** —
정렬하면 행동 인덱스 → 실제 액터 매핑이 결정마다 바뀌어 타겟 선택이 원리적으로 학습 불가능해진다.

**1v1 → 3v3:** `MakeArrayObservation`은 `MaxNum` 이하 개수를 허용하고 어텐션 인코더가
고정 크기를 낸다 <!-- 헤더 문서로 검증됨 -->. 벡터 크기와 해시가 동일하므로
`bReinitialize*=false`로 1v1 가중치에서 파인튜닝할 수 있다.

### 4.3 결정 주기 — **20 Hz** (근접 계획의 10Hz에서 변경)

| | 근접 (이전) | 슈터 (지금) |
|---|---|---|
| 보상 지연 | AnimNotify 0.35초 | **0** (히트스캔은 같은 프레임) |
| 제어 대상 | 전술 선택 | **조준 — 연속 제어 루프** |
| 필요 주기 | 10 Hz | **20 Hz** |

히트스캔은 크레딧 지연을 없애므로 그 이유로는 10Hz면 충분하다. 그런데 **조준이 제어 문제로 바뀌었다.**
`MaxTurnRateDeg = 180`/초에서 10Hz면 결정당 18도씩 끊긴다 — 정밀 조준이 불가능하다.
20Hz면 9도, 그리고 GAE 유효 지평 `1/(1−γλ) ≈ 17스텝`이 0.85초로 여전히 교전 한 합을 덮는다.

`MaxEpisodeStepNum = 640` (= 32초), 게임플레이 타임아웃 30초.

### ✅ Gate 3a — **스키마 동결 (되돌릴 수 없음)**

`GetObservationVectorSize()`, `GetObservationEncodedVectorSize()`, `GetActionVectorSize()`,
`GetActionDistributionVectorSize()`, `GetActionModifierVectorSize()` + **3종 호환성 해시**를
`SCHEMA.md`에 날짜와 함께 기록·커밋. 이후 변경 금지.

해시를 바꾸는 것: struct 필드 추가/삭제/**순서변경**, `FName Tag` 변경, `bActive` 변경,
`MaxNum`, `Size`, `AttentionEncodingSize`/`HeadNum`/`ValueEncodingSize`.

> **키 이름은 `namespace SHRL`의 `static const FName` 상수로 한 곳에만 선언하고,
> Specify / Make / Get / Modifier 네 군데가 전부 그 상수를 쓴다.**
> 순서가 어긋나면 **해시는 통과하면서 의미만 뒤바뀌고**, 보상 0으로 학습되며 에러는 없다.

---

## Part 5. 보상 설계

### 5.1 구조 — 게임플레이에서 누적, `GatherAgentReward`에서 배출

히트스캔이라 데미지가 즉시 들어오지만, 누산기 패턴은 그대로 쓴다 —
20Hz 결정 사이에 여러 발이 나갈 수 있기 때문이다. `UCombatStatsComponent`가 누산기를 들고,
`GatherAgentReward`가 읽고 **0으로 비운다.**

### 5.2 항목 (MaxHP 100, 20Hz, ~600스텝 에피소드)

| # | 항목 | 크기 | 비고 |
|---|---|---|---|
| 1 | 가한 데미지 | `+1.0 × (Dealt / EnemyMaxHP)` | |
| 2 | 받은 데미지 | `−0.7 × (Taken / SelfMaxHP)` | 등가 교환이 순이득이어야 함 |
| 3 | **조준 정렬** | `+0.01 × max(0, cos(AimError))^8` | **★ 초기 필수, 반드시 0으로 감쇠** |
| 4 | 명중 | `+0.1 × ShotsHit` | |
| 5 | 빗맞힘 | `−0.02 × (ShotsFired − ShotsHit)` | |
| 6 | 시간 벌점 | `−0.001` | 매 스텝 (30초에 −0.6) |
| 7 | 킬 | `+0.5` | 3v3에선 **팀 전체** |
| 8 | 자신 사망 | `−1.0` | 개별 |
| 9 | 승리 / 패배 | `+2.0 / −1.0` | **팀 전체** |
| 10 | 무승부 | `−0.5` | **양쪽 다** |

**3번이 조준 학습의 전부다.** `cos^8`은 정확히 겨눴을 때만 급격히 커지는 좁은 봉우리라
"대충 그 방향"에 만족하지 않게 한다. **2000 이터레이션에 걸쳐 선형 감쇠**시킨다 —
안 하면 조준만 하고 쏘지 않는 정책이 된다.

**10번은 선택이 아니다.** 무승부 벌점이 없으면 "아무것도 안 하고 30초 버티기"가 승리와 같은 가치가 된다.

### 5.3 ⚠️ 이 PPO는 나쁜 행동을 벌하지 않는다

`ppo.py:130` — `adv = adv.clip(adv_min, adv_max)`, `adv_min` 기본값 **`0.0`**
(`MinimumAdvantage = 0.0`) <!-- 검증됨 -->. **음수 advantage가 전부 0으로 잘려 그래디언트가 사라진다.**

결과: 벌점 항(2, 5, 6, 8, 10)은 **그 행동을 직접 억제하지 않는다.** 해당 상태의 `V(s)`를 낮춰서
*다른* 행동의 advantage를 양수로 만드는 간접 경로로만 작동한다.

→ **벌점을 키우는 것보다 보상 셰이핑을 정교하게 하는 쪽이 훨씬 효과적이다.**
→ **크리틱 정확도가 전부다.** `CriticWarmupIterations = 8`이 기본값인 이유.

> **`CriticWarmupIterations` 의 의미** <!-- 2026-09-17 train_ppo.py 대조 -->
> "처음 8번의 gather 동안 정책이 멈춘다"가 **아니다.** `train_ppo.py:173` 이
> `if ti == 0 and iterations_critic_warmup > 0:` 이므로 **첫 gather 안에서만**
> 크리틱 전용 패스를 8회 돌린다. 두 번째 gather부터는 정책이 곧바로 갱신된다.
>
> 참고로 `IterationsPerGather = 32` 는 "32번 gather마다"가 아니라
> **gather 한 번당 최적화 32회**다. 파이썬 로그의 `Iter:` 는 32씩 증가한다.
→ 파이썬이 `'All advantages were zero — no better than average actions found! Critic may be under-trained.'`를
찍으면 학습 신호가 0인 것이다.

### 5.4 슈터 특유의 보상 해킹

| 실패 모드 | 원인 | 대책 |
|---|---|---|
| **제자리 회전 파밍** | 항 3을 노려 빙빙 돌며 순간 정렬 | `cos^8` + 감쇠 스케줄 + 항 3에 `CanFire` 조건 부여 |
| **발사 거부** | 항 5(빗맞힘)가 무서워 아예 안 쏨 | 항 5 가중치를 항 4보다 작게 유지 (0.02 < 0.1) |
| **무한 카이팅** | 양쪽 같은 속도 → 안정적 무승부 | 항 6·10 + **아레나 반경 1500cm로 제한** |
| **벽 끼기** | 벽에 붙어 피격면 축소 | `ArenaEdge` 관측 + 벽 근접 시 소액 벌점 |
| **막타 스틸 (3v3)** | 킬러별 보너스 → 셋 다 뒤에서 대기 | **항 7·9를 팀 전체로** |

### 5.5 완료 조건

```cpp
if (Arena->IsMatchOver())              Out = ELearningAgentsCompletion::Termination;  // 전멸 — V(s')=0
else if (Time > MaxMatchTime)          Out = ELearningAgentsCompletion::Truncation;   // 크리틱이 꼬리 추정
```
타임아웃에 `Termination`을 쓰면 "시간이 다 되는 건 미래 가치 0인 진짜 끝"이라고 가르쳐 가치 함수가 왜곡된다.

> `RunTraining(..., bResetAgentsOnUpdate=true)`는 **PPO 이터레이션 경계에서도 매치 도중에** 리셋한다.
> `ResetAgentEpisode`는 아무 시점에나 안전해야 하고, 누산기는 반드시 비워져야 한다.

---

## Part 6. 커리큘럼 — 조준부터

**조준이 학습 대상이므로 0단계가 전술이 아니라 조준이다.** 이게 근접 계획과 가장 다른 점이다.

| 단계 | 상대 | 목표 | 성공 기준 | 예상 스텝 / 벽시계 |
|---|---|---|---|---|
| **0** | 고정 `ACombatDummy`, 거리 800cm | **조준만** | 명중률 ≥ 70% | 0.5~1M / **1~2시간** |
| **0.5** | 이동하는 더미 (좌우 왕복) | 추적 조준 | 명중률 ≥ 50% | 1~2M / **3~5시간** |
| **1** | `ASH_ScriptBot` (난이도 하) | 전술 + 조준 | 승률 ≥ 60% | 3~5M / **하룻밤** |
| **1.5** | `ASH_ScriptBot` (난이도 상) | | 승률 ≥ 50% | 2~3M / **반나절** |
| **2** | 동결 스냅샷 self-play | | v1이 v0을 ≥ 70% | 5~10M / **1~2일** |
| **3** | 3v3 파인튜닝 | 집중사격 | 3v3 튜닝본이 1v1 복제본을 ≥ 65% | 3~5M / **하룻밤** |

`ACombatDummy`는 `UCLASS(abstract)`이고 HP가 없다. 0단계에서는 **명중 횟수**로 보상을 준다
(`ShotsHit` 누산기). 팀 id만 부여하면 그대로 쓸 수 있다.

**0단계에서 오토 조준과 정책 조준을 둘 다 돌려 비교한다.** 오토 조준이 30분에 수렴하는데
정책 조준이 4시간째 명중률 10%면, 관측 설계(특히 `DirInAimFrame`의 RelativeTransform)가 틀린 것이다.
**이게 가장 싸고 빠른 진단 신호다.**

### Stage 1에서 warm start

```cpp
Policy = MakePolicy(Manager, Interactor, ..., Enc, Pol, Dec,
    /*bReinitializeEncoder*/ false, /*Policy*/ false, /*Decoder*/ false,   // ← 기존 가중치 로드
    PolicySettings, Seed);
```
같은 플래그가 0→0.5→1→2 전 단계와 1v1→3v3 파인튜닝을 구동한다.

### self-play는 최근 5개 스냅샷에서 균등 랜덤

`bSaveSnapshots = true`, `IterationsPerSnapshot = 200`. 상대 스택 3개를 두고
리셋마다 `LoadNetworkFromSnapshot`으로 **최근 5개 중 랜덤** 선택.
항상 최신만 로드하면 고전적인 self-play 순환/파국적 망각이 난다.

> **상대 스택은 공짜다.** `MakePolicy`는 Manager와 Interactor만 요구한다 —
> Critic, TrainingEnvironment, PPOTrainer, Communicator 없이 돈다. 오브젝트 3개, Python 0개.

### 모방학습 부트스트랩 — 하지 않는다

두 번째 파이프라인 전체(Controller 서브클래스, Recorder, Recording 에셋, ImitationTrainer)를
만들어야 하고 신규 팀엔 2주다. **탐색이 병목일 때만 이득인데, dense 조준 보상이 있으면
랜덤 탐색이 몇 분 안에 "적 쪽으로 겨누고 쏜다"를 찾는다.**
Stage 1이 정직한 튜닝 2회 후에도 승률 40% 아래면 그때 컨틴전시로.

---

## Part 7. 처리량 · 학습 실행

### 설정 순서 — 틀리면 에러 없이 안 돈다

```
1. GymsManager->Start()               ← 아무도 대신 안 불러줌
2. Manager->SetMaxAgentNum(64)        ← 모든 리스너 생성 전에
3. MakeInteractor → 4. MakePolicy → 5. MakeCritic → 6. MakeTrainingEnvironment
7. MakeSharedMemoryTrainingProcess({Timeout=60})
8. MakePPOTrainer
9. 상대 스택 구성
★10. AddAgent(Controller)  ← 맨 마지막. 폰이 아니라 컨트롤러를 등록
```
리스너가 생기기 전에 추가된 에이전트는 그 리스너에게 **영원히 보이지 않고 에러도 없다.**

### 틱 루프 (20Hz)  <!-- 2026-09-17 엔진 소스 대조 후 정정 -->

> **정정.** 이전 판은 `RunTraining` 앞에 `GatherObservations` / `MakeActionModifiers` /
> `RunInference` / `PerformActions` 를 직접 나열했다. **중복이다.**
> `LearningAgentsPPOTrainer.cpp:675-719` 에서 `RunTraining` 이 전부 부른다:
>
> ```
> RunTraining ─ (첫 호출) BeginTraining → Policy->RunInference()
>             └ (이후)   TrainingEnvironment->GatherCompletions()
>                        TrainingEnvironment->GatherRewards()
>                        ProcessExperience(bResetAgentsOnUpdate)
>                        Policy->RunInference()
>                          └ Interactor->GatherObservations()
>                             Interactor->MakeActionModifiers()
>                             EncodeObservations() / EvaluatePolicy()
>                             DecodeAndSampleActions(ActionNoiseScale)
>                             Interactor->PerformActions()
> ```
>
> 밖에서 또 부르면 **같은 스텝을 두 번 수집**한다. 학습 대상 정책은 `RunTraining` 한 줄이면 된다.
> **상대 스택(self-play)은 여전히 직접 돌려야 한다** — `RunTraining` 은 자기 정책만 안다.

```cpp
Accum += Dt;
if (Accum < 0.05f) return;    // DecisionPeriod = 1/20
Accum -= 0.05f;               // 0으로 리셋하지 않음 — 지터에도 주기 유지

// 상대 스택은 학습 대상이 아니므로 직접 추론시킨다 (RunInference 가 Gather/Perform 포함).
for (FOpponentStack& O : Opponents) { O.Policy->RunInference(0.3f); }

// 학습 대상은 이 한 줄이 전부다.
Trainer->RunTraining(TrainingSettings, GameSettings, true, true);

if (Trainer->HasTrainingFailed()) { UE_LOG(..., Error, ...); Trainer->EndTraining(); }
```

> **`bResetAgentsOnUpdate = true` 의 부작용 — 계측 지표 주의.**
> PPO gather 경계마다 **모든 에이전트가 리셋된다** (로그: `LearningAgentsManager: Resetting Agents [...]`).
> 게임 쪽에서 상태를 "순간값"으로 로깅하면 리셋 직후 프레임과 겹쳐 항상 무작위 기준선이 나온다.
> **결정마다 누산해 구간 평균**을 찍을 것. 파이썬 로그의 `Avg Rew` 와 같은 의미가 되어야 한다.

`EndPlay`에서 **반드시** `EndTraining()` 호출 — `BeginTraining`이 전역 엔진 상태
(고정 델타타임, `t.MaxFPS`, vsync)를 바꾸고 이전 값을 트레이너 내부에 보관한다.
빼먹으면 PIE를 멈춰도 에디터가 고정 타임스텝에 남는다.

### 렌더링 — `DefaultEngine.ini`는 건드리지 않는다

출고 빌드와 공유하는 파일이다. 디렉터 `BeginPlay`에서 콘솔 오버라이드:
```
r.Lumen.DiffuseIndirect.Allow 0 / r.Lumen.Reflections.Allow 0 / r.Shadow.Virtual.Enable 0
r.RayTracing.ForceAllRayTracingEffects 0 / r.ScreenPercentage 25 / r.SkeletalMeshLODBias 3
sg.ShadowQuality 0 / sg.EffectsQuality 0 / sg.PostProcessQuality 0 / r.VolumetricFog 0
```

`FLearningAgentsTrainingGameSettings`: `bUseFixedTimeStep=true`, `FixedTimeStepFrequency=60`,
`bDisableMaxFPS=true`, `bDisableVSync=true`, `bUseUnlitViewportRendering=true`,
`bDisableUseLessCPUInTheBackground=true`.

> **고정 타임스텝은 렌더 프레임당 1/60초씩 시뮬이 전진한다는 뜻이다.** 300fps로 렌더하면 실시간 5배.
> 이게 처리량 레버의 전부고, 그래서 렌더링 비용이 중요하다.

### 처리량 추정

16 아레나 × 2유닛 @ 실시간 3배, 20Hz → `20 × 3 × 32` ≈ **1,920 steps/s**.
`-nullrhi`면 약 2배. 20Hz라 10Hz 대비 스텝 수가 2배지만 수집 속도도 2배라 벽시계는 비슷하다.

#### GPU 학습이 처리량에 미치는 영향 — 정직하게

**정책망 자체는 작다.** 128폭 MLP + 64 GRU라, 순전파·역전파만 보면 GPU에서 커널 실행 오버헤드가
연산량을 압도한다. **"GPU로 바꾸면 학습이 몇 배 빨라진다"는 기대는 하지 말 것.**

실제 이득은 다른 데 있다.

| 이득 | 설명 |
|---|---|
| **★ CPU 코어를 시뮬레이션에 돌려준다** | 가장 큰 효과다. PPO 업데이트가 CPU에서 돌면 언리얼의 아레나 틱과 **같은 코어를 놓고 경쟁**한다. 학습을 GPU로 옮기면 그 코어가 비고, **아레나 개수를 올릴 여유가 생긴다** |
| 10v10 어텐션 | 적 10 + 아군 9에 대한 어텐션은 1v1보다 연산량이 훨씬 크다. 그때 GPU가 실제로 일한다 |
| 배치 확대 | `MaximumRecordedStepsPerIteration`을 32,768 이상으로 올릴 때 CPU는 버겁다 |

**측정 순서:** Gate 0a 통과 후 CPU/GPU 양쪽으로 Hello-World를 30분씩 돌려 이터레이션 벽시계를 비교한다.
GPU가 빠르면 `-GymsCountOverride=24` 또는 `32`로 올려 다시 잰다. **코어가 빈 만큼 아레나로 바꾸는 것이 요점이다.**

> **재현성 주의.** 같은 시드라도 **CPU와 GPU는 같은 궤적을 내지 않는다** (부동소수점 축약 순서, cuDNN 비결정성).
> 보고서에 쓸 실험은 `Device`를 결과와 함께 기록하고, 비교 실험은 **같은 디바이스에서만** 한다.

### Tensorboard에서 볼 것

| 곡선 | 이상 신호 |
|---|---|
| **entropy** | 초반 수백 이터레이션에 0으로 붕괴 → `ActionEntropyWeight` 0.005 → 0.01 |
| mean return | **처음 50 이터레이션은 의미 없음.** 30분 이상 관찰 후 판단 |
| critic loss | 안 줄면 보상 스케일 과대 — 5.3절 참조 |
| **명중률 (직접 로깅)** | 조준 학습의 가장 직접적인 지표. 커스텀 스칼라로 찍을 것 |

---

## Part 8. 출고 · 오토배틀러 셸 (Week 12–13)

### 추론 경로 — Python 없음

아키타입당 NN 에셋 3개(`_Encoder`, `_Policy`, `_Decoder`)만 출고. 크리틱은 학습 전용.
`NNERuntimeBasicCpu`가 백엔드이고 자동 쿠킹된다.

```cpp
Interactor = MakeInteractor(Manager, USHInteractor::StaticClass(), "InfInteractor");
Policy     = MakePolicy(Manager, Interactor, ..., Cfg->Encoder, Cfg->Policy, Cfg->Decoder,
                        false, false, false, Cfg->PolicySettings, Cfg->Seed);
// 결정당: GatherObservations → MakeActionModifiers → RunInference(0.25f) → PerformActions
```
`ActionNoiseScale = 0.0`은 완전 결정론이라 같은 보드가 매번 같은 전투를 만든다.
**0.2~0.3**이면 무능해 보이지 않으면서 다양성이 산다.

> **설정 표류 방지:** `FLearningAgentsPolicySettings`가 학습·런타임에서 바이트 단위로 같아야 한다.
> 아키타입당 `USHPolicyConfig : UPrimaryDataAsset` 하나가 설정 + NN 에셋 3개 + 시드를 들고
> 양쪽 디렉터가 그것만 읽게 한다.

### 셸 — 5가지만

1. `USHUnitDefinition : UPrimaryDataAsset` — `UnitId`, `PawnClass`, `PolicyConfig`, `Cost`, 스탯, 아이콘.
   **로스터 엔트리 + 아키타입↔신경망 매핑 + 스탯 테이블을 한 에셋이 겸한다.**
2. `ASH_Board : ASH_Arena` — `GetSpawnTransforms`만 오버라이드해 그리드 셀 반환
3. `ASH_MatchGameMode` — `Placement` → `StartCombat()` → `Combat` → `Result` 3상태
4. UMG 위젯 3개 — 로스터, 보드 슬롯(드롭 타겟), 매치 HUD
5. 고정 카메라 하나, 플레이어 컨트롤러는 possess 없이 `SetViewTarget`

**범위 외** (README에 명시): 골드·경제, 상점 리롤, 아이템, 시너지, 다중 라운드, PvP, 영속성.

### 애니메이션 — RL에 필수인가?

**아니다.** 히트스캔은 코드에서 트레이스하므로 `ABP_Wraith`에 Fire/HitReact/Death 상태가 없어도
학습은 정상 동작한다. 다만 **AimOffset은 넣는 게 좋다** — ParagonWraith에 9방향
`Idle_AO_*` 세트가 이미 있고, 없으면 정책이 어디를 겨누는지 화면에서 볼 수 없어 **디버깅이 불가능**하다.
Fire/HitReact/Death는 출고 단계(Week 12) 폴리시 작업으로 미룬다.

---

## Part 9. 신규/수정 파일 매니페스트

**신규:**
```
Source/HAI_Project_TEAM5/RL/SHRLTypes.h                        FName 상수, enum
Source/HAI_Project_TEAM5/Combat/CombatStatsComponent.h/.cpp     HP·팀·보상 누산기
Source/HAI_Project_TEAM5/Combat/CombatTeams.h/.cpp              FCombatTeams::AreHostile
Source/HAI_Project_TEAM5/RL/AimExecutorComponent.h/.cpp         조준 추종 + 오토조준 스위치
Source/HAI_Project_TEAM5/RL/SH_AIController.h/.cpp
Source/HAI_Project_TEAM5/RL/SH_ScriptBot.h/.cpp                 1단계 상대역 (~60줄)
Source/HAI_Project_TEAM5/RL/SH_Arena.h/.cpp                     : ALearningAgentsGymSimple  [학습+출고]
                                                               ★ ProjectPointToGym / GenerateRandomLocationInGym /
                                                                 GenerateRandomRotationInGym 오버라이드 (NavMesh 제거)
                                                               ★ GetSpawnTransforms — 보드가 오버라이드하는 유일한 지점
Source/HAI_Project_TEAM5/RL/SHInteractor.h/.cpp                 : ULearningAgentsInteractor [학습+출고]
Source/HAI_Project_TEAM5/RL/SHTrainingEnvironment.h/.cpp        : ...TrainingEnvironment    [학습만]
Source/HAI_Project_TEAM5/RL/SHRLDirector.h/.cpp                 조립 + 20Hz 루프
Source/HAI_Project_TEAM5/RL/SHPolicyConfig.h/.cpp               UPrimaryDataAsset
Content/RL/Lvl_SHTrain.umap                                     비 WP 빈 원형 아레나
SCHEMA.md                                                       동결된 크기 + 3종 해시
Plugins/PythonMLPackages/          ★ 엔진에서 복사 + cu128 패치 (1.1b) — git에 커밋
Plugins/PythonFoundationPackages/  ★ 엔진에서 복사 + sympy 1.13.3 (1.1b) — git에 커밋
```

`ASH_Arena`에 `ULearningAgentsEntitiesManagerComponent`를 붙이므로 **풀링 코드는 작성하지 않는다** (Part 3.0).

**수정:** `HAI_Project_TEAM5.Build.cs` · `HAI_Project_TEAM5.uproject` ·
`Base_Character` — **`ILearningAgentsEntityInterface` 구현 (B1)** ·
`BP_BaseCharacter` — **`AutoPossessAI = Placed in World or Spawned` (B2)** ·
[Base_Character.h/.cpp](Source/HAI_Project_TEAM5/Base_Character.cpp) (UPROPERTY, include, 회전 설정) ·
[Gun.h/.cpp](Source/HAI_Project_TEAM5/Gun.cpp) (히트스캔) · `BP_BaseCharacter` (ShootAction) · `IMC_Character`

---

## Part 10. 일정

| 주차 | 내용 | 게이트 |
|---|---|---|
| 1 | 플러그인·pip·Build.cs·**Git+LFS**·Hello-World RL 루프 | 0a / **0c** ★ |
| 2–3 | 히트스캔, HP·팀·사망, 조준 실행부, 스크립트 봇 — **RL 없음** | 1 |
| 4 | 아레나·풀링·학습 맵, 봇 vs 봇 16아레나 — **RL 없음** | 2 |
| 5 | 스키마 작성 → **동결**, 0단계 조준 학습 (오토 vs 정책 비교) | 3a ★ |
| 6–8 | 0.5단계 이동 표적 → 1단계 스크립트 봇 | 4 |
| 9–10 | 1.5단계 + self-play | 5 |
| 11 | 3v3 파인튜닝 | 6 |
| 12–13 | 오토배틀러 셸 + 추론 경로 + 애니메이션 폴리시 | 7 |
| 14 | 데모 영상, 보고서, **버퍼** | |

**2~4주차에 RL 코드가 한 줄도 없다.** 의도된 것이다. HP·팀·리셋이 깨진 상태에서 RL을 붙이면
모든 버그가 "학습이 안 됨"이라는 하나의 증상으로 위장하고, 분리하는 데 몇 주가 간다.

---

## Part 11. 위험 요소

| # | 위험 | 대책 |
|---|---|---|
| 1 | **조준 학습이 수렴하지 않음** | 5주차에 오토 조준과 병렬 비교. 4시간 후 명중률 10% 미만이면 `DirInAimFrame`의 RelativeTransform이 틀린 것 |
| 2 | **인터넷 자료가 전부 5.3~5.5 API** | experimental v0.2, 엔진에 문서·샘플 0. 헤더와 `ppo.py`가 유일한 문서 |
| 3 | 엔진 업그레이드 | 프로젝트 중 금지. README에 5.8 고정. **1.1b의 GPU 패치가 날아간다** |
| 3b | **GPU 패치가 팀원마다 다름** | 방법 A(프로젝트 로컬 복사)로 git에 넣을 것. 방법 B를 쓴다면 Gate 0a를 **4명 전원이** 통과해야 |
| 3c | **해시 검사 OFF = 공급망 위험** | `OverrideIndexURL` 절대 설정 금지. 공식 `download.pytorch.org`만. 여유되면 해시 직접 채우기 |
| 3d | GPU/CPU 결과 불일치 | 같은 시드도 디바이스가 다르면 궤적이 다르다. 비교 실험은 동일 디바이스에서만 |
| 4 | `EndTraining()` 누락 | 에디터가 고정 타임스텝에 남음. `EndPlay`에서 호출 |
| 5 | 공유메모리 누수 | 하드 크래시 후 타임아웃. 떠도는 `python.exe` 종료 |
| 6 | `HasTrainingFailed()`가 유일한 에러 신호 | 매 틱 폴링 + 크게 로깅 |
| 7 | `ActionEntropyWeight = 0.0` 기본값 | `0.005`에서 시작, entropy 곡선 감시 |
| 8 | 넉백/이동이 아레나를 넘음 | `ProjectPointToGym` 매 틱 클램프 + `GymsSpacing ≥ 2500` |
| 9 | 시드 5개 (PPO, MakePolicy, MakeCritic, Gym, GymsManager) | 전부 설정하고 **로깅**. 없으면 보고서를 못 씀 |
| 10 | **경로에 한글·공백 금지** | 파이썬 서브프로세스 argv 문제. 프로젝트를 한글 경로로 옮기지 말 것 |
| 11 | `GymsManager::Start()` | 아무도 대신 안 부름 |
| 12 | 근접 아키타입 추가 시 | **별도 스키마·별도 정책.** 슈터 스키마를 건드리면 해시가 깨짐 |

---

## 검증 방법

| 무엇 | 어떻게 |
|---|---|
| 파이썬/토치 배관 | `PipInstall\Scripts\python.exe -c "import torch; ..."` (Gate 0a) |
| RL 루프 전체 | Hello-World PIE + 작업관리자 `python.exe` + Tensorboard 곡선 (Gate 0c) |
| 히트스캔 | 팀 0이 팀 1을 쏴서 HP 감소, 0이면 사망 (Gate 1) |
| 풀링/리셋 | 봇 vs 봇 16아레나 30분, **죽었다 리셋된 유닛이 다시 죽는가** (Gate 2) |
| **관측 정확성** | **Visual Logger** (`VisLog` 콘솔) — `DirInAimFrame` 디버그 드로우가 적을 향하는지 |
| **조준 학습** | 0단계 명중률 커스텀 스칼라. 오토 조준 대비 곡선 |
| 정책 품질 | 별도 평가 구성(노이즈 0, 학습 off) 200매치 승률 |
| 출고 | 패키징 Development 빌드 전체 루프 + `python.exe` 부재 확인 |
