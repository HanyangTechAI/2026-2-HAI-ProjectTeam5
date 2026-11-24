# 행동 트리(BT_SHCombat) 조립 가이드

RL 모델과 비교할 상대인 **행동 트리**를 에디터에서 조립한다. 노드(태스크 · 서비스)는 C++ 로 이미 있다(`Source/.../Battle/AI/`).
에셋 두 개(블랙보드 · 행동 트리)를 만들고, 게임모드에 지정하면 HUD 드롭다운에서 고를 수 있다. 약 10분.

> 먼저 C++ 이 빌드돼 있어야 노드가 보인다(`SH Engage` 등).

## 공정성 — 무엇이 RL 과 같은가

| | RL 모델 | 행동 트리 |
|---|---|---|
| 보는 것 | `USH_VisionComponent` (시야각 90°, 사거리, 벽 · 엄폐물 · 아군 가림) + 맞은 방향 + 구역 반경 | **같은 것만** (`SH Perceive`) |
| 판단 주기 | 20 Hz | 20 Hz (서비스 · 태스크 모두) |
| 이동 | `PendingMoveInput` (단위벡터) | 같은 변수 |
| 조준 | `PendingTurnRateDeg`, 최대 `MaxTurnRateDeg`(180°/s) | 같은 변수 · 같은 상한 |
| 사격 | `AGun` 쿨다운 0.15초 | 같은 총 |

