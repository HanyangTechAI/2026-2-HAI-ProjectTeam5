// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/SH_BoardGrid.h"
#include "Battle/SH_Placement.h"
#include "RL/SH_Arena.h"
#include "SH_BattleBoard.generated.h"

class AGun;
class ASH_BulletTracer;
class ASH_RLCharacter;
struct FGunFireResult;
class USH_EnemyLayout;
class USH_UnitDefinition;

DECLARE_MULTICAST_DELEGATE_OneParam(FSHOnBattleEnded, ESHRoundEnd /*Result*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FSHOnPlacementChanged, int32 /*Placed*/, int32 /*Required*/);

/**
 *  플레이어가 하는 게임의 보드. 아레나를 상속해서 형상 · 라운드 규칙 · 축소 구역을 그대로 쓴다.
 *
 *  ★ 왜 ASH_Arena 인가. 학습된 정책의 관측이 유닛의 CachedGym 을 ASH_Arena 로 캐스팅해
 *  아군·적 목록, 라운드 경과, 구역 반경을 읽는다. 다른 클래스면 이 값이 전부 0 이 되어
 *  정책이 학습 때 본 적 없는 입력을 받는다.
 *
 *  ★ GymsManager 를 쓰지 않는다. 엔티티 풀은 Gym 이 Initialize 될 때만 유닛을 스폰하므로
 *  이 보드에서는 돌지 않고, 보드가 유닛을 직접 스폰한다.
 *
 *  ★ 배치 단계에서 라운드 판정이 돌지 않는 이유. 아레나 Tick 은 팀 유닛 캐시가 비었거나(첫 전투 전)
 *  이미 끝난 라운드면(다시 배치 중) 아무것도 하지 않는다. 그래서 StartBattle 밖에서 ResetRound 를
 *  부르면 배치 중에 45초 시간 초과가 난다.
 *
 *  ★ 보드 액터를 회전시켜 놓지 않는다. 아레나 중심과 격자 변환이 회전 0 을 가정한다.
 *
 *  ★ 여러 아레나 중에서 고르기. 레벨에 보드를 여러 개(같은 위치여도 된다) 놓고 하나만 bActiveBoard 를 켠다.
 *  꺼진 보드는 에디터 · 게임 양쪽에서 숨고 콜리전이 꺼지며, 유닛을 만들지도 Tick 하지도 않는다.
 *  게임모드는 켜진 보드를 쓴다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattleBoard : public ASH_Arena
{
	GENERATED_BODY()

public:
	ASH_BattleBoard();

	/** 이 레벨에서 쓰는 보드인가. */
	[[nodiscard]] bool IsActiveBoard() const { return bActiveBoard; }

	//~ 배치 단계 -------------------------------------------------------------

	/** 왼쪽 클릭: 빈 칸이면 아군 추가, 아군 칸이면 선택, 선택 중이면 이동. */
	void HandleCellClicked(FIntPoint Cell);

	/** 오른쪽 클릭: 그 칸의 아군을 뺀다. */
	void HandleCellRightClicked(FIntPoint Cell);

	/** 마우스 아래 칸. 칸 그리기에만 쓴다. */
	void SetHoveredCell(const TOptional<FIntPoint>& Cell) { HoveredCell = Cell; }

	[[nodiscard]] TOptional<FIntPoint> WorldToCell(const FVector& World) const;
	[[nodiscard]] bool IsAllyCell(FIntPoint Cell) const;
	[[nodiscard]] bool IsBlocked(FIntPoint Cell) const;

	[[nodiscard]] int32 GetPlacedCount() const { return AllyPlacement.Num(); }
	[[nodiscard]] int32 GetRequiredCount() const { return AllyPlacement.GetMaxUnits(); }
	[[nodiscard]] bool IsPlacementComplete() const { return AllyPlacement.IsFull(); }

	//~ 전투 ------------------------------------------------------------------

	/** 배치된 유닛으로 라운드를 시작한다(ResetRound). 배치가 안 끝났으면 false. */
	[[nodiscard]] bool StartBattle();

	/** 결과 화면 뒤 — 유닛을 되살려 각자의 칸으로 돌려놓는다. 배치는 유지된다. */
	void ReturnToPlacement();

	/** 정책이 조종할 유닛 전부(양 팀). */
	[[nodiscard]] TArray<ASH_RLCharacter*> GetAllUnits() const;

	[[nodiscard]] const USH_UnitDefinition* GetAllyDefinition() const;

	//~ 관전 도구 -------------------------------------------------------------

	/** 모든 유닛의 시야 레이(부채꼴 16개 + 보이는 적 선)를 그릴지. 나중에 배치하는 유닛에도 적용된다.
	 *  ★ 디버그 선이라 Shipping 빌드에서는 그려지지 않는다. 레이는 전투 중(정책이 관측할 때)에만 나온다. */
	void SetVisionRaysVisible(bool bVisible);
	[[nodiscard]] bool AreVisionRaysVisible() const { return bShowVisionRays; }

	/** 모든 유닛 총의 사격 판정 선(AGun::bDrawDebugTrace — 쏠 때마다 원점 → 맞은 곳)을 그릴지.
	 *  나중에 배치하는 유닛에도 적용된다. ★ 디버그 선이라 Shipping 빌드에서는 그려지지 않는다. */
	void SetAimRaysVisible(bool bVisible);
	[[nodiscard]] bool AreAimRaysVisible() const { return bShowAimRays; }

	/** 전투가 끝나면 한 번 방송한다. */
	FSHOnBattleEnded OnBattleEnded;

	/** 아군 수가 바뀌면 방송한다. */
	FSHOnPlacementChanged OnPlacementChanged;

	virtual void Tick(float DeltaSeconds) override;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual bool GetSpawnTransformForUnit(const ABase_Character* Unit, int32 Team, FTransform& OutTransform) const override;

	/** 이 보드는 아레나의 원형 스폰 슬롯을 쓰지 않는다(칸에 배치한다). 슬롯이 엄폐물에 걸려도 상관없다. */
	virtual void ValidateSpawnSlotsClear() const override {}

