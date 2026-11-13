// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BattleBoard.h"

#include "Battle/SH_BulletTracer.h"
#include "Battle/SH_EnemyLayout.h"
#include "Battle/SH_UnitDefinition.h"
#include "Combat/CombatStatsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "RL/SH_VisionComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Gun.h"
#include "LearningAgentsEntityInterface.h"
#include "RL/SH_RLCharacter.h"

ASH_BattleBoard::ASH_BattleBoard()
{
	// 관측의 생존 비율 분모와 ValidateSpawnSlotsClear 가 이 값을 쓴다. 학습과 같은 10 대 10.
	UnitsPerTeam = 10;

	// 아레나의 60초짜리 스폰 슬롯 그림은 학습 확인용이다. 배치 칸과 헷갈린다.
	bDrawDebugOnBeginPlay = false;

	// ★ 생성자에서 월드·다른 액터를 건드리지 않는다. 이 생성자는 CDO 를 만들 때도 돈다 — 월드가 없다.
}

void ASH_BattleBoard::OnConstruction(const FTransform& Transform)
{
	// ★ 켤 때 다시 보이게 하는 것을 Super 보다 먼저 한다. 아레나의 RebuildArenaGeometry 가
	//   bAutoBuildBoundary=false 이면 C++ 벽을 숨기는데, 그 결정을 덮어쓰지 않기 위해서다.
	//   (C++ 컴포넌트는 생성 스크립트를 다시 돌려도 새로 만들어지지 않아 이전 숨김 상태가 남는다.)
	if (bActiveBoard && RootComponent)
	{
		RootComponent->SetVisibility(true, /*bPropagateToChildren=*/true);
	}

	Super::OnConstruction(Transform);

	// 꺼진 보드는 에디터 뷰포트에서도 숨긴다. 같은 위치에 보드 두 개를 겹쳐 놓아도 하나만 보인다.
	// 콜리전도 끈다 — 켜진 보드의 막힌 칸 검사와 유닛 이동이 꺼진 보드의 엄폐물에 걸리지 않게.
	if (!bActiveBoard && RootComponent)
	{
		RootComponent->SetVisibility(false, /*bPropagateToChildren=*/true);
	}
	SetActorEnableCollision(bActiveBoard);
}

void ASH_BattleBoard::BeginPlay()
{
	Super::BeginPlay();

	if (!bActiveBoard)
	{
		SetActorTickEnabled(false);   // 배치 칸도 그리지 않고 라운드 판정도 하지 않는다
		return;
	}

	// 격자는 아레나 크기를 따른다. 반경 1,750 → 14 x 14, 2,450 → 20 x 20.
	Grid = FSHBoardGrid::Covering(ArenaRadius);

	// ★ 순서: 막힌 칸을 유닛보다 먼저 계산한다. 유닛 캡슐도 WorldStatic 채널을 막아서
	//   나중에 계산하면 유닛이 선 칸이 "막힘" 이 된다.
	RebuildBlockedCells();
	SpawnEnemies();
	BroadcastPlacement();
}

//~ 칸 판정 -------------------------------------------------------------------

TOptional<FIntPoint> ASH_BattleBoard::WorldToCell(const FVector& World) const
{
	const FVector Center = GetActorLocation();
	return Grid.LocalToCell(FVector2D(World.X - Center.X, World.Y - Center.Y));
}

bool ASH_BattleBoard::IsAllyCell(const FIntPoint Cell) const
{
	return Grid.IsInside(Cell) && Grid.CellCenter(Cell).X < AllyZoneMaxX;
}

bool ASH_BattleBoard::IsBlocked(const FIntPoint Cell) const
{
	if (!Grid.IsInside(Cell))
	{
		return true;
	}
	const int32 Index = FSHBoardGrid::ToIndex(Cell.X, Cell.Y, Grid.Columns);
	return BlockedCells.IsValidIndex(Index) && BlockedCells[Index];
}

