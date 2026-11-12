# 인게임 프론트엔드 구현 가이드 — Modern C++ 공부를 겸해서

> 대상: 이 프로젝트의 C++ 코드를 직접 짜면서 Effective C++(3판) · Effective Modern C++ 을 공부하려는 사람.
> 이 문서는 **설계 · 선언 · 어려운 부분의 코드 조각 · 함정 · 확인 방법**까지 준다. 함수 본문 대부분은 직접 쓴다.
> 항목 표기: **EC n** = Effective C++ 3판 항목 n, **EMC n** = Effective Modern C++ 항목 n.
> 작성 기준 코드: 2026-11-03 의 `Source/HAI_Project_TEAM5` (누적 16시간 정책, `Snapshots/2026-10-25/cum016h00m`).

---

## 목차

0. [완성 모습과 범위](#0-완성-모습과-범위)
1. [구조 한눈에](#1-구조-한눈에)
2. [0단계 — 에셋 준비](#2-0단계--에셋-준비)
3. [1단계 — 격자 수학 `FSHBoardGrid`](#3-1단계--격자-수학-fshboardgrid)
4. [2단계 — 배치 상태 `FSHPlacement`](#4-2단계--배치-상태-fshplacement)
5. [3단계 — 데이터 에셋](#5-3단계--데이터-에셋)
6. [4단계 — 보드 `ASH_BattleBoard`](#6-4단계--보드-ash_battleboard)
7. [5단계 — 정책 실행 `ASH_BattleBrain`](#7-5단계--정책-실행-ash_battlebrain)
8. [6단계 — 입력과 카메라 `ASH_BattlePlayerController`](#8-6단계--입력과-카메라-ash_battleplayercontroller)
9. [7단계 — 게임 흐름 `ASH_BattleGameMode`](#9-7단계--게임-흐름-ash_battlegamemode)
10. [8단계 — UI `UBattleHUDWidget`](#10-8단계--ui-ubattlehudwidget)
11. [9단계 — 마무리와 확장](#11-9단계--마무리와-확장)
12. [부록](#12-부록)

각 단계는 같은 순서로 적었다: **목표 → 만들 것 → C++ 개념 → 코드 조각 → 언리얼 함정 → 완료 확인 → 연습 문제**.

---

## 0. 완성 모습과 범위

### 화면 흐름

1. 레벨을 열면 아레나(학습 때와 같은 팔각형 · 엄폐물)가 보이고, **적 10명이 동쪽에 이미 서 있다.**
2. 서쪽 절반에 격자 칸이 보인다. 빈 칸을 클릭하면 아군이 하나 생기고(최대 10명), 아군이 있는 칸을 클릭해 고른 뒤 다른 칸을 클릭하면 옮겨진다. 우클릭하면 뺀다.
3. 화면의 `배치 7 / 10` 표시가 10 이 되면 **시작** 버튼이 켜진다.
4. 시작을 누르면 양 팀 모두 학습된 정책으로 싸운다(라운드 규칙은 학습과 같다: 전멸 또는 45초, 20초부터 구역 축소).
5. 끝나면 승리/패배/시간 초과를 표시하고, **다시 배치** 버튼으로 2번으로 돌아간다.

### 이번에 하지 않는 것

상점 · 골드 · 레벨업 · 여러 라운드 연속 · 유닛 합성 · 네트워크 멀티플레이 · 패키징 빌드(9단계에 주의점만 적음).

---

## 1. 구조 한눈에

### 클래스

```
ASH_BattleGameMode              단계 상태 기계 (Placement → Battle → Result → Placement)
 │                              시작 요청 처리, 델리게이트로 UI 에 알림
 ├─ ASH_BattleBoard : ASH_Arena 레벨에 하나 배치 (BP_BattleBoard)
 │   ├─ FSHBoardGrid            [UObject 아님] 칸 ↔ 좌표 변환, 격자 크기
 │   ├─ FSHPlacement            [UObject 아님] 칸 → 유닛 점유 관리
 │   └─ 유닛 스폰 · 전투 시작(ResetRound) · 전투 종료 감지
 ├─ ASH_BattleBrain             전투마다 새로 스폰. ULearningAgentsManager + Interactor + Policy
 │                              0.05초마다 Policy->RunInference (학습기 · 파이썬 없음)
 ├─ ASH_BattlePlayerController  마우스 커서 → 칸, 클릭 처리, 탑다운 카메라, HUD 생성
 └─ UBattleHUDWidget            시작 버튼 · 배치 수 · 결과 (레이아웃은 WBP_BattleHUD)

데이터 에셋
 ├─ USH_UnitDefinition  (UPrimaryDataAsset) 이름 · 아이콘 · 팀별 유닛 클래스 · 정책 스냅샷 폴더
 └─ USH_EnemyLayout     (UPrimaryDataAsset) 적 배치 목록 = (유닛 정의, 칸) 의 배열
```

### 왜 보드가 `ASH_Arena` 를 상속해야 하는가

학습된 정책은 관측을 만들 때 유닛의 `CachedGym` 을 `ASH_Arena` 로 캐스팅해서 이런 값을 읽는다.
- 아군 · 적 목록: `GetTeamUnits`
- 라운드 경과와 상한: `GetRoundElapsed`, `RoundTimeLimitSeconds`
- 안전 구역 반경: `GetZoneRadius`
- 생존 수: `GetAliveCount`, `GetTeamSize`

([SH_ShooterInteractor.cpp](../Source/HAI_Project_TEAM5/RL/SH_ShooterInteractor.cpp) 의 `GatherAgentObservation_Implementation`.)

인게임 보드가 `ASH_Arena` 가 아니면 이 값들이 전부 0 이 된다. 그러면 정책이 학습 때 본 적 없는 입력을 받고 엉뚱하게 움직인다.
`ASH_Arena` 를 상속하면 관측 · 라운드 판정 · 축소 구역을 **그대로 재사용**한다.
([SH_Arena.h:293](../Source/HAI_Project_TEAM5/RL/SH_Arena.h) 의 "출고 보드는 이 함수만 오버라이드한다" 주석이 처음부터 이 설계를 염두에 둔 것이다.)

### 단계 상태 기계

```
            모두 배치 + 시작 클릭              한 팀 전멸 / 45초
 Placement ──────────────────────▶ Battle ──────────────────▶ Result
     ▲                                                          │
     └────────────────────── 다시 배치 클릭 ─────────────────────┘
```

허용되는 전이는 이 세 개뿐이다. 7단계에서 이것을 **컴파일 타임에 검사**한다.

### 만들 파일

| 파일 | 단계 |
|---|---|
| `Source/HAI_Project_TEAM5/Battle/SH_BoardGrid.h/.cpp` | 1 |
| `Source/HAI_Project_TEAM5/Battle/SH_Placement.h/.cpp` | 2 |
| `Source/HAI_Project_TEAM5/Battle/Tests/SH_BoardTests.cpp` | 1·2 |
| `Source/HAI_Project_TEAM5/Battle/SH_UnitDefinition.h/.cpp`, `SH_EnemyLayout.h` | 3 |
| `Source/HAI_Project_TEAM5/Battle/SH_BattleBoard.h/.cpp` + `RL/SH_Arena.h/.cpp` 수정 | 4 |
| `Source/HAI_Project_TEAM5/RL/SH_SnapshotFiles.h/.cpp` (새 공용) + `SH_CombatTrainer.cpp` 수정 | 5 |
| `Source/HAI_Project_TEAM5/Battle/SH_BattleBrain.h/.cpp` | 5 |
| `Source/HAI_Project_TEAM5/Battle/SH_BattlePlayerController.h/.cpp` | 6 |
| `Source/HAI_Project_TEAM5/Battle/SH_BattleGameMode.h/.cpp`, `SH_BattleTypes.h` | 7 |
| `Source/HAI_Project_TEAM5/Battle/BattleHUDWidget.h/.cpp` | 8 |

`Battle/` 폴더는 새로 만든다. 모듈은 그대로(`HAI_Project_TEAM5`)라 Build.cs 는 고칠 필요가 없다.
`UMG`, `EnhancedInput`, `LearningAgents`, `LearningAgentsTraining` 이 이미 의존성에 있다.

### 빌드 루틴

- **헤더를 바꾸거나 새 `UCLASS` / `USTRUCT` 를 만들면** 에디터를 닫고 전체 빌드한다. Live Coding 은 이런 변경을 반영하지 못하고, 켜져 있으면 커맨드라인 빌드도 막는다.

  ```bash
  "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" HAI_Project_TEAM5Editor Win64 Development -Project="C:/UnrealProjects/HAI_Project_TEAM5/HAI_Project_TEAM5.uproject" -WaitMutex
  ```

- `.cpp` 본문만 바꿨으면 에디터의 Live Coding(Ctrl+Alt+F11)으로 충분하다.
- 언리얼은 **경고를 에러로 취급**한다. 이 프로젝트에서 자주 난 경고는 C4458(지역 변수가 멤버 이름을 가림)이다.
  `Controller`, `Character`, `Mesh`, `Role`, `Owner` 같은 이름은 부모 클래스 멤버와 겹치니 지역 변수 이름으로 쓰지 않는다(EC 33).

---

## 2. 0단계 — 에셋 준비

C++ 없이 에디터에서 한다. 이후 단계에서 쓸 재료다.

1. **레벨 `Content/Battle/Lvl_Battle`**
   - 빈 레벨에 조명(Directional Light, Sky Light)과 바닥 높이 기준을 둔다.
   - 보드는 4단계에서 놓는다.
   - 위에서 비스듬히 내려다보는 **CameraActor** 하나를 놓고 태그 `BattleCamera` 를 붙인다. 6단계에서 찾는다.
2. **유닛 블루프린트 두 개** — `Content/RL/BP_Unit_Team0`, `BP_Unit_Team1` 을 `Content/Battle/` 로 **복제**해 `BP_GameUnit_Team0`, `BP_GameUnit_Team1` 로 만든다. 원본은 학습용이라 건드리지 않는다.
   - `AIControllerClass` = **`SH_RLBotController`** (두 팀 모두). 팀 1 원본은 스크립트 봇 컨트롤러다.
   - `AutoPossessAI` = `Placed in World or Spawned` (원본 값 유지 확인)
   - **`bTrainingMode` = false, `bDisableAnimation` = false.** 학습용 처리량 최적화라서 켜 두면 애니메이션이 멈춘 T-포즈로 보인다([SH_RLCharacter.h](../Source/HAI_Project_TEAM5/RL/SH_RLCharacter.h) 주석).
   - `CombatStats.TeamId` 0 / 1, 메시의 Overlay Material(팀 색)은 원본 값이 그대로 따라온다.
3. **정책 스냅샷 경로** — `C:/UnrealProjects/HAI_Project_TEAM5/Snapshots/2026-10-25/cum016h00m`. 3단계 데이터 에셋에 넣는다.

**완료 확인**: 두 BP 를 레벨에 끌어다 놓고 Play 하면 서 있기만 한다(정책이 아직 없으므로 정상). 애니메이션 포즈(대기 자세)로 보여야 한다.

---

## 3. 1단계 — 격자 수학 `FSHBoardGrid`

### 목표

칸 좌표(정수 2개)와 월드 좌표(실수)를 오가는 계산을 **UObject 가 아닌 작은 값 타입**으로 만든다.
게임에서 가장 많이 틀리는 계산(음수 좌표 반올림, 경계)을 여기 가두고 자동화 테스트로 고정한다.

### 설계 수치

| 값 | 근거 |
|---|---|
| 칸 크기 250 cm | 유닛 캡슐 지름 84 cm + 엄폐물 사이를 지나갈 여유 |
| 14 × 14 칸, 원점(보드 기준) (−1750, −1750) | 아레나 반경 1,750 cm 를 덮는다(3,500 / 250 = 14) |
| 아군 구역 = X 방향 0~5열 (보드 기준 x < −250 cm) | 서쪽 절반에서 중앙선과 한 칸 띄움 |

★ 학습 때 아군은 중심에서 1,200 cm 떨어진 호에서 시작했다. 너무 앞(중앙 쪽)이나 벽에 붙여 놓으면 정책이 학습 때 본 적 없는 상황이 된다. 그러면 행동이 어색해질 수 있다. 버그가 아니라 분포 차이다.

### 만들 것

```cpp
// Battle/SH_BoardGrid.h
#pragma once
#include "CoreMinimal.h"

/**
 *  보드 격자. 좌표는 보드 액터 기준 로컬(XY, cm). UObject 가 아니다 — 리플렉션 매크로가 없다.
 *  ★ 보드를 회전시켜 배치하지 않는다는 가정이다(로컬 = 월드 − 보드 위치).
 */
struct FSHBoardGrid
{
    static constexpr double DefaultCellSize = 250.0;

    FVector2D Origin{ -1750.0, -1750.0 };   // (0,0) 칸의 바깥 모서리
    double    CellSize = DefaultCellSize;
    int32     Columns  = 14;                // X 방향 칸 수
    int32     Rows     = 14;                // Y 방향 칸 수

    [[nodiscard]] bool IsInside(FIntPoint Cell) const;
    [[nodiscard]] FVector2D CellCenter(FIntPoint Cell) const;
    [[nodiscard]] TOptional<FIntPoint> LocalToCell(FVector2D Local) const;

    [[nodiscard]] static constexpr int32 ToIndex(int32 X, int32 Y, int32 InColumns) noexcept
    {
        return Y * InColumns + X;
    }
};
```

### C++ 개념

- **값 타입과 Rule of Zero (EMC 17)**
  - 멤버가 전부 값(`FVector2D`, `double`, `int32`)이라 복사 · 이동 · 소멸 함수를 하나도 쓰지 않는다. 컴파일러가 만드는 것이 정답이다.
  - 소멸자를 직접 선언하는 순간 이동 생성자가 자동으로 만들어지지 않는다는 규칙이 EMC 17 의 핵심이다. 그래서 이유 없이 쓰지 않는다.
- **멤버 기본값 초기화와 `{}` (EMC 7, EC 4)**
  - `FVector2D Origin{ -1750.0, -1750.0 };` 처럼 선언하는 자리에서 초기화하면, 어느 생성자로 만들어도 초기화되지 않은 값이 남지 않는다.
  - `{}` 는 축소 변환(`double` → `int32`)을 컴파일 에러로 막는다.
- **`const` 멤버 함수 (EC 3)**: 격자를 바꾸지 않는 조회 함수는 전부 `const`. 보드가 `const FSHBoardGrid&` 로 넘겨도 쓸 수 있다.
- **`constexpr` (EMC 15)**
  - `ToIndex` 는 컴파일 타임에도 계산된다. 그래서 `static_assert(FSHBoardGrid::ToIndex(1, 2, 14) == 29);` 같은 검사를 헤더에 둘 수 있다.
  - `#define` 대신 `static constexpr` 상수를 쓰는 것이 EC 2 다.
- **`TOptional` = `std::optional`**: "칸이 없을 수도 있음"을 반환 타입으로 표현한다. `-1` 같은 특수값을 쓰면 호출하는 쪽이 검사를 잊어도 컴파일된다(EC 18).
- **`[[nodiscard]]`**: 반환값을 버리면 경고가 나고, 언리얼은 그 경고를 에러로 막는다. 조회 함수의 결과를 안 쓰는 것은 거의 항상 버그다.
- **`noexcept` (EMC 14)**: 언리얼은 예외를 끈 채 빌드한다. 그래서 효과보다는 "이 함수는 실패 경로가 없다"는 문서 역할이 크다.

### 코드 조각 — 가장 많이 틀리는 곳

```cpp
TOptional<FIntPoint> FSHBoardGrid::LocalToCell(const FVector2D Local) const
{
    // ★ static_cast<int32>(-0.3) 은 0 이다(0 쪽으로 자른다). 격자 바로 바깥(왼쪽) 점이 0번 칸으로 잘못 들어온다.
    //   내림(Floor)을 써야 −1 이 되어 IsInside 에서 걸러진다.
    const FIntPoint Cell{
        FMath::FloorToInt32((Local.X - Origin.X) / CellSize),
        FMath::FloorToInt32((Local.Y - Origin.Y) / CellSize) };

    if (!IsInside(Cell))
    {
        return {};          // 빈 TOptional
    }
    return Cell;
}
```

`CellCenter` 는 직접 쓴다: 칸의 모서리 `Origin + Cell × CellSize` 에 칸 크기의 절반을 더한다.

### 테스트 — 언리얼 자동화 테스트

```cpp
// Battle/Tests/SH_BoardTests.cpp
#include "Misc/AutomationTest.h"
#include "Battle/SH_BoardGrid.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSHBoardGridTest, "TEAM5.Board.Grid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSHBoardGridTest::RunTest(const FString& Parameters)
{
    const FSHBoardGrid Grid;
    TestEqual(TEXT("원점 칸"), Grid.LocalToCell({ -1749.0, -1749.0 }), TOptional<FIntPoint>(FIntPoint(0, 0)));
    TestFalse(TEXT("격자 바로 바깥"), Grid.LocalToCell({ -1750.1, 0.0 }).IsSet());
    // 직접 추가: 마지막 칸, 칸 경계값(정확히 250의 배수), CellCenter → LocalToCell 왕복
    return true;
}
#endif
```

실행: 에디터 메뉴 **Tools → Session Frontend → Automation** 에서 `TEAM5` 를 검색해 실행한다.
`TestEqual` 이 `TOptional` 비교를 지원하지 않아 컴파일되지 않으면, `IsSet()` 과 `GetValue()` 로 나눠서 비교한다.

### 언리얼 함정

- `FIntPoint` 는 `int32` 두 개, `FVector2D` 는 언리얼 5부터 `double` 두 개다. 섞어 계산할 때 암시적 변환을 믿지 말고 명시한다.
- `UObject` 가 아니므로 `UPROPERTY` 를 붙일 수 없다. 에디터에 노출하려면 `USTRUCT` 로 바꿔야 하는데, 이 단계에서는 일부러 순수 C++ 로 둔다. 테스트가 엔진 없이 돈다.

### 완료 확인

자동화 테스트가 전부 초록. `static_assert` 하나 이상 헤더에 있음.

### 연습 문제

1. `CellCenter` 의 역함수가 `LocalToCell` 이라는 성질(왕복)을 모든 칸에 대해 검사하는 테스트를 쓴다.
2. `IsInside` 를 `constexpr` 로 만들 수 있는지 시도해 본다. `FIntPoint` 의 생성자가 `constexpr` 인지에 따라 결과가 달라진다. 엔진 헤더 `IntPoint.h` 를 읽고 이유를 적는다.

---

## 4. 2단계 — 배치 상태 `FSHPlacement`

### 목표

"어느 칸에 어느 유닛이 있는가"만 관리하는 클래스. **지형(막힌 칸, 아군 구역)은 모른다.** 그 판단은 보드가 한다.
책임을 하나로 줄이면 테스트가 쉽고, 규칙이 바뀌어도 이 클래스는 그대로다.

### 만들 것

```cpp
// Battle/SH_Placement.h
#pragma once
#include "CoreMinimal.h"

class ASH_RLCharacter;

enum class ESHPlaceResult : uint8
{
    Ok,
    Occupied,       // 다른 유닛이 있다
    Full,           // 이미 최대 인원
    NotPlaced,      // 이동/제거하려는 유닛이 배치되어 있지 않다
};

class FSHPlacement
{
public:
    explicit FSHPlacement(int32 InMaxUnits) : MaxUnits(InMaxUnits) {}

    /** 새 유닛을 빈 칸에 둔다. */
    [[nodiscard]] ESHPlaceResult Add(ASH_RLCharacter* Unit, FIntPoint Cell);
    /** 배치된 유닛을 다른 빈 칸으로 옮긴다. */
    [[nodiscard]] ESHPlaceResult Move(const ASH_RLCharacter* Unit, FIntPoint ToCell);
    [[nodiscard]] ESHPlaceResult Remove(const ASH_RLCharacter* Unit);

    [[nodiscard]] ASH_RLCharacter* FindUnit(FIntPoint Cell) const;
    [[nodiscard]] TOptional<FIntPoint> FindCell(const ASH_RLCharacter* Unit) const;
    [[nodiscard]] int32 Num() const { return CellToUnit.Num(); }
    [[nodiscard]] int32 GetMaxUnits() const { return MaxUnits; }
    [[nodiscard]] bool IsFull() const { return Num() >= MaxUnits; }

    /** 배치된 (칸, 유닛) 을 하나씩 넘겨준다. 내부 TMap 을 밖에 내주지 않는다. */
    void ForEach(TFunctionRef<void(FIntPoint, ASH_RLCharacter&)> Visit) const;

private:
    TMap<FIntPoint, TWeakObjectPtr<ASH_RLCharacter>> CellToUnit;
    int32 MaxUnits = 10;
};
```

### C++ 개념

- **쓰기 쉽고 틀리기 어려운 인터페이스 (EC 18)**
  - 결과를 `bool` 이 아니라 `enum class` 로 돌려준다. 호출하는 쪽이 실패 이유별로 다른 메시지를 띄울 수 있다.
  - `[[nodiscard]]` 가 있어서 결과 검사를 잊을 수 없다.
  - `explicit` 생성자는 `FSHPlacement P = 10;` 같은 의도치 않은 변환을 막는다.
- **범위 있는 열거형 (EMC 10)**: `ESHPlaceResult::Ok` 처럼 써야 하고, `int` 로 암시적으로 변환되지 않는다. 언리얼 리플렉션에 노출할 때만 `UENUM()` 을 붙인다.
- **데이터 멤버는 private (EC 22)**: `CellToUnit` 을 public 으로 두면 누가 맵을 직접 고쳐 "같은 유닛이 두 칸에 있음" 같은 상태를 만들 수 있다. 불변식(한 유닛은 최대 한 칸)을 이 클래스가 지킨다.
- **내부 핸들 반환 금지 (EC 28)**: `const TMap<...>& GetMap()` 을 주는 대신 `ForEach` 로 방문만 허용한다. `TFunctionRef` 는 소유하지 않는 함수 참조라 할당이 없다. 호출 동안만 유효하다(이 차이는 부록의 표 참고).
- **약한 참조 (EMC 20 의 언리얼판)**: `TWeakObjectPtr` 은 유닛이 파괴되면 `nullptr` 이 된다. 이 클래스는 유닛의 수명을 소유하지 않으므로 약한 참조가 맞다.
- **`TMap` 의 키**: `FIntPoint` 는 엔진이 `GetTypeHash` 를 제공해서 바로 키로 쓸 수 있다. 직접 만든 구조체를 키로 쓰려면 `operator==` 와 `GetTypeHash` 를 정의해야 한다(연습 2).
- **`const` 포인터 매개변수**: `Move(const ASH_RLCharacter* Unit, …)` 는 "유닛을 바꾸지 않고 식별에만 쓴다"는 뜻이다. `Add` 는 맵에 저장하므로 non-const.

### 코드 조각 — `ForEach`

```cpp
void FSHPlacement::ForEach(TFunctionRef<void(FIntPoint, ASH_RLCharacter&)> Visit) const
{
    for (const TPair<FIntPoint, TWeakObjectPtr<ASH_RLCharacter>>& Pair : CellToUnit)
    {
        if (ASH_RLCharacter* Unit = Pair.Value.Get())   // 파괴된 유닛은 건너뛴다
        {
            Visit(Pair.Key, *Unit);
        }
    }
}
```

`FindCell` 은 맵을 한 바퀴 도는 선형 탐색이면 충분하다. 유닛이 최대 10명이라 역방향 맵을 따로 두는 복잡도를 감수할 이유가 없다.
최적화는 측정한 뒤에 한다.

### 언리얼 함정

- `TMap` 을 순회하는 도중에 `Remove` 하면 안 된다. 지울 키를 모았다가 루프 밖에서 지운다.
- `TWeakObjectPtr` 을 `Get()` 없이 비교하면 "파괴된 유닛 == nullptr" 판정이 의도와 다를 수 있다. 비교 전에 `IsValid()` 또는 `Get()` 을 쓴다.

### 완료 확인

테스트 `TEAM5.Board.Placement` 를 만든다. 문제는 유닛이다.
- `TWeakObjectPtr` 은 실제 UObject 를 요구해서, 테스트에서 임의의 주소를 유닛인 척 넣을 수 없다.
- `ASH_RLCharacter` 는 액터라 월드 없이 만들 수도 없다.

그래서 이 단계의 자동화 테스트는 유닛 없이 확인되는 것만 검사한다. 예를 들면 빈 상태의 `Num()`·`IsFull()`, 없는 유닛의 `Remove` → `NotPlaced` 같은 것이다. 유닛이 필요한 검사(Occupied, Full)는 4단계의 PIE 확인으로 넘긴다.
→ 이 불편이 **연습 3** 의 동기다.

### 연습 문제

1. `Move` 를 "목적지에 다른 유닛이 있으면 서로 자리를 바꾼다"로 바꾸려면 반환 타입에 무엇을 추가해야 할지 설계해 본다.
2. `struct FSHCellKey { int32 X; int32 Y; };` 를 만들어 `TMap` 키로 쓰고, 필요한 `operator==` 와 `GetTypeHash` 를 정의한다. C++20 의 `= default` 비교 연산자도 써 본다.
3. `FSHPlacement` 를 유닛 타입에 대한 **템플릿**(`TSHPlacement<UnitType>`)으로 바꾸면, 테스트에서 UObject 가 아닌 가짜 타입을 넣을 수 있다. 장단점을 적는다(헤더에 구현이 들어가야 한다는 점 포함, EC 30).

---

## 5. 3단계 — 데이터 에셋

### 목표

유닛 종류와 적 배치를 **코드가 아니라 데이터**로 둔다. 지금은 1종이지만, 종류를 늘릴 때 코드를 고치지 않고 에셋만 추가하게 한다.

### 만들 것

```cpp
// Battle/SH_UnitDefinition.h
UCLASS(BlueprintType)
class HAI_PROJECT_TEAM5_API USH_UnitDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    [[nodiscard]] TSubclassOf<ASH_RLCharacter> GetUnitClass(int32 Team) const
    {
        return Team == 0 ? UnitClassTeam0 : UnitClassTeam1;
    }
    [[nodiscard]] const FText& GetDisplayName() const { return DisplayName; }
    [[nodiscard]] const FString& GetPolicyDirectory() const { return PolicySnapshotDirectory.Path; }

private:
    UPROPERTY(EditDefaultsOnly, Category = "Unit")
    FText DisplayName;

    UPROPERTY(EditDefaultsOnly, Category = "Unit")
    TObjectPtr<UTexture2D> Icon;

    UPROPERTY(EditDefaultsOnly, Category = "Unit")
    TSubclassOf<ASH_RLCharacter> UnitClassTeam0;      // BP_GameUnit_Team0

    UPROPERTY(EditDefaultsOnly, Category = "Unit")
    TSubclassOf<ASH_RLCharacter> UnitClassTeam1;      // BP_GameUnit_Team1

    /** 이 유닛을 조종하는 정책. 종류마다 관측·행동이 다르면 정책도 달라야 한다. */
    UPROPERTY(EditDefaultsOnly, Category = "Unit")
    FDirectoryPath PolicySnapshotDirectory;
};
```

```cpp
// Battle/SH_EnemyLayout.h
USTRUCT(BlueprintType)
struct FSHEnemySlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) TObjectPtr<USH_UnitDefinition> Unit;
    UPROPERTY(EditAnywhere) FIntPoint Cell = FIntPoint::ZeroValue;
};

UCLASS(BlueprintType)
class HAI_PROJECT_TEAM5_API USH_EnemyLayout : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "Layout")
    TArray<FSHEnemySlot> Slots;
};
```

에디터에서 `DA_Unit_Shooter`(USH_UnitDefinition)와 `DA_EnemyLayout_01`(USH_EnemyLayout)을 만든다.
적 칸은 동쪽(열 8~13)에 10개를 넣는다. 예를 들어 열 10~11, 행 2~11 에 지그재그로 넣으면 된다.

### C++ 개념

- **합성 (EC 38)**
  - "슈터 유닛은 유닛 정의를 **가진다**". 유닛 클래스가 정의 클래스를 상속하지 않는다.
  - 정의는 공유되는 읽기 전용 데이터이고, 유닛은 그 데이터를 참조하는 인스턴스다.
- **public 상속은 is-a (EC 32)**: `USH_UnitDefinition` 은 **하나의** `UPrimaryDataAsset` 이다. 에셋 관리자가 기대하는 모든 동작(ID, 로드)이 그대로 맞으므로 상속이 옳다.
- **`const&` 반환과 그 한계 (EC 28)**: `GetDisplayName()` 이 멤버의 `const&` 를 돌려주는 것은 에셋이 살아 있는 동안만 안전하다. 반환된 참조가 에셋보다 오래 남으면 이미 해제된 메모리를 가리킨다. 데이터 에셋은 보통 오래 살지만, 이 가정을 주석으로 남긴다.
  반대로 함수 안에서 새로 만든 값을 참조로 돌려주는 것은 항상 틀렸다(EC 21). 그때는 값으로 반환한다.
- **`TSubclassOf<T>` vs `TSoftClassPtr<T>`**
  - `TSubclassOf` 는 에셋을 참조하는 순간 클래스가 메모리에 올라온다(하드 참조).
  - `TSoftClassPtr` 은 경로만 들고 있다가 필요할 때 로드한다(소프트 참조).
  - 지금은 1종이라 하드 참조가 단순하다. 종류가 수십 개가 되면 소프트 참조로 바꾼다(9단계).

### 언리얼 함정

- `UPROPERTY` 가 없는 `UObject*` 멤버는 가비지 컬렉터가 모른다. 언제든 지워질 수 있고 에러도 나지 않는다. **UObject 를 가리키는 멤버에는 반드시 `UPROPERTY`.**
- `private` `UPROPERTY` 도 `EditDefaultsOnly` 면 에디터에서 편집된다. 다만 블루프린트 그래프에서 읽으려면 `meta = (AllowPrivateAccess = "true")` 가 필요하다.

### 완료 확인

두 데이터 에셋을 만들고 값이 저장된다. `DA_EnemyLayout_01` 에 칸 10개.

---

## 6. 4단계 — 보드 `ASH_BattleBoard`

### 목표

`ASH_Arena` 를 상속해서 다음을 맡는다.
- 칸 판정: 아군 구역, 막힌 칸
- 유닛 스폰
- 배치 상태 관리
- 전투 시작과 종료 감지

그 전에 **아레나에 확장 지점 하나**를 추가해야 한다.

### 4-1. 아레나에 가상 훅 추가 (기존 코드 수정)

**왜 필요한가.** 전투 시작은 `ASH_Arena::ResetRound()` 한 번이면 된다. 이 함수가 유닛을 리셋하고 `ApplySpawnSlots` 로 배치한 뒤 라운드를 시작한다.
그런데 `ApplySpawnSlots`([SH_Arena.cpp](../Source/HAI_Project_TEAM5/RL/SH_Arena.cpp))는 **"슬롯 i → 유닛 목록의 i번째"**로 배치한다.
유닛 목록은 `CollectUnits` 가 `TActorIterator` 로 월드를 훑은 순서라 보장된 순서가 없다. 플레이어가 A 를 (2,3) 에 놓았어도 B 가 거기로 갈 수 있다.
유닛이 1종이면 겉보기에 같지만, 종류가 생기면 바로 틀린다.

**수정** — `SH_Arena.h` 의 `protected:` 에 추가:

```cpp
/**
 *  유닛 하나의 시작 위치를 직접 정하는 훅. true 를 돌려주면 ApplySpawnSlots 가 슬롯 대신 이 값을 쓴다.
 *  기본 구현은 false — 학습 아레나의 동작은 바뀌지 않는다. 인게임 보드가 플레이어 배치를 돌려준다.
 */
virtual bool GetSpawnTransformForUnit(const ABase_Character* Unit, int32 Team, FTransform& OutTransform) const
{
    return false;
}
```

`SH_Arena.cpp` 의 `ApplySpawnSlots` 루프에서 슬롯을 고르는 줄을 바꾼다.

```cpp
ABase_Character* Unit = Units[Index];
FTransform Custom;
const FTransform& Slot = GetSpawnTransformForUnit(Unit, Team, Custom) ? Custom : Slots[Index];
```

**C++ 개념**
- **템플릿 메서드 / NVI (EC 35)**
  - `ApplySpawnSlots` 는 가상이 아닌 고정된 흐름이다(목록 갱신 → 위치 결정 → 순간이동 → 조준 회전).
  - 자식이 바꿀 수 있는 것은 "위치 결정" 한 점뿐이다. 흐름 전체를 가상으로 열면 자식이 목록 갱신을 빠뜨릴 수 있다.
- **비가상 함수를 재정의하지 않는다 (EC 36)**: 보드에서 `ApplySpawnSlots` 와 같은 이름의 함수를 새로 만들면 아레나 코드는 여전히 부모 버전을 부른다. 그래서 훅을 연 것이다.
- **조건 연산자의 값 범주**
  - `const FTransform& Slot = 조건 ? Custom : Slots[Index];` 에서 두 피연산자가 같은 타입의 lvalue 라서 결과도 lvalue 다. 참조가 그중 하나에 직접 묶인다.
  - 한쪽이 임시 객체(예: 함수가 값으로 돌려준 `FTransform`)면 결과는 prvalue 가 된다. 참조는 임시 객체에 묶이고 수명이 연장된다. 이 경우도 동작은 하지만, 왜 그런지 설명할 수 있어야 한다.

**완료 확인**: 수정 후 학습 레벨 `Lvl_SHTrain` 을 3분 돌려 기존과 같이 동작하는지 본다. 기본 구현이 `false` 라 학습 결과에는 영향이 없어야 한다.

### 4-2. 보드 클래스

```cpp
// Battle/SH_BattleBoard.h
#pragma once
#include "CoreMinimal.h"
#include "RL/SH_Arena.h"
#include "Battle/SH_BoardGrid.h"
#include "Battle/SH_Placement.h"
#include "SH_BattleBoard.generated.h"

class ASH_RLCharacter;
class USH_UnitDefinition;
class USH_EnemyLayout;

DECLARE_MULTICAST_DELEGATE_OneParam(FSHOnBattleEnded, ESHRoundEnd /*Result*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FSHOnPlacementChanged, int32 /*Placed*/, int32 /*Required*/);

UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattleBoard : public ASH_Arena
{
    GENERATED_BODY()

public:
    ASH_BattleBoard();

    //~ 배치 단계 ---------------------------------------------------------
    /** 칸 클릭 하나를 처리한다: 빈 칸이면 아군 추가, 아군 칸이면 선택, 선택 중이면 이동. */
    void HandleCellClicked(FIntPoint Cell);
    void HandleCellRightClicked(FIntPoint Cell);   // 아군 제거
    [[nodiscard]] TOptional<FIntPoint> WorldToCell(const FVector& World) const;
    [[nodiscard]] bool IsAllyCell(FIntPoint Cell) const;
    [[nodiscard]] bool IsBlocked(FIntPoint Cell) const;
    [[nodiscard]] bool IsPlacementComplete() const { return AllyPlacement.IsFull(); }

    //~ 전투 ---------------------------------------------------------------
    /** 배치된 유닛으로 라운드를 시작한다(ResetRound). */
    void StartBattle();
    /** 결과 화면 뒤 — 유닛을 되살려 각자의 칸으로 돌려놓는다. 배치는 유지된다. */
    void ReturnToPlacement();
    /** 정책이 조종할 유닛 전부(양 팀). Brain 이 등록한다. */
    [[nodiscard]] TArray<ASH_RLCharacter*> GetAllUnits() const;

    FSHOnBattleEnded OnBattleEnded;
    FSHOnPlacementChanged OnPlacementChanged;

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;
    virtual bool GetSpawnTransformForUnit(const ABase_Character* Unit, int32 Team, FTransform& OutTransform) const override;

private:
    [[nodiscard]] ASH_RLCharacter* SpawnUnit(const USH_UnitDefinition& Definition, int32 Team, FIntPoint Cell);
    [[nodiscard]] FTransform CellTransform(FIntPoint Cell, int32 Team) const;
    void SpawnEnemies();
    void RebuildBlockedCells();

    UPROPERTY(EditAnywhere, Category = "Battle")
    TObjectPtr<USH_UnitDefinition> AllyDefinition;

    UPROPERTY(EditAnywhere, Category = "Battle")
    TObjectPtr<USH_EnemyLayout> EnemyLayout;

    UPROPERTY(EditAnywhere, Category = "Battle", meta = (ClampMin = "1"))
    int32 AllyColumns = 6;

    FSHBoardGrid Grid;
    FSHPlacement AllyPlacement{ 10 };
    FSHPlacement EnemyPlacement{ 10 };
    TBitArray<> BlockedCells;
    TOptional<FIntPoint> SelectedCell;
    bool bBattleRunning = false;
};
```

### C++ 개념

- **`override` (EMC 12)**
  - `GetSpawnTransformForUnit` 의 시그니처를 하나라도 틀리면(예: `const` 빠짐) `override` 가 컴파일 에러를 낸다.
  - 없으면 이름만 같은 **새 함수**가 생기고 아레나는 계속 부모 버전을 부른다. 에러 없이 배치가 무시된다.
- **생성자에서 가상 함수를 부르지 않는다 (EC 9)**
  - 언리얼은 클래스마다 **CDO(Class Default Object)** 를 엔진 시작 때 생성자로 만든다. 그래서 생성자에서는 월드도, 다른 액터도 없다.
  - 막힌 칸 계산(`RebuildBlockedCells`)은 월드 충돌을 조회하므로 `BeginPlay` 에서 한다.
- **멤버 초기화 순서 (EC 4)**: 멤버는 **선언 순서**대로 초기화된다. `AllyPlacement{ 10 }` 이 `Grid` 의 값을 쓰게 바꾼다면 `Grid` 가 먼저 선언되어 있어야 한다.
- **이름 가림 (EC 33)**: 부모 `ASH_Arena` 에 `Tick` 이 있다. 보드에서 `Tick(float)` 를 `override` 하면 같은 이름의 다른 오버로드가 가려질 수 있다. 엔진 클래스에서는 드물지만 알고 있어야 한다.
- **`TBitArray<>`**: 칸 196개의 막힘 여부를 비트 하나씩 저장한다. `bool` 배열보다 메모리가 8분의 1이고, 인덱스는 1단계의 `ToIndex` 로 만든다.

### 코드 조각 — 유닛 스폰

```cpp
ASH_RLCharacter* ASH_BattleBoard::SpawnUnit(const USH_UnitDefinition& Definition, const int32 Team, const FIntPoint Cell)
{
    const TSubclassOf<ASH_RLCharacter> UnitClass = Definition.GetUnitClass(Team);
    UWorld* World = GetWorld();
    if (!UnitClass || !World)
    {
        return nullptr;
    }

    FActorSpawnParameters Params;
    // ★ 캡슐이 바닥·이웃과 살짝 겹쳐도 스폰한다. 기본값이면 스폰이 조용히 실패해 nullptr 이 온다.
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ASH_RLCharacter* Unit = World->SpawnActor<ASH_RLCharacter>(UnitClass, CellTransform(Cell, Team), Params);
    if (!Unit)
    {
        return nullptr;
    }

    // ★ 이 한 줄이 유닛을 "이 보드 소속"으로 만든다. Base_Character::InitializeEntity_Implementation 이
    //   CachedGym = this 를 넣고, 엔진의 IsMemberOfGym 은 GetGym() == this 로 소속을 판정한다.
    //   빠뜨리면 CollectUnits 가 이 유닛을 무시해 관측의 아군·적 목록이 비고, 라운드 판정도 안 된다.
    ILearningAgentsEntityInterface::Execute_InitializeEntity(Unit, this);

    // BP 의 AutoPossessAI 가 맞으면 이미 컨트롤러가 있다. 없을 때만 만든다.
    if (!Unit->GetController())
    {
        Unit->SpawnDefaultController();
    }
    return Unit;
}
```

`CellTransform` 은 직접 쓴다.
- 위치 = 보드 위치 + `Grid.CellCenter(Cell)` + Z 로 `SpawnZOffset`(아레나 속성, 100 cm)
- 회전 = 팀 0 은 yaw 0(동쪽을 봄), 팀 1 은 yaw 180

### 코드 조각 — 막힌 칸 계산

```cpp
void ASH_BattleBoard::RebuildBlockedCells()
{
    BlockedCells.Init(false, Grid.Columns * Grid.Rows);

    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(42.0f, 96.0f);   // 유닛 캡슐 반경·반높이
    // ★ 세 번째 인자로 this 를 넘기면(무시할 액터) 안 된다. 엄폐물과 벽이 이 보드의 컴포넌트라 검사에서 빠진다.
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(BoardCell), /*bTraceComplex=*/false);
    const float SafeRadius = ArenaRadius - ClampMargin;

    for (int32 Y = 0; Y < Grid.Rows; ++Y)
    {
        for (int32 X = 0; X < Grid.Columns; ++X)
        {
            const FIntPoint Cell{ X, Y };
            const FVector2D Local = Grid.CellCenter(Cell);
            const bool bOutside = Local.Size() > SafeRadius;
            const FVector Where = CellTransform(Cell, 0).GetLocation();
            const bool bHitsCover = !bOutside &&
                GetWorld()->OverlapAnyTestByChannel(Where, FQuat::Identity, ECC_WorldStatic, Capsule, Query);
            BlockedCells[FSHBoardGrid::ToIndex(X, Y, Grid.Columns)] = bOutside || bHitsCover;
        }
    }
}
```

캡슐 중심이 바닥에서 100 cm 위이고 반높이가 96 cm 라서, 캡슐 바닥이 바닥면보다 4 cm 위에 있다. 그래서 바닥 자체와는 겹치지 않고 엄폐물과 벽만 잡힌다.
`DrawDebugBox` 로 막힌 칸을 빨갛게 그려 눈으로 확인한다.
★ **유닛을 스폰하기 전에** 계산한다. 유닛 캡슐도 WorldStatic 채널을 막아서, 나중에 계산하면 유닛이 선 칸이 막힘이 된다.

### 코드 조각 — 전투 시작과 종료 감지

```cpp
void ASH_BattleBoard::StartBattle()
{
    bBattleRunning = true;
    ResetRound();     // 유닛 리셋 → ApplySpawnSlots(→ 우리 훅) → BeginRound
}

void ASH_BattleBoard::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);      // 아레나의 라운드 판정 · 축소 구역

    // 아레나는 라운드가 끝나면 상태만 바꾸고 아무에게도 알리지 않는다(학습 환경이 매 결정 IsRoundOver 를 묻는다).
    // 게임은 "끝났다"를 한 번만 알려야 하므로 여기서 감지해 델리게이트로 방송한다.
    if (bBattleRunning && IsRoundOver())
    {
        bBattleRunning = false;
        // ★ 정책이 멈춰도 유닛의 Pending 입력은 남아 Tick 이 계속 적용한다. 비우지 않으면 결과 화면에서
        //   생존자가 마지막 방향으로 계속 걷는다.
        for (ASH_RLCharacter* Unit : GetAllUnits())
        {
            Unit->ClearPendingInput();
        }
        OnBattleEnded.Broadcast(GetRoundEnd());
    }
}
```

**배치 단계에서 라운드 판정이 돌지 않는 이유**를 이해해야 한다. 아레나 `Tick` 은 두 조건 중 하나면 아무것도 하지 않는다.
1. 팀 유닛 캐시(`TeamUnits`)가 비어 있다. 이 캐시는 `ApplySpawnSlots` 에서만 채워지므로 첫 `StartBattle` 전에는 비어 있다.
2. 이미 끝난 라운드(`RoundEnd != None`)다. "다시 배치" 중이 이 경우다.

이 두 불변식이 깨지면(예: 누가 배치 중에 `ResetRound` 를 부르면) 배치 중에 45초 시간 초과가 난다. → 연습 1.

### 언리얼 함정

- **`BP_BattleBoard` 의 엄폐물은 학습 아레나와 같아야 한다.** 다르면 정책이 학습 때와 다른 지형에서 싸운다.
- **`BP_BattleBoard` 는 `BP_SHArena` 를 복제한 뒤 부모를 `SH_BattleBoard` 로 바꿔 만든다.** 엄폐물을 손으로 옮기지 않아도 된다.
- `Team0UnitClass` / `Team1UnitClass` 는 **그대로 둔다.** 이 둘은 학습용 엔티티 풀이 쓰는 값인데, 보드는 GymsManager 가 초기화하지 않으므로 풀이 유닛을 스폰하지 않는다(엔진의 `InitializeLearningComponent` 는 Gym 초기화 때만 불린다). 비우면 엔티티 매니저가 BeginPlay 에서 `No entity classes` 에러를 찍는다.
- **유닛을 없앨 때 총도 없앤다.** 총은 별도 액터라 `Unit->Destroy()` 만으로는 빈 자리에 총이 남는다.
- `UnitsPerTeam` 은 10 으로 둔다. `ValidateSpawnSlotsClear` 같은 검사가 이 값을 쓴다.
- 보드 액터를 **회전시켜 놓지 않는다.** 아레나의 중심 · 스폰 계산과 격자 변환이 모두 회전 0 을 가정한다.

### 완료 확인 (PIE)

- 시작하면 적 10명이 `DA_EnemyLayout_01` 의 칸에 서 있다.
- 아직 입력이 없으니 테스트용으로 `BeginPlay` 에서 아군 10명을 임의의 칸에 넣는다. 그다음 `StartBattle` 을 5초 뒤에 부르는 임시 타이머를 단다. 이 시점에는 정책이 없으므로 유닛은 서 있기만 한다.
- 45초 뒤 `OnBattleEnded` 가 `Timeout` 으로 한 번만 불리는지 로그로 확인한다.

### 연습 문제

1. 위의 "두 불변식"에 기대지 않도록, 보드가 자기 단계(`bBattleRunning`)가 아닐 때 라운드 판정을 확실히 막는 방법을 설계한다. 아레나에 훅을 하나 더 여는 방법과 그렇지 않은 방법을 비교한다.
2. `CellTransform` 이 보드 회전도 지원하게 만든다(`GetActorTransform().TransformPosition`). 그러면 1단계의 "회전 0 가정" 주석을 지울 수 있는가?

---

## 7. 5단계 — 정책 실행 `ASH_BattleBrain`

### 목표

학습기와 파이썬 없이, 학습된 정책을 불러와 **0.05초마다 추론**한다. 양 팀 20명을 한 정책이 조종한다.
이 일은 학습 코드의 **평가 모드**가 이미 하고 있다. [SH_CombatTrainer.cpp](../Source/HAI_Project_TEAM5/RL/SH_CombatTrainer.cpp) `Assemble` 의 611~649행 순서를 그대로 따른다.

### 5-1. 스냅샷 파일 헬퍼를 공용으로 옮기기

`Snapshots/<날짜>/cum<누적>/cum<누적>_policy.bin` 이름 규칙을 처리하는 `SnapshotFile` 은 지금 `SH_CombatTrainer.cpp` 의 **익명 네임스페이스** 안에 있다. 다른 파일에서 부를 수 없다.

```cpp
// RL/SH_SnapshotFiles.h
#pragma once
#include "CoreMinimal.h"

namespace SHSnapshot
{
    /** 스냅샷 폴더 안 파일의 실제 경로. 새 형식(cum..._policy.bin)과 옛 형식(policy.bin) 모두. */
    HAI_PROJECT_TEAM5_API FString ResolveFile(const FString& Directory, const TCHAR* FileName);
}
```

본문은 기존 `SnapshotFile` 을 그대로 `.cpp` 로 옮기고, `SH_CombatTrainer.cpp` 의 호출을 `SHSnapshot::ResolveFile` 로 바꾼다.

**C++ 개념**
- **링크 범위**
  - 익명 네임스페이스 안의 이름은 **내부 링크**라서 그 `.cpp` 밖에서 보이지 않는다.
  - 이름 있는 네임스페이스의 함수 선언을 헤더에 두면 **외부 링크**가 되어 여러 파일이 같은 함수를 부른다.
  - 헤더에 `inline` 없이 함수 **본문**을 두면, 그 헤더를 포함한 `.cpp` 마다 정의가 생겨 링크 에러(중복 정의, ODR 위반)가 난다.
- **비멤버 함수 (EC 23)**: 이 헬퍼는 어떤 객체의 상태도 필요 없다. 그래서 클래스의 정적 멤버보다 네임스페이스 함수가 맞다.
- **컴파일 의존성 (EC 31)**: 이 헤더는 `CoreMinimal.h` 만 포함한다. 학습 클래스 헤더를 포함하지 않으므로 인게임 코드가 학습 코드에 묶이지 않는다.

### 5-2. Brain 클래스

```cpp
// Battle/SH_BattleBrain.h
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattleBrain : public AActor
{
    GENERATED_BODY()

public:
    ASH_BattleBrain();

    /** 유닛을 에이전트로 등록하고 정책을 불러온다. 실패하면 false — 이유는 로그에. */
    [[nodiscard]] bool Assemble(const TArray<ASH_RLCharacter*>& Units, const FString& SnapshotDirectory);

    /** 정책 메모리를 비우고 추론을 시작한다. */
    void StartThinking();
    void StopThinking() { bThinking = false; }

    virtual void Tick(float DeltaSeconds) override;

private:
    [[nodiscard]] bool LoadSnapshot(const FString& Directory);

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<ULearningAgentsManager> Manager;

    UPROPERTY()
    TObjectPtr<USH_ShooterInteractor> Interactor;

    UPROPERTY()
    TObjectPtr<ULearningAgentsPolicy> Policy;

    TArray<int32> AgentIds;
    float DecisionAccumulator = 0.0f;
    bool bThinking = false;

    static constexpr float DecisionPeriod = 1.0f / 20.0f;   // 학습과 같은 20 Hz
};
```

생성자: `Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("Manager"));`, 틱 켜기.

### 코드 조각 — 조립 순서 (순서가 틀리면 에러 없이 안 돈다)

```cpp
bool ASH_BattleBrain::Assemble(const TArray<ASH_RLCharacter*>& Units, const FString& SnapshotDirectory)
{
    if (Units.Num() == 0) { return false; }

    // 1. 에이전트 상한은 리스너(Interactor·Policy)를 만들기 **전에** 정한다.
    Manager->SetMaxAgentNum(Units.Num());

    // 2. Interactor. MakeInteractor 는 매니저 포인터를 lvalue 참조로 받는다 — 지역 변수에 담아 넘긴다.
    ULearningAgentsManager* ManagerPtr = Manager;
    ULearningAgentsInteractor* InteractorPtr = ULearningAgentsInteractor::MakeInteractor(
        ManagerPtr, USH_ShooterInteractor::StaticClass(), TEXT("BattleInteractor"));
    Interactor = Cast<USH_ShooterInteractor>(InteractorPtr);
    if (!Interactor) { return false; }

    // ★ 관측 정규화 값을 유닛과 맞춘다. 학습 때와 다르면 '본 지 몇 초'·'맞은 지 몇 초' 관측이 0~1 을 벗어난다.
    Interactor->MemoryHorizonSeconds    = Units[0]->Vision->MemoryHorizonSeconds;
    Interactor->HitMemoryHorizonSeconds = Units[0]->HitMemoryHorizonSeconds;

    // 3. Policy — 무작위 초기화로 만든 뒤 스냅샷으로 덮어쓴다. 크리틱·학습 환경은 만들지 않는다.
    Policy = ULearningAgentsPolicy::MakePolicy(
        ManagerPtr, InteractorPtr, ULearningAgentsPolicy::StaticClass(), TEXT("BattlePolicy"),
        /*Encoder*/ nullptr, /*Policy*/ nullptr, /*Decoder*/ nullptr,
        /*bReinitEncoder*/ true, /*bReinitPolicy*/ true, /*bReinitDecoder*/ true,
        FLearningAgentsPolicySettings(), /*Seed*/ 1234);
    if (!Policy || !LoadSnapshot(SnapshotDirectory)) { return false; }

    // 4. 에이전트 등록.
    AgentIds.Reset(Units.Num());
    for (ASH_RLCharacter* Unit : Units)
    {
        AgentIds.Add(Manager->AddAgent(Unit));
    }
    return true;
}
```

`LoadSnapshot` 은 직접 쓴다. `policy.bin`, `encoder.bin`, `decoder.bin` 세 파일을 `SHSnapshot::ResolveFile` 로 찾는다. 그다음 `Policy->GetPolicyNetworkAsset()` / `GetEncoderNetworkAsset()` / `GetDecoderNetworkAsset()` 의 `LoadNetworkFromSnapshot(FFilePath)` 에 넘긴다.
`ASH_CombatTrainer::LoadPolicySnapshot` 이 같은 일을 하니 읽어 보고 따라 쓴다.

### 코드 조각 — 20 Hz 추론

```cpp
void ASH_BattleBrain::StartThinking()
{
    Manager->ResetAgents(AgentIds);    // 정책의 순환 메모리를 비운다(지난 전투의 기억이 새지 않게)
    DecisionAccumulator = 0.0f;
    bThinking = true;
}

void ASH_BattleBrain::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bThinking || !Policy) { return; }

    DecisionAccumulator += DeltaSeconds;
    if (DecisionAccumulator < DecisionPeriod) { return; }

    // ★ 0 으로 되돌리지 않고 주기만큼 뺀다 — 프레임 간격이 흔들려도 평균 20 Hz 가 유지된다.
    // ★ while 로 여러 번 돌리지 않는다. 같은 프레임에 두 번 추론하면 게임이 진행되지 않은 같은 관측으로
    //   행동만 덮어쓴다. 학습 코드(ASH_CombatTrainer::Tick)도 한 프레임에 한 번이다.
    DecisionAccumulator -= DecisionPeriod;
    Policy->RunInference(1.0f);       // 관측 수집 → 마스크 → 신경망 → 행동 실행까지 전부
}
```

`RunInference` 의 인자는 행동을 뽑을 때의 잡음 배율이다. 1.0 은 학습 때와 같은 확률 분포에서 뽑는다. 0 이면 항상 가장 확률이 높은 행동만 고르는데, 이 정책은 확률적으로 학습됐으므로 1.0 을 기본으로 둔다.

### C++ 개념 — 소유권

| 대상 | 표준 C++ | 언리얼 |
|---|---|---|
| UObject (Manager, Policy, 유닛) | 해당 없음 | `UPROPERTY` 가 붙은 `TObjectPtr` 이 **가비지 컬렉터에게** 참조를 알린다. 해제는 GC 가 한다 |
| UObject 가 아닌 객체를 혼자 소유 | `std::unique_ptr` (EMC 18) | `TUniquePtr` |
| 그 생성 | `std::make_unique` (EMC 21) | `MakeUnique` |
| 소유하지 않고 지켜봄 | `std::weak_ptr` (EMC 20) | UObject 는 `TWeakObjectPtr`, 일반 객체는 `TWeakPtr` |

- UObject 를 `TUniquePtr` 에 넣으면 안 된다. 메모리를 GC 와 스마트 포인터가 동시에 관리하게 되어, 한쪽이 지운 것을 다른 쪽이 또 지운다.
- `Interactor`, `Policy` 에 `UPROPERTY()` 를 빼면 컴파일은 되지만, 다음 GC 때 지워질 수 있다. 그러면 몇 분 뒤 무작위로 크래시한다.
- **자원은 객체에 맡긴다 (EC 13)**: Brain 액터가 사라지면 Manager(서브오브젝트)도 같이 정리된다. 그래서 GameMode 는 전투마다 Brain 을 새로 스폰하고, 끝나면 `Destroy()` 한다. 유닛을 바꿨을 때 에이전트를 하나하나 지우는 코드가 필요 없다.

### 언리얼 함정

- 정책을 조립할 때 Interactor 가 유닛 하나의 `Vision` 을 읽으므로 유닛이 먼저 있어야 한다. **배치가 끝난 뒤, 시작 버튼을 눌렀을 때** 조립한다.
- 스냅샷의 관측 크기는 176 이다. 관측 스키마(Interactor)를 바꾼 빌드에서 옛 스냅샷을 불러오면 엔진이 "Invalid Format" 을 찍는다. 이때 정책은 무작위 상태로 남는다.
- `Snapshots/` 폴더 경로는 **에디터 PIE 에서만** 그대로 쓸 수 있다. 패키징은 9단계.

### 완료 확인 (PIE)

4단계의 임시 코드(아군 10명 자동 배치 + 5초 뒤 시작)에 Brain 을 붙인다. 두 팀이 움직이며 싸우고, 보통 10~15초 안에 한쪽이 전멸해야 한다(학습 마지막 평균 라운드 10.5초).
`Lvl_SHView` 와 비슷한 움직임이면 성공이다.

### 연습 문제

1. `Assemble` 이 실패 이유를 `bool` 대신 `enum class` 나 `TValueOrError<void, FString>` 로 돌려주게 바꾼다. 호출하는 쪽 코드가 어떻게 달라지는지 비교한다(EC 18).
2. Brain 을 액터 대신 `UActorComponent` 로 만들어 GameMode 에 붙이는 설계와 비교한다. 수명은 누가 관리하고, Tick 은 어떻게 받는가?

---

## 8. 6단계 — 입력과 카메라 `ASH_BattlePlayerController`

### 목표

마우스 커서 아래의 바닥 점 → 칸 → 보드의 `HandleCellClicked` 로 넘긴다. HUD 위젯도 여기서 만든다.
시점은 0단계에서 놓은 `BattleCamera` 를 시작 위치로 삼아 컨트롤러가 직접 움직인다: WASD 평행 이동(Shift 가속), Q/E 회전, 가운데 버튼 끌기 회전, 휠 확대·축소. `ASH_SpectatorPawn` 은 커서를 숨기고 우클릭으로 시점을 돌려서 칸 선택·우클릭 제거와 부딪힌다.

### 에셋

- `Content/Battle/Input/IA_Select`(Bool), `IA_Remove`(Bool), `IMC_Battle`(왼쪽 클릭 → IA_Select, 오른쪽 클릭 → IA_Remove).

### 만들 것

```cpp
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattlePlayerController : public APlayerController
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;

private:
    void HandleSelect();
    void HandleRemove();
    [[nodiscard]] TOptional<FIntPoint> CellUnderCursor() const;
    [[nodiscard]] ASH_BattleBoard* FindBoard() const;

    UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputMappingContext> BattleMapping;
    UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> SelectAction;
    UPROPERTY(EditDefaultsOnly, Category = "Input") TObjectPtr<UInputAction> RemoveAction;
    UPROPERTY(EditDefaultsOnly, Category = "UI")    TSubclassOf<UBattleHUDWidget> HUDClass;

    TWeakObjectPtr<ASH_BattleBoard> Board;
};
```

### 코드 조각

```cpp
void ASH_BattlePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
    {
        // 멤버 함수 포인터: &ThisClass::HandleSelect 는 "이 클래스의 HandleSelect" 라는 값이다.
        // this 와 함께 저장되었다가 클릭 때 this->HandleSelect() 로 불린다.
        EnhancedInput->BindAction(SelectAction, ETriggerEvent::Started, this, &ThisClass::HandleSelect);
        EnhancedInput->BindAction(RemoveAction, ETriggerEvent::Started, this, &ThisClass::HandleRemove);
    }
}

TOptional<FIntPoint> ASH_BattlePlayerController::CellUnderCursor() const
{
    FHitResult Hit;
    if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), /*bTraceComplex=*/false, Hit))
    {
        return {};
    }
    const ASH_BattleBoard* BoardPtr = Board.Get();
    return BoardPtr ? BoardPtr->WorldToCell(Hit.ImpactPoint) : TOptional<FIntPoint>{};
}
```

`BeginPlay` 에서 할 일(직접 쓴다):
1. `bShowMouseCursor = true;`, `SetInputMode(FInputModeGameAndUI{});`
2. `ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer())` 로 `BattleMapping` 을 추가한다.
3. `TActorIterator<ACameraActor>` 로 태그가 `BattleCamera` 인 카메라를 찾아 `SetViewTarget`.
4. `TActorIterator<ASH_BattleBoard>` 로 보드를 찾아 `Board` 에 저장한다.
5. `CreateWidget<UBattleHUDWidget>(this, HUDClass)` → `AddToViewport()`.

### C++ 개념

- **캐스팅 최소화 (EC 27)**
  - `Cast<UEnhancedInputComponent>(InputComponent)` 는 실패하면 `nullptr` 를 돌려주는 안전한 언리얼 캐스트다(UObject 전용, RTTI 대신 리플렉션 사용).
  - 타입이 확실해도 `static_cast` 를 UObject 에 쓰지 않는다. 틀렸을 때 잘못된 메모리를 읽고도 조용히 진행된다.
  - 캐스트가 여러 곳에 흩어지면 설계 신호다. 보드 포인터를 `BeginPlay` 에서 한 번 찾아 저장하는 이유다.
- **람다 기본 캡처 금지 (EMC 31)**
  - 입력을 람다로 바인딩하고 싶을 때 `[=]` 나 `[&]` 를 쓰면 `this` 가 암시적으로 캡처된다. 컨트롤러가 파괴된 뒤 람다가 불리면 크래시한다.
  - 언리얼에서는 `BindAction` 에 멤버 함수를 넘기거나, 델리게이트의 `AddWeakLambda(this, …)` 를 써서 객체가 살아 있을 때만 불리게 한다.
- **`auto` (EMC 5)**: `auto* Subsystem = ULocalPlayer::GetSubsystem<…>(…);` 처럼 오른쪽에 타입이 이미 드러나면 `auto` 가 중복을 줄인다. 포인터임을 보이려고 `auto*` 로 쓴다.

### 언리얼 함정

- `GetHitResultUnderCursorByChannel` 은 **충돌이 있는 표면**만 맞힌다. 아레나 바닥 메시의 Visibility 채널 응답이 Block 인지 확인한다. 바닥을 못 맞히면 칸이 영영 안 잡힌다.
- 유닛 캡슐이 Visibility 를 막으면 유닛 위를 클릭했을 때 커서가 바닥 대신 캡슐에 맞는다. 맞은 점의 XY 로 칸을 계산하므로 같은 칸이 나와서 대체로 문제없다. 그래도 이것을 알고 있어야 한다.
- GameMode 의 `DefaultPawnClass` 를 비우면 폰 없이 컨트롤러만 있다. 이때 입력은 컨트롤러가 받으므로 위 구조로 동작한다.

### 완료 확인

빈 아군 칸 클릭 → 유닛 생김, 같은 칸 클릭 → 선택, 다른 칸 클릭 → 이동, 우클릭 → 제거. 막힌 칸과 동쪽 칸에는 놓이지 않는다. 10명이 되면 더 생기지 않는다.

---

## 9. 7단계 — 게임 흐름 `ASH_BattleGameMode`

### 목표

단계 전이를 **한 곳에서만** 일으키고, 바뀔 때마다 UI 에 알린다.

### 만들 것

```cpp
// Battle/SH_BattleTypes.h
#pragma once
#include "CoreMinimal.h"

enum class ESHBattlePhase : uint8 { Placement, Battle, Result };

/** 허용되는 전이. constexpr 이라 static_assert 로 컴파일 타임 검사를 할 수 있다. */
[[nodiscard]] constexpr bool IsAllowedTransition(const ESHBattlePhase From, const ESHBattlePhase To) noexcept
{
    switch (From)
    {
    case ESHBattlePhase::Placement: return To == ESHBattlePhase::Battle;
    case ESHBattlePhase::Battle:    return To == ESHBattlePhase::Result;
    case ESHBattlePhase::Result:    return To == ESHBattlePhase::Placement;
    }
    return false;
}
static_assert(IsAllowedTransition(ESHBattlePhase::Placement, ESHBattlePhase::Battle));
static_assert(!IsAllowedTransition(ESHBattlePhase::Placement, ESHBattlePhase::Result));
```

```cpp
DECLARE_MULTICAST_DELEGATE_OneParam(FSHOnPhaseChanged, ESHBattlePhase);

UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattleGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ASH_BattleGameMode();       // DefaultPawnClass = nullptr; PlayerControllerClass = 6단계 컨트롤러

    /** UI 의 시작 버튼. 조건이 안 되면 아무 일도 하지 않는다. */
    void RequestStartBattle();
    void RequestReturnToPlacement();

    [[nodiscard]] ESHBattlePhase GetPhase() const { return Phase; }
    [[nodiscard]] ASH_BattleBoard* GetBoard() const { return Board.Get(); }

    FSHOnPhaseChanged OnPhaseChanged;

protected:
    virtual void BeginPlay() override;

private:
    void SetPhase(ESHBattlePhase NewPhase);
    void HandleBattleEnded(ESHRoundEnd Result);

    UPROPERTY(EditDefaultsOnly, Category = "Battle")
    TSubclassOf<ASH_BattleBrain> BrainClass;

    TWeakObjectPtr<ASH_BattleBoard> Board;

    UPROPERTY()
    TObjectPtr<ASH_BattleBrain> Brain;

    ESHBattlePhase Phase = ESHBattlePhase::Placement;
    ESHRoundEnd LastResult = ESHRoundEnd::None;
};
```

### 코드 조각

```cpp
void ASH_BattleGameMode::SetPhase(const ESHBattlePhase NewPhase)
{
    // 전이 규칙을 어기는 호출은 프로그래밍 실수다 — 개발 빌드에서 바로 멈춰 알린다.
    check(IsAllowedTransition(Phase, NewPhase));
    Phase = NewPhase;
    OnPhaseChanged.Broadcast(Phase);
}

void ASH_BattleGameMode::RequestStartBattle()
{
    ASH_BattleBoard* BoardPtr = Board.Get();
    if (Phase != ESHBattlePhase::Placement || !BoardPtr || !BoardPtr->IsPlacementComplete())
    {
        return;
    }
    // 1) Brain 스폰 → 2) Assemble(BoardPtr->GetAllUnits(), 아군 정의의 정책 폴더) → 3) BoardPtr->StartBattle()
    // → 4) Brain->StartThinking() → 5) SetPhase(Battle).  ★ 3 이 4 보다 먼저: 리셋·배치가 끝난 상태로 첫 관측을 모은다.
}
```

`BeginPlay` 에서는 보드를 찾아 저장하고 `Board->OnBattleEnded.AddUObject(this, &ThisClass::HandleBattleEnded)` 로 구독한다. `HandleBattleEnded` 는 Brain 을 멈추고 결과를 저장한 뒤 `SetPhase(Result)`.
`RequestReturnToPlacement` 는 Brain 을 `Destroy()` 하고, `Board->ReturnToPlacement()` 를 부른 뒤 `SetPhase(Placement)`.

### C++ 개념

- **상태 전이를 한 함수로 (EC 18)**
  - `Phase = …` 를 여러 곳에서 직접 쓰면 "알림 누락" 같은 실수가 생긴다.
  - `SetPhase` 만 쓰게 하면 규칙 검사와 알림이 항상 같이 일어난다. `Phase` 가 `private` 인 이유다(EC 22).
- **`constexpr` 함수 + `static_assert` (EMC 15)**: 전이 표를 테스트 실행 없이 **컴파일러가** 검사한다. 규칙을 바꾸면 `static_assert` 도 같이 바꿔야 해서, 규칙의 의도가 코드에 남는다.
- **관찰자 — 네이티브 멀티캐스트 델리게이트**: GameMode 는 누가 듣는지 모른다. HUD 가 늘어나도 GameMode 코드는 그대로다.
  - `AddUObject(this, &Func)`: 객체가 파괴되면 자동으로 호출되지 않는다.
  - 반환되는 `FDelegateHandle` 로 직접 해제할 수도 있다(8단계).
- **`const&` 로 넘기기 (EC 20)**: `TArray<ASH_RLCharacter*>` 를 `Assemble` 에 값으로 넘기면 배열 전체가 복사된다. 읽기만 하므로 `const TArray<…>&`.
- **`MoveTemp` / `Forward` (EMC 23, 25)**: `MoveTemp` 는 `std::move` 와 같다. 이동 자체를 하지 않고 "이동해도 된다"는 캐스트만 한다.
  - `GetAllUnits()` 가 지역 배열을 값으로 반환할 때는 `MoveTemp` 를 쓰지 않는다. 반환값 최적화(RVO)가 막힐 수 있다.
  - 배열을 멤버에 **넘겨 저장할** 때 `MoveTemp` 를 쓴다.
- **`Emplace` vs `Add` (EMC 42)**: `TArray::Emplace(Args…)` 는 요소를 배열 안에서 바로 만든다. `Add(T(...))` 는 임시 객체를 만든 뒤 이동한다. 포인터나 정수 배열에서는 차이가 없다. 구조체 배열에서 의미가 있다.

### 완료 확인

- 10명 배치 전에는 `RequestStartBattle` 이 아무 일도 하지 않는다.
- 시작 → 전투 → 결과 → 다시 배치 → 시작이 반복된다.
- 다시 배치 후 유닛이 각자의 칸에 HP 가득 찬 상태로 서 있다.
- 잘못된 전이를 일부러 부르면 `check` 에 걸려 멈춘다(개발 빌드).

---

## 10. 8단계 — UI `UBattleHUDWidget`

### 목표

레이아웃은 위젯 블루프린트(WBP_BattleHUD)에서 그리고, 동작은 C++ 에 둔다.

### 만들 것

```cpp
// Battle/BattleHUDWidget.h
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Battle/SH_BattleTypes.h"
#include "BattleHUDWidget.generated.h"

class UButton;          // ★ 전방 선언 — 헤더가 UMG 위젯 헤더 전부를 끌어오지 않게 한다(EC 31)
class UTextBlock;
class ASH_BattleGameMode;

UCLASS(Abstract)
class HAI_PROJECT_TEAM5_API UBattleHUDWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    UFUNCTION() void HandleStartClicked();       // 버튼의 OnClicked 는 동적 델리게이트 → UFUNCTION 필수
    UFUNCTION() void HandleReplayClicked();
    void HandlePhaseChanged(ESHBattlePhase NewPhase);
    void HandlePlacementChanged(int32 Placed, int32 Required);

    // 이름이 위젯 블루프린트의 위젯 이름과 같아야 한다. 없으면 블루프린트 컴파일이 실패한다(BindWidget).
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> StartButton;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ReplayButton;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PlacedText;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ResultText;

    TWeakObjectPtr<ASH_BattleGameMode> GameMode;
    FDelegateHandle PhaseHandle;
    FDelegateHandle PlacementHandle;
};
```

### 코드 조각

```cpp
void UBattleHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();

    StartButton->OnClicked.AddDynamic(this, &ThisClass::HandleStartClicked);
    ReplayButton->OnClicked.AddDynamic(this, &ThisClass::HandleReplayClicked);

    if (ASH_BattleGameMode* Mode = GetWorld()->GetAuthGameMode<ASH_BattleGameMode>())
    {
        GameMode = Mode;
        PhaseHandle = Mode->OnPhaseChanged.AddUObject(this, &ThisClass::HandlePhaseChanged);
        // 배치 수는 보드가 방송한다: Mode->GetBoard()->OnPlacementChanged 에 같은 방식으로 구독
        HandlePhaseChanged(Mode->GetPhase());       // 현재 상태로 한 번 그려 둔다
    }
}

void UBattleHUDWidget::NativeDestruct()
{
    if (ASH_BattleGameMode* Mode = GameMode.Get())
    {
        Mode->OnPhaseChanged.Remove(PhaseHandle);
    }
    Super::NativeDestruct();
}
```

`HandlePhaseChanged` 에서는 단계에 따라 버튼을 보이거나 숨긴다(`SetVisibility`). `HandlePlacementChanged` 에서는 `PlacedText->SetText(FText::Format(…))` 로 `배치 7 / 10` 을 쓰고, `StartButton->SetIsEnabled(Placed == Required)`.
결과 문구는 GameMode 의 `LastResult` 를 보고 정한다. `LastResult` 는 `private` 이니 GameMode 에 `[[nodiscard]] ESHRoundEnd GetLastResult() const` 를 추가한다.
- 팀 0 승리 = 승리
- 팀 1 승리 = 패배
- 시간 초과·동시 전멸 = 양쪽 패배(학습과 같은 규칙)

### C++ 개념

- **동적 델리게이트 vs 네이티브 델리게이트**

  | | 동적 (`DECLARE_DYNAMIC_…`) | 네이티브 (`DECLARE_MULTICAST_DELEGATE_…`) |
  |---|---|---|
  | 예 | `UButton::OnClicked` | 우리가 만든 `FSHOnPhaseChanged` |
  | 블루프린트에서 바인딩 | 가능 | 불가 |
  | 바인딩 대상 | `UFUNCTION` 만 (이름으로 찾음) | 아무 멤버 함수, 람다 |
  | 비용 | 이름 조회, 느림 | 함수 포인터, 빠름 |
  | C++ 에서 바인딩 | `AddDynamic` | `AddUObject`, `AddWeakLambda`, `AddRaw` |

  - `AddRaw` 는 UObject 가 아닌 객체용이고, 수명을 확인하지 않는다. 객체가 먼저 파괴되면 크래시한다.
- **헤더 전방 선언 (EC 31)**: `UButton*` 은 포인터라서 크기만 알면 된다. 헤더에서 `#include "Components/Button.h"` 대신 `class UButton;` 을 쓰고, `.cpp` 에서 포함한다. 이 헤더를 포함하는 다른 파일이 UMG 헤더 변경 때마다 다시 컴파일되지 않는다.
- **`UCLASS(Abstract)`**: C++ 클래스 자체로는 위젯을 만들 수 없게 한다. 반드시 레이아웃이 있는 WBP 를 쓰도록 강제한다(EC 18).

### 언리얼 함정

- `BindWidget` 이름이 WBP 의 위젯 이름과 한 글자라도 다르면 **WBP 컴파일 에러**가 난다. 이 에러가 런타임 `nullptr` 대신 컴파일 타임에 잡아 주는 장치다.
- `NativeConstruct` 는 위젯이 화면에 붙을 때마다 불릴 수 있다. 바인딩을 두 번 하지 않으려면 `NativeDestruct` 에서 해제하거나, `NativeOnInitialized`(한 번만 불림)에서 바인딩한다.
- `AddDynamic` 대상 함수에 `UFUNCTION()` 이 없으면 컴파일은 되지만 **런타임에 바인딩이 조용히 실패**할 수 있다.

### 완료 확인

배치 수가 실시간으로 바뀌고, 10명이 되면 시작 버튼이 켜진다. 전투 중에는 버튼이 숨고, 끝나면 결과와 다시 배치 버튼이 보인다.

---

## 11. 9단계 — 마무리와 확장

### 마무리 목록

- 배치 단계에서 칸 강조: 마우스 아래 칸, 선택된 칸, 놓을 수 없는 칸. 바닥에 칸 크기의 반투명 평면을 깔고 머티리얼 파라미터로 색을 바꾼다. 처음에는 `DrawDebugBox` 로도 충분하다.
- 결과 화면에 생존 수: `Board->GetAliveCount(0/1)`.
- 기본 맵을 `Lvl_Battle` 로: **프로젝트 설정 → Maps & Modes**. 학습 레벨은 각자 GameMode Override 를 쓰므로 영향이 없다.

### 유닛 종류 추가 (확장)

종류마다 관측·행동이 같고 정책만 다르다면, **유닛 정의마다 Brain 을 하나씩** 둔다.
- GameMode 는 유닛을 정의별로 묶는다(`TMap<const USH_UnitDefinition*, TArray<ASH_RLCharacter*>>`).
- 그리고 묶음마다 Brain 을 조립한다.

이 구조가 **전략 패턴(EC 35)** 이다. "어떻게 행동할지"(정책)를 유닛 클래스에 넣지 않고, 바꿔 끼울 수 있는 객체(Brain + 스냅샷)로 둔다.

★ 학습된 정책이 1종뿐이라, 새 종류는 **새로 학습**해야 한다. 아니면 스크립트 봇 컨트롤러로 대신한다.
관측 스키마를 바꾸면(예: 근접 유닛) Interactor 도 새로 만들어야 하고, 그러면 기존 정책과 함께 쓸 수 없다.

### 패키징할 때

- `Snapshots/` 폴더는 패키지에 들어가지 않는다. 정책 가중치를 **신경망 에셋**(`ULearningAgentsNeuralNetwork`, 콘텐츠 브라우저의 에셋)으로 저장해 콘텐츠로 넣는다.
- 방법: 에디터에서 스냅샷을 에셋에 `LoadNetworkFromSnapshot` 으로 한 번 불러 저장한다. 그리고 `MakePolicy` 의 Encoder/Policy/Decoder 인자에 그 에셋을 넘기고 `bReinit…` 를 `false` 로 둔다.
- `LearningAgentsTraining` 모듈(아레나의 부모 `ALearningAgentsGymSimple` 이 여기 있다)은 런타임 모듈이라 출고 빌드에 들어간다(Build.cs 주석).

---

## 12. 부록

### A. 표준 C++ ↔ 언리얼 대응표

| 표준 | 언리얼 | 메모 |
|---|---|---|
| `std::vector<T>` | `TArray<T>` | `Num()`, `Add`, `Emplace`, `RemoveAt` |
| `std::unordered_map<K,V>` | `TMap<K,V>` | 키에 `GetTypeHash` 필요 |
| `std::optional<T>` | `TOptional<T>` | `IsSet()`, `GetValue()`, `Get(기본값)` |
| `std::variant<…>` | `TVariant<…>` | |
| `std::function<F>` | `TFunction<F>` | 소유하는 함수 객체(할당 가능) |
| `std::function_ref`(C++26) | `TFunctionRef<F>` | 소유하지 않음, 호출 동안만 유효 |
| `std::unique_ptr` / `make_unique` | `TUniquePtr` / `MakeUnique` | UObject 에 쓰지 않는다 |
| `std::shared_ptr` / `weak_ptr` | `TSharedPtr` / `TWeakPtr` | UObject 에 쓰지 않는다 |
| (UObject 참조) | `TObjectPtr` + `UPROPERTY`, `TWeakObjectPtr` | GC 가 수명 관리 |
| `std::move` / `std::forward` | `MoveTemp` / `Forward` | |
| 예외 `throw` | 없음 | `check`(개발 빌드에서 멈춤), `ensure`(로그 후 계속), 반환값으로 실패 표현 |
| `std::string` | `FString` (+ `FName`, `FText`) | `FText` 는 화면에 보이는 글자(현지화) |
| `dynamic_cast` | `Cast<T>` | UObject 전용, 리플렉션 기반 |

### B. 언리얼에서 그대로 쓰기 어려운 Modern C++ 조언과 이유

- **EMC 18~21 스마트 포인터**: UObject 는 GC 가 관리하므로 쓰지 않는다. UObject 가 아닌 객체(1·2단계의 값 타입 등)에만 쓴다.
- **EMC 14 `noexcept`**: 예외가 꺼져 있어서 최적화 효과가 거의 없다. 문서 역할로만 쓴다.
- **EC 13 RAII**: UObject 에는 결정적인 소멸 시점이 없다(GC 가 정한다). 그래서 "소멸자에서 정리"를 기대하지 말고 `EndPlay` / `NativeDestruct` 에서 명시적으로 정리한다.
- **표준 라이브러리 컨테이너**: 쓸 수는 있지만 리플렉션(`UPROPERTY`)과 직렬화를 지원하지 않는다. 엔진 API 도 언리얼 컨테이너를 받는다. 그래서 언리얼 컨테이너를 기본으로 쓴다.

### C. 자주 나는 함정 (이 프로젝트에서 실제로 겪은 것 포함)

1. `UPROPERTY` 없는 UObject 포인터 → 어느 순간 GC 에 지워짐, 무작위 크래시.
2. 생성자에서 월드 · 다른 액터 접근 → CDO 생성 때 `nullptr`.
3. `override` 빠짐 → 오버라이드가 아니라 새 함수, 에러 없이 무시됨.
4. 지역 변수가 부모 멤버 이름과 같음(`Controller`, `Mesh` 등) → C4458, 빌드 실패.
5. `AddDynamic` 대상에 `UFUNCTION` 없음 → 바인딩 조용히 실패.
6. 학습용 BP 를 그대로 씀 → `bDisableAnimation` 때문에 T-포즈.
7. 유닛에 `Execute_InitializeEntity` 를 안 부름 → 보드 소속이 아님, 관측의 아군·적 목록이 비고 라운드가 안 끝남.
8. 정책 조립 순서 틀림(`SetMaxAgentNum` 을 나중에) → 에이전트가 정책에 보이지 않음.
9. 헤더 바꾸고 Live Coding → 반영 안 됨. 에디터 닫고 전체 빌드.

### D. 진행 체크리스트

- [ ] 0단계: `Lvl_Battle`, `BP_GameUnit_Team0/1`(RL 컨트롤러, 학습용 최적화 끔), `BattleCamera`
- [ ] 1단계: `FSHBoardGrid` + 자동화 테스트 초록 + `static_assert`
- [ ] 2단계: `FSHPlacement` + 규칙 테스트
- [ ] 3단계: `DA_Unit_Shooter`, `DA_EnemyLayout_01`
- [ ] 4단계: 아레나 훅(학습 레벨 3분 회귀 확인) + `BP_BattleBoard`(엄폐물 동일) + 적 스폰 + 임시 시작
- [ ] 5단계: `SHSnapshot::ResolveFile` 공용화 + `ASH_BattleBrain` + 두 팀이 실제로 싸움
- [ ] 6단계: 클릭으로 배치·이동·제거, 카메라 고정
- [ ] 7단계: 단계 상태 기계 + 다시 배치 반복
- [ ] 8단계: HUD (배치 수, 시작 버튼, 결과)
- [ ] 9단계: 칸 강조, 기본 맵 설정

### E. 이 가이드에서 다룬 항목

| 항목 | 다룬 단계 |
|---|---|
| EC 2 `#define` 대신 `const`·`enum`·`inline` | 1 |
| EC 3 `const` 를 쓸 수 있으면 쓴다 | 1, 2 |
| EC 4 사용 전 초기화 (멤버 초기화 순서) | 1, 4 |
| EC 9 생성자·소멸자에서 가상 함수 금지 | 4 |
| EC 13 자원 관리는 객체에 | 5, 부록 B |
| EC 18 쓰기 쉽고 틀리기 어려운 인터페이스 | 1, 2, 7, 8 |
| EC 20 `const&` 전달 | 7 |
| EC 21 객체를 반환해야 할 때 참조를 반환하지 않는다 | 3, 7 |
| EC 22 데이터 멤버는 private | 2, 7 |
| EC 23 비멤버 비friend 함수 | 1, 5 |
| EC 27 캐스팅 최소화 | 6 |
| EC 28 내부 핸들 반환 금지 | 2, 3 |
| EC 30 인라인의 득실 | 2 (연습) |
| EC 31 파일 간 컴파일 의존성 최소화 | 5, 8 |
| EC 32 public 상속은 is-a | 3 |
| EC 33 상속된 이름 가리지 않기 | 1, 4 |
| EC 35 가상 함수의 대안 (NVI, 전략) | 4, 9 |
| EC 36 비가상 함수 재정의 금지 | 4 |
| EC 38 합성으로 has-a | 3 |
| EMC 5 `auto` | 6 |
| EMC 7 `()` 와 `{}` 구분 | 1 |
| EMC 10 범위 있는 열거형 | 2, 7 |
| EMC 12 `override` | 4 |
| EMC 14 `noexcept` | 1, 부록 B |
| EMC 15 `constexpr` | 1, 7 |
| EMC 17 특수 멤버 함수 생성 규칙 | 1 |
| EMC 18 / 20 / 21 스마트 포인터 | 2, 5 |
| EMC 23 / 25 `move` · `forward` | 7 |
| EMC 31 람다 기본 캡처 금지 | 6 |
| EMC 42 `emplace` | 7 |
