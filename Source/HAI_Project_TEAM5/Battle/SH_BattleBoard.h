// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/SH_BoardGrid.h"
#include "Battle/SH_Placement.h"
#include "RL/SH_Arena.h"
#include "SH_BattleBoard.generated.h"

class ASH_RLCharacter;
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
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattleBoard : public ASH_Arena
{
	GENERATED_BODY()

public:
	ASH_BattleBoard();

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

	/** 전투가 끝나면 한 번 방송한다. */
	FSHOnBattleEnded OnBattleEnded;

	/** 아군 수가 바뀌면 방송한다. */
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

	/** 유닛을 칸으로 순간이동시키고 조준을 맞춘다. */
	void PlaceUnitAt(ASH_RLCharacter& Unit, FIntPoint Cell, int32 Team) const;

	/** 총은 별도 액터라 유닛과 같이 파괴되지 않는다. 둘 다 없앤다. */
	static void DestroyUnit(ASH_RLCharacter& Unit);

	void BroadcastPlacement() const;
	void DrawPlacementGrid() const;

	UPROPERTY(EditAnywhere, Category = "SH|Battle")
	TObjectPtr<USH_UnitDefinition> AllyDefinition;

	UPROPERTY(EditAnywhere, Category = "SH|Battle")
	TObjectPtr<USH_EnemyLayout> EnemyLayout;

	/** 아군이 쓸 수 있는 열 수(X 방향, 서쪽부터). 6이면 보드 기준 x < −250 cm. */
	UPROPERTY(EditAnywhere, Category = "SH|Battle", meta = (ClampMin = "1"))
	int32 AllyColumns = 6;

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