FTransform ASH_BattleBoard::CellTransform(const FIntPoint Cell, const int32 Team) const
{
	const FVector Center = GetActorLocation();
	const FVector2D Local = Grid.CellCenter(Cell);
	const FVector Location(Center.X + Local.X, Center.Y + Local.Y, Center.Z + SpawnZOffset);

	// 팀 0 은 서쪽에서 동쪽(+X)을, 팀 1 은 동쪽에서 서쪽을 본다.
	const FRotator Facing(0.0f, Team == 0 ? 0.0f : 180.0f, 0.0f);
	return FTransform(Facing, Location);
}

void ASH_BattleBoard::RebuildBlockedCells()
{
	BlockedCells.Init(false, Grid.Columns * Grid.Rows);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(42.0f, 96.0f);   // 유닛 캡슐 반경·반높이

	// ★ 이 보드 자신을 무시하면 안 된다. 엄폐물과 벽이 이 보드의 컴포넌트다.
	//   바닥은 캡슐이 4 cm 위에 떠 있어서(중심 100, 반높이 96) 걸리지 않는다.
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(SHBoardCell), /*bTraceComplex=*/false);

	const double SafeRadius = ArenaRadius - ClampMargin;

	for (int32 Y = 0; Y < Grid.Rows; ++Y)
	{
		for (int32 X = 0; X < Grid.Columns; ++X)
		{
			const FIntPoint Cell(X, Y);
			const bool bOutside = Grid.CellCenter(Cell).Size() > SafeRadius;
			const bool bHitsObstacle = !bOutside && World->OverlapAnyTestByChannel(
				CellTransform(Cell, 0).GetLocation(), FQuat::Identity, ECC_WorldStatic, Capsule, Query);
			BlockedCells[FSHBoardGrid::ToIndex(X, Y, Grid.Columns)] = bOutside || bHitsObstacle;
		}
	}
}

//~ 유닛 ----------------------------------------------------------------------

ASH_RLCharacter* ASH_BattleBoard::SpawnUnit(const USH_UnitDefinition& Definition, const int32 Team, const FIntPoint Cell)
{
	const TSubclassOf<ASH_RLCharacter> UnitClass = Definition.GetUnitClass(Team);
	UWorld* World = GetWorld();
	if (!UnitClass || !World)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleBoard] %s: 팀 %d 의 유닛 클래스가 비어 있다."), *Definition.GetName(), Team);
		return nullptr;
	}

	FActorSpawnParameters Params;
	// ★ 캡슐이 이웃과 살짝 겹쳐도 스폰한다. 기본값이면 스폰이 조용히 실패해 nullptr 이 온다.
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASH_RLCharacter* Unit = World->SpawnActor<ASH_RLCharacter>(UnitClass, CellTransform(Cell, Team), Params);
	if (!Unit)
	{
		return nullptr;
	}

	// ★ 이 한 줄이 유닛을 "이 보드 소속" 으로 만든다. ABase_Character::InitializeEntity_Implementation 이
	//   CachedGym = this 를 넣고, 엔진의 IsMemberOfGym 은 GetGym() == this 로 소속을 판정한다.
	//   빠뜨리면 CollectUnits 가 이 유닛을 무시해 관측의 아군·적 목록이 비고 라운드도 안 끝난다.
	ILearningAgentsEntityInterface::Execute_InitializeEntity(Unit, this);

	// BP 의 AutoPossessAI 가 맞으면 이미 컨트롤러가 있다. 없을 때만 만든다.
	if (!Unit->GetController())
	{
		Unit->SpawnDefaultController();
	}

	// 관전 도구. 총은 유닛 BeginPlay(SpawnActor 안)에서 이미 만들어졌다.
	// AddUObject 는 이 보드를 약하게 잡는다 — 보드가 먼저 사라져도 호출되지 않는다.
	if (AGun* UnitGun = Unit->gun)
	{
		UnitGun->OnShotResolved.AddUObject(this, &ThisClass::HandleShotResolved);
		UnitGun->bDrawDebugTrace = bShowAimRays;
	}
	if (Unit->Vision)
	{
		Unit->Vision->bDrawDebug = bShowVisionRays;
	}

	const int32 ActualTeam = Unit->CombatStats ? static_cast<int32>(Unit->CombatStats->TeamId) : -1;
	if (ActualTeam != Team)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_BattleBoard] %s: 팀 %d 로 스폰했는데 CombatStats.TeamId 가 %d 다. 유닛 정의의 클래스를 확인할 것."),
			*UnitClass->GetName(), Team, ActualTeam);
	}
	return Unit;
}