다른 점:
- RL 은 한 결정 주기의 사격을 모아 한꺼번에 처리하고(PR #3), 행동 트리는 쏘는 즉시 처리한다(스크립트 봇과 같음).
  같은 순간 서로 쏘면 먼저 처리되는 쪽이 이길 수 있다.
- 행동 트리는 **엄폐물 위치(지형)를 안다**(`SH Find Cover` 가 보드의 엄폐물 컴포넌트를 읽는다). 정책은 시야 레이로만 지형을 본다.
  적 위치는 여전히 시야로만 안다.
결과를 해석할 때 참고.

## 1. 블랙보드 `BB_SHCombat`

콘텐츠 브라우저 `Content/Battle/AI/` 폴더 → 우클릭 → **Artificial Intelligence → Blackboard** → 이름 `BB_SHCombat`.
열어서 **New Key** 로 아래 11개를 만든다. **이름 · 타입이 정확히 같아야** 노드가 키를 자동으로 찾는다(`SH_BotSteering.h` 의 `SHBot::Keys`).

| 키 이름 | 타입 | 채우는 곳 / 뜻 |
|---|---|---|
| `TargetActor` | Object (Base Class: **Actor**) | 지금 보이는 가장 가까운 적 |
| `HasTarget` | Bool | 보이는 적이 있다 |
| `RecentlySeen` | Bool | 방금(4초 안) 놓친 적이 있고 아직 그 자리를 확인하지 않았다 |
| `LastSeenLocation` | Vector | 적을 마지막으로 본 곳 |
| `UnderFire` | Bool | 1.5초 안에 맞았는데 쏜 적이 안 보인다 |
| `ThreatLocation` | Vector | 마지막으로 나를 쏜 곳 |
| `OutsideZone` | Bool | 축소 구역 밖(경계 150 cm 안쪽부터) |
| `ZoneCenter` | Vector | 아레나 중심 |
| `CoverLocation` | Vector | `SH Find Cover` 가 고른 엄폐물 끝(적 쪽에서 가려지는 모서리) |
| `PeekLocation` | Vector | 그 끝에서 옆으로 한 걸음 나간 자리(앞을 확인하는 곳) |
| `FrontLocation` | Vector | 적이 있다고 보는 곳(방금 본 곳 > 나를 쏜 곳 > 상대 진영 쪽) |

저장.

## 2. 행동 트리 `BT_SHCombat`

같은 폴더 → 우클릭 → **Artificial Intelligence → Behavior Tree** → 이름 `BT_SHCombat`. 열고 오른쪽 Details 의 **Blackboard Asset = BB_SHCombat**.

### 구조

```
ROOT
 └─ Selector "Combat"                      ← 서비스: SH Perceive
     ├─ SH Engage                          ← 데코레이터: Blackboard  HasTarget  Is Set     (Observer aborts: Both)
     ├─ SH Move Toward  [ZoneCenter]       ← 데코레이터: Blackboard  OutsideZone Is Set    (Observer aborts: Lower Priority)
     ├─ SH Turn Toward  [ThreatLocation]   ← 데코레이터: Blackboard  UnderFire  Is Set     (Observer aborts: Lower Priority)
     ├─ SH Move Toward  [LastSeenLocation] ← 데코레이터: Blackboard  RecentlySeen Is Set   (Observer aborts: Lower Priority)
     ├─ Sequence "Advance"                  ← 엄폐물 사이로 전진
     │   ├─ SH Find Cover
     │   ├─ SH Move Toward  [CoverLocation]
     │   └─ SH Peek
     └─ SH Search                           ← 갈 엄폐물이 없을 때만
```

왼쪽부터 우선순위가 높다. Selector 는 왼쪽부터 시도해 처음 되는 것을 한다.
"보이면 싸운다 > 구역 밖이면 들어간다 > 맞으면 돌아본다 > 놓쳤으면 쫓는다 > **엄폐물 끝으로 가서 앞을 확인하고, 안전하면 다음 엄폐물로** > (갈 곳이 없으면) 찾는다".

Sequence 는 자식을 왼쪽부터 차례로 하고, 하나라도 실패하면 거기서 멈춘다.
Find Cover 가 갈 곳을 못 찾거나 Move Toward 가 막히면 Sequence 가 실패 → Selector 가 Search 로 넘어간다.
Peek 중에 적이 보이면 맨 왼쪽 Engage 의 데코레이터(Observer aborts: Both)가 Sequence 를 끊고 교전한다.

### 순서대로

1. ROOT 아래 빈 곳 우클릭 → **Composites → Selector**. ROOT 와 연결. 이름(Node Name) `Combat`.
2. Selector 우클릭 → **Add Service → SH Perceive**. (Interval 0.05 가 기본값이다 — 그대로 둔다.)
3. Selector 아래에 노드 6개를 **왼쪽에서 오른쪽 순서로** 놓고 Selector 에 연결한다(빈 곳 우클릭 → Tasks / Composites → …).
   1. **SH Engage** — 기본값 그대로(사격 콘 3°, 거리 900±200 cm).
   2. **SH Move Toward** — Details → Blackboard → **Location Key = ZoneCenter**, Accept Radius `300`, Timeout `6`.
   3. **SH Turn Toward** — 기본값(Location Key = ThreatLocation).
   4. **SH Move Toward** — 기본값(Location Key = LastSeenLocation, Accept 150, Timeout 4).
   5. **Composites → Sequence** (이름 `Advance`). 그 아래에 왼쪽부터:
      1. **SH Find Cover** — 기본값(한 번에 250~1300 cm, 모서리에서 90 cm, 옆걸음 70 cm).
      2. **SH Move Toward** — **Location Key = CoverLocation**, Accept Radius `80`, Timeout `5`.
      3. **SH Peek** — 기본값(옆으로 나가 1초 동안 ±30° 확인).
   6. **SH Search** — 기본값(2초, 120°/s, 막히면 옆으로 우회).
4. 1~4번 태스크에 데코레이터를 붙인다: 태스크 우클릭 → **Add Decorator → Blackboard**. (5번 Sequence 와 6번 Search 에는 붙이지 않는다.)
   | 태스크 | Blackboard Key | Key Query | Observer aborts |
   |---|---|---|---|
   | SH Engage | HasTarget | Is Set | **Both** |
   | SH Move Toward (ZoneCenter) | OutsideZone | Is Set | **Lower Priority** |
   | SH Turn Toward | UnderFire | Is Set | **Lower Priority** |
   | SH Move Toward (LastSeen) | RecentlySeen | Is Set | **Lower Priority** |
   - Bool 키의 Is Set = 값이 true.
   - **Lower Priority**: 조건이 참이 되면 오른쪽(우선순위가 낮은) 행동을 끊고 이 행동을 한다(예: 찾다가 적을 보면 곧바로 교전).
   - **Both**: 위 + 조건이 거짓이 되면(적이 사라지면) 자기 자신도 끊는다.
5. 저장. 노드에 빨간 경고가 없어야 한다(키 이름이 틀리면 노드 설명에 `None` 이 보인다).

## 3. 게임모드에 지정

`Content/Battle/BP_BattleGameMode` 열기 → Class Defaults → **SH | Battle | AI → Combat Behavior Tree = BT_SHCombat** → 컴파일 · 저장.
(`BT Controller Class` 는 기본값 `SH_BTBotController` 그대로.)

지정하지 않으면 HUD 에서 "행동 트리" 를 골라도 RL 모델로 되돌아가고, 출력 로그에 이유가 찍힌다.

## 4. 써 보기

1. `Lvl_Battle` PIE. 좌측 상단 **아군 / 적군** 드롭다운에서 각각 `RL 모델` 또는 `행동 트리`.
2. 10명 배치 → 시작. 결과 화면에 `아군 RL 모델 vs 적군 행동 트리` 처럼 조합이 같이 나온다.
3. 드롭다운은 배치 단계에서만 바뀐다. "다시 배치" 후 바꿔서 다시 시작하면 된다.

### 디버깅

- PIE 중 `BT_SHCombat` 을 열고 상단 디버그 드롭다운에서 유닛(`SH_BTBotController_N`)을 고르면 지금 실행 중인 노드 · 블랙보드 값이 보인다.
- 게임 화면에서 `'`(작은따옴표) → 게임플레이 디버거 → 넘패드 2 = Behavior Tree.
- 시야 레이 체크박스를 켜면 행동 트리 유닛의 시야(부채꼴)도 보인다 — 같은 시야 컴포넌트를 쓰기 때문이다.

## 5. 평가 모드에서 상대로 쓰기

학습된 모델(팀 0)이 행동 트리(팀 1)를 상대로 얼마나 이기는지 잰다. 학습은 하지 않는다.

**에디터에서 보기** — `Lvl_SHView` 를 열고 트레이너 액터(`SH_CombatTrainer`)의 `SH|RL|Eval` 에서
1. `bEvalOnPlay` 켜기, `EvalSnapshotDirectory` = 볼 모델 폴더(예: `Training/START_11_13/cum017h06m`)
2. `bEvalOpponentBehaviorTree` 켜기 → Play. 스냅샷 상대 · self-play 설정보다 우선한다.

**헤드리스로 승률 재기** — `-SHEval=<모델 폴더> -SHEvalOpponent=bt -SHEvalRounds=<라운드 수>`.
끝나면 로그에 팀 0 승률과 95% 구간이 `상대 행동 트리 'BT_SHCombat'` 와 함께 나온다.
- `-SHAlternateSides=1` 을 같이 주면 라운드마다 진영을 바꾼다(평가 기본은 고정 진영이라 지형 유불리가 섞인다).
- `-SHArena=BP_SHArena_Corridor` 처럼 맵을, `-SHEvalNoise=0` 으로 행동 잡음을 정할 수 있다.
- 다른 트리를 쓰려면 `-SHBotTree=<에셋 경로>`.

## 노드 요약 (C++)

| 노드 | 파일 | 하는 일 |
|---|---|---|
| SH Perceive (서비스) | `SH_BTService_Perceive` | 20 Hz 로 시야 스캔 → 블랙보드 8개 키 |
| SH Engage | `SH_BTTask_Engage` | 조준(회전 상한 안에서) · 오차 3° 이내 사격 · 900±200 cm 유지 · 좌우 이동, 구역 밖이면 쏘면서 중심으로 |
| SH Move Toward | `SH_BTTask_MoveToward` | 블랙보드 위치로 직진(가는 쪽을 봄). 도착 성공, 시간 초과 · 막힘(1초에 40 cm 미만) 실패 |
| SH Turn Toward | `SH_BTTask_TurnToward` | 블랙보드 위치 쪽으로 회전만 |
| SH Find Cover | `SH_BTTask_FindCover` | 보드 엄폐물의 모서리 중 적 쪽(FrontLocation)에서 **가려지고**, 직선으로 갈 수 있고, 비어 있고, 최근에 안 간 곳을 고른다. 점수 = 적 쪽으로 다가가는 정도 − 거리 × 0.3 |
| SH Peek | `SH_BTTask_Peek` | 앞을 보며 엄폐물 끝에서 옆으로 한 걸음 나가, 1초 동안 앞쪽 ±30° 를 훑는다. 아무도 없으면 성공(다음 엄폐물로) |
| SH Search | `SH_BTTask_Search` | 유닛마다 다른 방향 · 속도로 훑으며 아레나 반대편으로 전진. 1초에 40 cm 도 못 가면 1.5초 동안 옆으로 우회 |
| (공통 뼈대) | `SH_BTTask_UnitBase` | 20 Hz 결정 · 생존 확인 · 끝나거나 끊기면 이동/조준 의도 지우기 |
| 컨트롤러 | `SH_BTBotController` | `SH_RLBotController` 상속(조준 · 몸통 회전 설정 공유). 전투 때만 트리 실행, 죽으면 멈춤 |

값(사격 콘, 교전 거리 등)은 트리의 각 노드 Details 에서 바꾼다. 코드를 다시 빌드할 필요가 없다.