private:
	[[nodiscard]] ASH_RLCharacter* SpawnUnit(const USH_UnitDefinition& Definition, int32 Team, FIntPoint Cell);
	[[nodiscard]] FTransform CellTransform(FIntPoint Cell, int32 Team) const;
	void SpawnEnemies();
	void RebuildBlockedCells();

	/** 유닛을 칸으로 순간이동시키고 조준을 맞춘다. */
	void PlaceUnitAt(ASH_RLCharacter& Unit, FIntPoint Cell, int32 Team) const;

	/** 총은 별도 액터라 유닛과 같이 파괴되지 않는다. 둘 다 없앤다. */
	static void DestroyUnit(ASH_RLCharacter& Unit);

	void BroadcastPlacement() const;
	void DrawPlacementGrid() const;

	/** 총이 사격을 처리할 때마다 불린다(AGun::OnShotResolved). 날아가는 총알 그림을 만든다. */
	void HandleShotResolved(const AGun& Gun, const FGunFireResult& Result);

	/** 날아가는 총알 그림. 비우면 총알을 그리지 않는다(판정에는 영향 없음). */
	UPROPERTY(EditAnywhere, Category = "SH|Battle|View")
	TSubclassOf<ASH_BulletTracer> TracerClass;

	/** 총알 그림의 속도(cm/s). 15,000 이면 1배속에서 3,000 cm 를 0.2초, 0.1배속에서 2초에 난다. */
	UPROPERTY(EditAnywhere, Category = "SH|Battle|View", meta = (ClampMin = "100.0"))
	float TracerSpeed = 15000.0f;

	/** 총알 그림이 나오는 총 메시 소켓. 없으면 판정 트레이스의 시작점(발밑 + 60 cm)에서 나온다. */
	UPROPERTY(EditAnywhere, Category = "SH|Battle|View")
	FName MuzzleSocketName = TEXT("MuzzleFlashSocket");

	bool bShowVisionRays = false;
	/** 기본 켜짐 — BP_Gun 의 bDrawDebugTrace 기본값(true)과 같게 두어 지금까지 보이던 화면을 유지한다. */
	bool bShowAimRays = true;

	UPROPERTY(EditAnywhere, Category = "SH|Battle")
	TObjectPtr<USH_UnitDefinition> AllyDefinition;

	UPROPERTY(EditAnywhere, Category = "SH|Battle")
	TObjectPtr<USH_EnemyLayout> EnemyLayout;

	/** 끄면 이 보드를 쓰지 않는다. 레벨에서 켜진 보드는 하나여야 한다. */
	UPROPERTY(EditAnywhere, Category = "SH|Battle")
	bool bActiveBoard = true;

	/**
	 *  아군 구역의 동쪽 경계(보드 기준 X, cm). 칸 중심이 이 값보다 서쪽이면 아군 칸이다.
	 *  ★ 열 수가 아니라 거리로 정한 이유: 격자 크기가 ArenaRadius 를 따라 바뀐다.
	 *  −250 이면 14 x 14 격자에서 서쪽 6열, 20 x 20 격자에서 서쪽 9열이다(둘 다 가운데 선 바로 서쪽 한 열은 비운다).
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Battle", meta = (ClampMax = "0.0"))
	float AllyZoneMaxX = -250.0f;

	/** 배치 단계에 칸을 그린다. 디버그 선이라 출고 빌드에서는 보이지 않는다 — 나중에 머티리얼로 바꾼다. */
	UPROPERTY(EditAnywhere, Category = "SH|Battle")
	bool bDrawPlacementGrid = true;

	FSHBoardGrid Grid;
	FSHPlacement AllyPlacement{ 10 };
	FSHPlacement EnemyPlacement{ 10 };
	TBitArray<> BlockedCells;

	TOptional<FIntPoint> SelectedCell;
	TOptional<FIntPoint> HoveredCell;

	bool bPlacementOpen = true;
	bool bBattleRunning = false;
};