void ASH_BattleBoard::SpawnEnemies()
{
	if (!EnemyLayout)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleBoard] %s: EnemyLayout 이 비어 있다. 적이 없다."), *GetName());
		return;
	}

	for (const FSHEnemySlot& EnemySlot : EnemyLayout->Slots)
	{
		if (!EnemySlot.Unit || IsAllyCell(EnemySlot.Cell) || IsBlocked(EnemySlot.Cell))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[SH_BattleBoard] 적 배치 (%d, %d) 를 건너뛴다 — 유닛 정의가 없거나, 아군 구역이거나, 막힌 칸이다."),
				EnemySlot.Cell.X, EnemySlot.Cell.Y);
			continue;
		}

		ASH_RLCharacter* Unit = SpawnUnit(*EnemySlot.Unit, /*Team=*/1, EnemySlot.Cell);
		if (!Unit)
		{
			continue;
		}
		if (EnemyPlacement.Add(Unit, EnemySlot.Cell) != ESHPlaceResult::Ok)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SH_BattleBoard] 적 배치 (%d, %d) 가 겹치거나 인원을 넘는다."),
				EnemySlot.Cell.X, EnemySlot.Cell.Y);
			DestroyUnit(*Unit);
		}
	}

	if (!EnemyPlacement.IsFull())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SH_BattleBoard] 적이 %d / %d 명이다. 정책은 10 대 10 으로 학습됐다."),
			EnemyPlacement.Num(), EnemyPlacement.GetMaxUnits());
	}
}

void ASH_BattleBoard::PlaceUnitAt(ASH_RLCharacter& Unit, const FIntPoint Cell, const int32 Team) const
{
	const FTransform Where = CellTransform(Cell, Team);
	Unit.SetActorTransform(Where, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// 폰만 돌려서는 조준이 맞지 않는다 — 컨트롤 회전이 조준 방향이다.
	if (AController* UnitController = Unit.GetController())
	{
		UnitController->SetControlRotation(Where.Rotator());
	}
}

void ASH_BattleBoard::DestroyUnit(ASH_RLCharacter& Unit)
{
	if (AGun* UnitGun = Unit.gun)
	{
		UnitGun->Destroy();
	}
	Unit.Destroy();
}

//~ 배치 입력 -----------------------------------------------------------------

void ASH_BattleBoard::HandleCellClicked(const FIntPoint Cell)
{
	if (!bPlacementOpen || !IsAllyCell(Cell) || IsBlocked(Cell))
	{
		return;
	}

	ASH_RLCharacter* UnitOnCell = AllyPlacement.FindUnit(Cell);

	if (SelectedCell.IsSet())
	{
		const FIntPoint From = SelectedCell.GetValue();
		if (From == Cell)
		{
			SelectedCell.Reset();              // 같은 칸을 다시 누르면 선택 해제
		}
		else if (UnitOnCell)
		{
			SelectedCell = Cell;               // 다른 아군을 누르면 선택을 옮긴다
		}
		else if (ASH_RLCharacter* Selected = AllyPlacement.FindUnit(From))
		{
			if (AllyPlacement.Move(Selected, Cell) == ESHPlaceResult::Ok)
			{
				PlaceUnitAt(*Selected, Cell, /*Team=*/0);
			}
			SelectedCell.Reset();
		}
		else
		{
			SelectedCell.Reset();              // 선택한 유닛이 사라졌다
		}
		return;
	}

	if (UnitOnCell)
	{
		SelectedCell = Cell;
		return;
	}

	if (AllyPlacement.IsFull() || !AllyDefinition)
	{
		return;
	}

	if (ASH_RLCharacter* NewUnit = SpawnUnit(*AllyDefinition, /*Team=*/0, Cell))
	{
		if (AllyPlacement.Add(NewUnit, Cell) == ESHPlaceResult::Ok)
		{
			BroadcastPlacement();
		}
		else
		{
			DestroyUnit(*NewUnit);
		}
	}
}

void ASH_BattleBoard::HandleCellRightClicked(const FIntPoint Cell)
{
	if (!bPlacementOpen)
	{
		return;
	}

	ASH_RLCharacter* Unit = AllyPlacement.FindUnit(Cell);
	if (!Unit)
	{
		return;
	}

	if (AllyPlacement.Remove(Unit) == ESHPlaceResult::Ok)
	{
		DestroyUnit(*Unit);
		if (SelectedCell.IsSet() && SelectedCell.GetValue() == Cell)
		{
			SelectedCell.Reset();
		}
		BroadcastPlacement();
	}
}

void ASH_BattleBoard::SetVisionRaysVisible(const bool bVisible)
{
	bShowVisionRays = bVisible;
	for (ASH_RLCharacter* Unit : GetAllUnits())
	{
		if (Unit->Vision)
		{
			Unit->Vision->bDrawDebug = bVisible;
		}
	}
}

void ASH_BattleBoard::SetAimRaysVisible(const bool bVisible)
{
	bShowAimRays = bVisible;
	for (ASH_RLCharacter* Unit : GetAllUnits())
	{
		if (AGun* UnitGun = Unit->gun)
		{
			UnitGun->bDrawDebugTrace = bVisible;
		}
	}
}

void ASH_BattleBoard::HandleShotResolved(const AGun& Gun, const FGunFireResult& Result)
{
	UWorld* World = GetWorld();
	if (!TracerClass || !World || !Result.bFired)
	{
		return;
	}

	// 보이는 시작점은 총구, 끝점은 판정이 맞힌 곳(못 맞혔으면 사거리 끝).
	// ★ 총구와 판정 시작점은 수십 cm 떨어져 있어 선이 판정 선과 완전히 같지는 않다 — 그림이라 괜찮다.
	FVector Start = Result.TraceStart;
	if (Gun.Mesh && Gun.Mesh->DoesSocketExist(MuzzleSocketName))
	{
		Start = Gun.Mesh->GetSocketLocation(MuzzleSocketName);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ASH_BulletTracer* Tracer = World->SpawnActor<ASH_BulletTracer>(TracerClass, Start, FRotator::ZeroRotator, Params))
	{
		Tracer->Launch(Start, Result.ImpactPoint, TracerSpeed);
	}
}

void ASH_BattleBoard::BroadcastPlacement() const
{
	OnPlacementChanged.Broadcast(AllyPlacement.Num(), AllyPlacement.GetMaxUnits());
}

//~ 전투 ----------------------------------------------------------------------

bool ASH_BattleBoard::GetSpawnTransformForUnit(const ABase_Character* Unit, const int32 Team, FTransform& OutTransform) const
{
	const ASH_RLCharacter* RLUnit = Cast<ASH_RLCharacter>(Unit);
	const FSHPlacement& Placement = (Team == 0) ? AllyPlacement : EnemyPlacement;
	const TOptional<FIntPoint> Cell = Placement.FindCell(RLUnit);
	if (!Cell.IsSet())
	{
		return false;
	}
	OutTransform = CellTransform(Cell.GetValue(), Team);
	return true;
}

bool ASH_BattleBoard::StartBattle()
{
	if (!bPlacementOpen || !IsPlacementComplete())
	{
		return false;
	}

	bPlacementOpen = false;
	SelectedCell.Reset();
	HoveredCell.Reset();
	bBattleRunning = true;

	// 유닛 리셋 → ApplySpawnSlots(→ GetSpawnTransformForUnit) → BeginRound.
	ResetRound();
	return true;
}

void ASH_BattleBoard::ReturnToPlacement()
{
	bBattleRunning = false;
	bPlacementOpen = true;

	// 아레나 Tick 은 이미 끝난 라운드에서 아무것도 하지 않는다. 다음 ResetRound 까지 판정이 멈춰 있다.
	auto Restore = [this](const FSHPlacement& Placement, const int32 Team)
	{
		Placement.ForEach([this, Team](const FIntPoint Cell, ASH_RLCharacter& Unit)
		{
			ILearningAgentsEntityInterface::Execute_ResetEntity(&Unit, this);
			ILearningAgentsEntityInterface::Execute_EnableEntity(&Unit);
			PlaceUnitAt(Unit, Cell, Team);
		});
	};
	Restore(AllyPlacement, 0);
	Restore(EnemyPlacement, 1);

	BroadcastPlacement();
}

const USH_UnitDefinition* ASH_BattleBoard::GetAllyDefinition() const
{
	return AllyDefinition;
}

TArray<ASH_RLCharacter*> ASH_BattleBoard::GetAllUnits() const
{
	TArray<ASH_RLCharacter*> Units;
	Units.Reserve(AllyPlacement.Num() + EnemyPlacement.Num());

	auto Collect = [&Units](const FIntPoint, ASH_RLCharacter& Unit) { Units.Add(&Unit); };
	AllyPlacement.ForEach(Collect);
	EnemyPlacement.ForEach(Collect);
	return Units;
}

void ASH_BattleBoard::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);      // 아레나의 라운드 판정 · 축소 구역

	if (bPlacementOpen && bDrawPlacementGrid)
	{
		DrawPlacementGrid();
	}

	// 아레나는 라운드가 끝나면 상태만 바꾸고 아무에게도 알리지 않는다(학습 환경이 결정마다 IsRoundOver 를 묻는다).
	// 게임은 "끝났다" 를 한 번만 알려야 하므로 여기서 감지해 방송한다.
	if (bBattleRunning && IsRoundOver())
	{
		bBattleRunning = false;

		// ★ 정책이 멈춰도 유닛의 Pending 입력은 남아서 Tick 이 계속 적용한다. 비우지 않으면 결과 화면에서
		//   생존자가 마지막 방향으로 계속 걷는다.
		for (ASH_RLCharacter* Unit : GetAllUnits())
		{
			Unit->ClearPendingInput();
		}

		OnBattleEnded.Broadcast(GetRoundEnd());
	}
}

void ASH_BattleBoard::DrawPlacementGrid() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double SafeRadius = ArenaRadius - ClampMargin;
	const double HalfCell = Grid.CellSize * 0.5 - 8.0;
	const FVector Extent(HalfCell, HalfCell, 2.0);
	const FVector Center = GetActorLocation();

	for (int32 Y = 0; Y < Grid.Rows; ++Y)
	{
		for (int32 X = 0; X < Grid.Columns; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!IsAllyCell(Cell))
			{
				continue;
			}
			const FVector2D Local = Grid.CellCenter(Cell);
			if (Local.Size() > SafeRadius)
			{
				continue;   // 아레나 밖 칸은 그리지 않는다
			}

			FColor Color = FColor(170, 180, 190);
			if (IsBlocked(Cell))
			{
				Color = FColor::Red;
			}
			else if (SelectedCell.IsSet() && SelectedCell.GetValue() == Cell)
			{
				Color = FColor::Green;
			}
			else if (HoveredCell.IsSet() && HoveredCell.GetValue() == Cell)
			{
				Color = FColor::Yellow;
			}

			DrawDebugBox(World, FVector(Center.X + Local.X, Center.Y + Local.Y, Center.Z + 5.0), Extent, Color,
				/*bPersistentLines=*/false, /*LifeTime=*/-1.0f, /*DepthPriority=*/0, /*Thickness=*/3.0f);
		}
	}
#endif
}
