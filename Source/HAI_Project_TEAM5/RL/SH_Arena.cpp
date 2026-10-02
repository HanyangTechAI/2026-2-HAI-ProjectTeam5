// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_Arena.h"

#include "Base_Character.h"
#include "Combat/CombatStatsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "LearningAgentsEntitiesManagerComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace SHArena
{
	// 엔진 기본 도형. 항상 존재하므로 하드 레퍼런스로 두어도 안전하고,
	// 프로토타입 형상을 콘텐츠 작업 없이 코드에서 전부 만들 수 있다.
	static const TCHAR* PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	static const TCHAR* CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	// 엔진 기본 도형의 로컬 바운드 반폭(cm). Plane/Cube 모두 100 x 100 크기다.
	static constexpr float BasicShapeHalfExtent = 50.0f;
}

ASH_Arena::ASH_Arena()
{
	// 라운드 판정과 축소 구역 때문에 틱이 필요하다. 아레나 16개 x 유닛 20 이라 비용은 작다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 베이스 생성자가 RootComponent 와 SimpleGymFloor 를 이미 만들었다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(SHArena::PlaneMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(SHArena::CubeMeshPath);

	if (SimpleGymFloor && PlaneMesh.Succeeded())
	{
		SimpleGymFloor->SetStaticMesh(PlaneMesh.Object);
		SimpleGymFloor->SetCollisionProfileName(TEXT("BlockAll"));
		SimpleGymFloor->SetMobility(EComponentMobility::Movable);
		SimpleGymFloor->CastShadow = false;
	}

	Walls.Reserve(NumWalls);
	for (int32 Index = 0; Index < NumWalls; ++Index)
	{
		const FName WallName(*FString::Printf(TEXT("Wall_%02d"), Index));
		UStaticMeshComponent* Wall = CreateDefaultSubobject<UStaticMeshComponent>(WallName);
		if (Wall)
		{
			Wall->SetupAttachment(RootComponent);
			if (CubeMesh.Succeeded())
			{
				Wall->SetStaticMesh(CubeMesh.Object);
			}
			// BlockAll 이면 나중에 추가할 Weapon 트레이스 채널도 막는다
			// (새 채널의 기본 응답이 Block 인 한). 총알이 옆 아레나로 넘어가지 않게 하는 1차 방어선.
			Wall->SetCollisionProfileName(TEXT("BlockAll"));
			Wall->SetMobility(EComponentMobility::Movable);
			Wall->CastShadow = false;
		}
		Walls.Add(Wall);
	}

	EntitiesManager = CreateDefaultSubobject<ULearningAgentsEntitiesManagerComponent>(TEXT("EntitiesManager"));
}

void ASH_Arena::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RebuildArenaGeometry();
	// ★ 반드시 분리해서 부른다. 경계 생성을 꺼도 유닛 풀은 채워져야 한다.
	RebuildEntityPool();
}

void ASH_Arena::RebuildArenaGeometry()
{
	// --- 수작업 형상 모드 ---------------------------------------------------
	// C++ 벽은 BP 에서 삭제할 수 없으므로(Inherited) 숨기고 콜리전만 끈다.
	// 바닥(SimpleGymFloor)은 건드리지 않는다 — BP 에서 크기를 직접 정하게 둔다.
	if (!bAutoBuildBoundary)
	{
		for (UStaticMeshComponent* Wall : Walls)
		{
			if (Wall)
			{
				Wall->SetVisibility(false);
				Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
		return;
	}

	// --- 바닥 ---------------------------------------------------------------
	// GymsManager 는 첫 번째 Gym 의 GetGymExtents() 로 16개 전부의 격자 위치를 정한다.
	// GetGymExtents 는 SimpleGymFloor 의 로컬 바운드 x 컴포넌트 스케일이므로,
	// 스케일을 여기서 ArenaRadius 에 맞추면 격자 간격도 자동으로 따라온다.
	if (SimpleGymFloor)
	{
		const float FloorScale = ArenaRadius / SHArena::BasicShapeHalfExtent;
		SimpleGymFloor->SetRelativeScale3D(FVector(FloorScale, FloorScale, 1.0f));
	}

	// --- 팔각형 벽 ----------------------------------------------------------
	// 안쪽 면이 ArenaRadius 에 오도록, 벽 중심을 ArenaRadius + 두께/2 에 놓는다.
	// 정팔각형 한 변의 길이 = 2 * ArenaRadius * tan(22.5deg). 4% 겹치게 잡아 틈을 없앤다.
	const float PlacementRadius = ArenaRadius + 0.5f * WallThickness;
	const float SideLength = 2.0f * ArenaRadius * FMath::Tan(PI / 8.0f) * 1.04f;

	const FVector WallScale(
		SideLength / (2.0f * SHArena::BasicShapeHalfExtent),
		WallThickness / (2.0f * SHArena::BasicShapeHalfExtent),
		WallHeight / (2.0f * SHArena::BasicShapeHalfExtent));

	for (int32 Index = 0; Index < Walls.Num(); ++Index)
	{
		UStaticMeshComponent* Wall = Walls[Index];
		if (!Wall)
		{
			continue;
		}

		const float AngleDeg = 45.0f * static_cast<float>(Index);
		const float AngleRad = FMath::DegreesToRadians(AngleDeg);

		// 벽의 길이축(X)이 접선 방향을 향해야 면이 중심을 본다 → yaw = 각도 + 90.
		Wall->SetRelativeLocation(FVector(
			PlacementRadius * FMath::Cos(AngleRad),
			PlacementRadius * FMath::Sin(AngleRad),
			0.5f * WallHeight));
		Wall->SetRelativeRotation(FRotator(0.0f, AngleDeg + 90.0f, 0.0f));
		Wall->SetRelativeScale3D(WallScale);

		// bAutoBuildBoundary 를 껐다 켰을 때 되살린다.
		Wall->SetVisibility(true);
		Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void ASH_Arena::RebuildEntityPool()
{
	if (!EntitiesManager)
	{
		return;
	}

	EntitiesManager->Entities.Reset();

	// 두 클래스가 모두 지정되기 전에는 비워 둔다 — 그렇지 않으면
	// CheckEntityClasses() 가 매 프레임 Error 를 찍는다.
	if (!Team0UnitClass || !Team1UnitClass)
	{
		return;
	}

	auto MakeInfo = [this](const TSubclassOf<AActor>& UnitClass)
	{
		FLearningAgentsEntityInfo Info;
		Info.EntityClass = UnitClass;
		Info.EntitySpawnZOffset = SpawnZOffset;
		// Min == Max. 이유는 UnitsPerTeam 주석 참조.
		Info.EpisodeEntitySpawnCountMin = UnitsPerTeam;
		Info.EpisodeEntitySpawnCountMax = UnitsPerTeam;
		return Info;
	};

	EntitiesManager->Entities.Add(MakeInfo(Team0UnitClass));
	EntitiesManager->Entities.Add(MakeInfo(Team1UnitClass));
}

void ASH_Arena::GetGymExtents(FVector& OutMinExtents, FVector& OutMaxExtents) const
{
	// 콜리전이 있는 컴포넌트 전체(바닥 + 벽 + 엄폐물)를 감싸는 로컬 박스.
	//
	// ★ bNonColliding=false 인 이유: 여기서 필요한 것은 "물리적으로 차지하는 범위" 다.
	//   피치를 정하는 목적이 총알 누출과 아레나 겹침을 막는 것이므로, 콜리전 없는
	//   장식 메시는 포함시키지 않는다. 에디터 전용 빌보드가 섞여 들어오는 것도 막는다.
	const FBox Box = CalculateComponentsBoundingBoxInLocalSpace(/*bNonColliding=*/false);
	if (Box.IsValid)
	{
		OutMinExtents = Box.Min;
		OutMaxExtents = Box.Max;
		return;
	}

	// 폴백: 바닥 하나만 본다 (부모 구현).
	Super::GetGymExtents(OutMinExtents, OutMaxExtents);
}

void ASH_Arena::BeginPlay()
{
	Super::BeginPlay();

	if (Team0UnitClass && Team1UnitClass && Team0UnitClass == Team1UnitClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_Arena] %s: 두 팀이 같은 유닛 클래스를 쓴다. 엔진 풀은 EntityClass 이름으로 ")
			TEXT("키잉되므로 두 팀이 한 풀을 공유해 팀 구분이 불가능해진다. 별도 BP 를 만들 것."),
			*GetName());
	}

	if (SpawnRadius + SpawnRadiusJitter > ArenaRadius - ClampMargin)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SH_Arena] %s: SpawnRadius(%.0f) + Jitter(%.0f) 가 클램프 반경(%.0f) 밖이다. ")
			TEXT("스폰 즉시 안으로 당겨진다."),
			*GetName(), SpawnRadius, SpawnRadiusJitter, ArenaRadius - ClampMargin);
	}

	// Gym 의 생명주기 훅. GymsManager::Start() 가 스폰 직후 Initialize() 를 부르고,
	// 에피소드마다 Reset() 이 불린다. 둘 다 이 BeginPlay 이후다.
	OnGymInitialized.AddDynamic(this, &ASH_Arena::HandleGymInitialized);
	OnPostGymReset.AddDynamic(this, &ASH_Arena::HandlePostGymReset);

	// 엄폐물이 스폰 자리를 먹었는지 확인한다. 유닛이 아직 스폰되기 전이라
	// 유닛끼리의 겹침은 잡히지 않고 순수하게 배치 실수만 잡힌다.
	ValidateSpawnSlotsClear();

	if (bDrawDebugOnBeginPlay)
	{
		DrawDebugArena(60.0f);
	}
}

void ASH_Arena::ValidateSpawnSlotsClear() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ABase_Character 의 기본 캡슐 치수. BP 에서 바꿨다면 여기도 맞춰야 한다.
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(42.0f, 96.0f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SHSpawnSlotCheck), /*bTraceComplex=*/false);
	// ★ AddIgnoredActor(this) 를 쓰면 안 된다 — 엄폐물이 이 액터의 컴포넌트이므로
	//   검사 대상 자체가 사라진다. 바닥만 콕 집어 무시한다.
	// TObjectPtr 를 그대로 넘기면 raw-pointer / TWeakObjectPtr 오버로드가 모호해진다(C2668).
	if (const UPrimitiveComponent* FloorComponent = SimpleGymFloor)
	{
		Params.AddIgnoredComponent(FloorComponent);
	}

	for (int32 Team = 0; Team < 2; ++Team)
	{
		TArray<FTransform> Slots;
		GetSpawnTransforms(Team, UnitsPerTeam, Slots);

		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			const FVector Location = Slots[Index].GetLocation();

			TArray<FOverlapResult> Overlaps;
			if (!World->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity,
					ECC_Pawn, Capsule, Params))
			{
				continue;
			}

			for (const FOverlapResult& Overlap : Overlaps)
			{
				const UPrimitiveComponent* Comp = Overlap.GetComponent();
				UE_LOG(LogTemp, Error,
					TEXT("[SH_Arena] %s: 팀%d 슬롯 %d (%s) 가 %s 에 막혀 있다. ")
					TEXT("BP_SHArena 에서 엄폐물을 옮기거나 SpawnRadius(%.0f) 를 조정할 것."),
					*GetName(), Team, Index, *Location.ToCompactString(),
					Comp ? *Comp->GetName() : TEXT("(알 수 없음)"), SpawnRadius);
			}
		}
	}
}

void ASH_Arena::HandleGymInitialized()
{
	// 초기화 때 한 번만 검사한다 — 매 리셋마다 찍으면 로그가 도배된다.
	ValidateTeamAuthoring();
	ApplySpawnSlots();
	BeginRound();
}

void ASH_Arena::HandlePostGymReset()
{
	ApplySpawnSlots();
	BeginRound();
}

//~ 라운드 -------------------------------------------------------------------

bool ASH_Arena::IsUnitAlive(const ABase_Character* Unit)
{
	return Unit && Unit->bEntityEnabled && Unit->CombatStats && Unit->CombatStats->IsAlive();
}

void ASH_Arena::BeginRound()
{
	const UWorld* World = GetWorld();
	RoundStartTime = World ? World->GetTimeSeconds() : 0.0f;
	RoundEnd = ESHRoundEnd::None;
	FirstDamageTime = -1.0f;
	ZoneDamageTimer = 0.0f;
}

void ASH_Arena::EndRound(ESHRoundEnd End)
{
	RoundEnd = End;

	FSHRoundRecord Record;
	Record.End = End;
	Record.Duration = GetRoundElapsed();
	Record.FirstDamageTime = FirstDamageTime;

	// 리포트가 돌지 않는 환경(에디터 미리보기 등)에서 무한히 쌓이지 않게.
	if (PendingRoundRecords.Num() < 4096)
	{
		PendingRoundRecords.Add(Record);
	}
}

float ASH_Arena::GetRoundElapsed() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::Max(0.0f, World->GetTimeSeconds() - RoundStartTime) : 0.0f;
}

float ASH_Arena::GetZoneRadius() const
{
	if (!bShrinkZone)
	{
		return ArenaRadius;
	}

	const float Elapsed = GetRoundElapsed();
	if (Elapsed <= ZoneStartSeconds)
	{
		return ArenaRadius;
	}

	const float Alpha = FMath::Clamp(
		(Elapsed - ZoneStartSeconds) / FMath::Max(ZoneShrinkSeconds, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	return FMath::Lerp(ArenaRadius, FMath::Min(ZoneFinalRadius, ArenaRadius), Alpha);
}

float ASH_Arena::GetRoundOutcome(int32 Team) const
{
	switch (RoundEnd)
	{
	case ESHRoundEnd::Team0Wins:
		return (Team == 0) ? 1.0f : -1.0f;
	case ESHRoundEnd::Team1Wins:
		return (Team == 1) ? 1.0f : -1.0f;
	case ESHRoundEnd::Timeout:
	case ESHRoundEnd::MutualWipe:
		// ★ 승자 없음. TFT 와 같이 양쪽 모두 패배로 본다 — 대치를 균형점에서 빼내는 규칙이다.
		return bTimeoutIsLossForBoth ? -1.0f : 0.0f;
	default:
		return 0.0f;
	}
}

int32 ASH_Arena::GetAliveCount(int32 Team) const
{
	return (Team == 0 || Team == 1) ? AliveCount[Team] : 0;
}

int32 ASH_Arena::GetTeamSize(int32 Team) const
{
	return (Team == 0 || Team == 1) ? TeamUnits[Team].Num() : 0;
}

const TArray<TWeakObjectPtr<ABase_Character>>& ASH_Arena::GetTeamUnits(int32 Team) const
{
	static const TArray<TWeakObjectPtr<ABase_Character>> Empty;
	return (Team == 0 || Team == 1) ? TeamUnits[Team] : Empty;
}

void ASH_Arena::ConsumeRoundRecords(TArray<FSHRoundRecord>& OutRecords)
{
	OutRecords.Append(PendingRoundRecords);
	PendingRoundRecords.Reset();
}

float ASH_Arena::ConsumeZoneDamage(int32 Team)
{
	if (Team != 0 && Team != 1)
	{
		return 0.0f;
	}
	const float Total = ZoneDamageTotal[Team];
	ZoneDamageTotal[Team] = 0.0f;
	return Total;
}

void ASH_Arena::ApplyZoneDamage(float DeltaSeconds)
{
	ZoneDamageTimer += DeltaSeconds;
	if (ZoneDamageTimer < ZoneDamageInterval)
	{
		return;
	}
	const float Dt = ZoneDamageTimer;
	ZoneDamageTimer = 0.0f;

	const float Radius = GetZoneRadius();
	if (Radius >= ArenaRadius)
	{
		return;   // 아직 줄지 않았다
	}

	const FVector Center = GetActorLocation();
	const float RadiusSq = Radius * Radius;
	const float Amount = ZoneDamagePerSecond * Dt;

	for (int32 Team = 0; Team < 2; ++Team)
	{
		for (const TWeakObjectPtr<ABase_Character>& Weak : TeamUnits[Team])
		{
			ABase_Character* Unit = Weak.Get();
			if (!IsUnitAlive(Unit))
			{
				continue;
			}
			if (FVector::DistSquaredXY(Unit->GetActorLocation(), Center) <= RadiusSq)
			{
				continue;
			}

			// ★ 가해자 nullptr. 받은 피해로는 누산되지만(보상에 들어간다) 누구의 준 피해도
			//   아니다. ASH_RLCharacter 의 피격 기억은 가해자가 없으면 지난 기억을 유지한다.
			ZoneDamageTotal[Team] += Unit->CombatStats->ApplyDamage(Amount, nullptr);
		}
	}
}

void ASH_Arena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	for (int32 Team = 0; Team < 2; ++Team)
	{
		int32 Alive = 0;
		for (const TWeakObjectPtr<ABase_Character>& Weak : TeamUnits[Team])
		{
			Alive += IsUnitAlive(Weak.Get()) ? 1 : 0;
		}
		AliveCount[Team] = Alive;
	}

	// 끝난 라운드는 학습 환경이 아레나를 리셋할 때까지 그대로 둔다.
	// ★ 여기서 스스로 리셋하면 안 된다. 리셋은 완료 신호(Termination)를 처리한 뒤
	//   USH_ShooterTrainingEnv::ResetAgentEpisode 가 해야 결과 보상과 리셋 순서가 맞는다.
	if (RoundEnd != ESHRoundEnd::None)
	{
		return;
	}

	// 유닛이 없는 아레나(레벨에 편집용으로 놓은 인스턴스 등)는 판정하지 않는다.
	if (TeamUnits[0].Num() == 0 || TeamUnits[1].Num() == 0)
	{
		return;
	}

	const float Elapsed = GetRoundElapsed();

	if (FirstDamageTime < 0.0f)
	{
		for (int32 Team = 0; Team < 2 && FirstDamageTime < 0.0f; ++Team)
		{
			for (const TWeakObjectPtr<ABase_Character>& Weak : TeamUnits[Team])
			{
				const ABase_Character* Unit = Weak.Get();
				if (Unit && Unit->CombatStats && Unit->CombatStats->CurrentHP < Unit->CombatStats->MaxHP)
				{
					FirstDamageTime = Elapsed;
					break;
				}
			}
		}
	}

	const bool bWiped0 = AliveCount[0] == 0;
	const bool bWiped1 = AliveCount[1] == 0;
	if (bWiped0 && bWiped1)
	{
		EndRound(ESHRoundEnd::MutualWipe);
		return;
	}
	if (bWiped1)
	{
		EndRound(ESHRoundEnd::Team0Wins);
		return;
	}
	if (bWiped0)
	{
		EndRound(ESHRoundEnd::Team1Wins);
		return;
	}
	if (Elapsed >= RoundTimeLimitSeconds)
	{
		EndRound(ESHRoundEnd::Timeout);
		return;
	}

	if (bShrinkZone)
	{
		ApplyZoneDamage(DeltaSeconds);
	}
}

void ASH_Arena::CollectUnits(int32 Team, TArray<ABase_Character*>& OutUnits) const
{
	OutUnits.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ABase_Character> It(World); It; ++It)
	{
		ABase_Character* Unit = *It;
		if (!Unit || !IsMemberOfGym(Unit))
		{
			continue;
		}

		// ★ 팀의 근거는 클래스가 아니라 유닛이 들고 있는 데이터다.
		//   각 팀 BP 가 CombatStats.TeamId 를 저장하고, 아레나는 읽기만 한다.
		const UCombatStatsComponent* Stats = Unit->CombatStats;
		if (Stats && static_cast<int32>(Stats->TeamId) == Team)
		{
			OutUnits.Add(Unit);
		}
	}
}

void ASH_Arena::ValidateTeamAuthoring() const
{
	TArray<ABase_Character*> Team0Units, Team1Units;
	CollectUnits(0, Team0Units);
	CollectUnits(1, Team1Units);

	if (Team0Units.Num() > 0 && Team1Units.Num() > 0)
	{
		return;
	}

	// 한쪽 팀이 비어 있으면 전투가 성립하지 않는다. 거의 항상 원인은 하나다:
	// 두 유닛 BP 중 하나가 CombatStats.TeamId 를 기본값 0 인 채로 두었다.
	UE_LOG(LogTemp, Error,
		TEXT("[SH_Arena] %s: 팀 구성이 잘못됐다 (팀0 %d명 / 팀1 %d명). ")
		TEXT("각 유닛 BP 의 CombatStats -> TeamId 를 확인할 것 — ")
		TEXT("팀은 클래스가 아니라 이 값으로 정해진다. 두 BP 가 모두 기본값 0 이면 이 상태가 된다."),
		*GetName(), Team0Units.Num(), Team1Units.Num());
}

void ASH_Arena::ApplySpawnSlots()
{
	for (int32 Team = 0; Team < 2; ++Team)
	{
		TArray<ABase_Character*> Units;
		CollectUnits(Team, Units);

		// 라운드 판정과 관측이 매 프레임 쓰는 캐시. 여기서만 월드를 훑는다.
		TeamUnits[Team].Reset(Units.Num());
		for (ABase_Character* Unit : Units)
		{
			TeamUnits[Team].Add(Unit);
		}

		if (Units.Num() == 0)
		{
			continue;
		}

		TArray<FTransform> Slots;
		GetSpawnTransforms(Team, Units.Num(), Slots);

		for (int32 Index = 0; Index < Units.Num() && Index < Slots.Num(); ++Index)
		{
			ABase_Character* Unit = Units[Index];
			const FTransform& Slot = Slots[Index];

			// TeleportPhysics — 보간 없이 즉시 옮긴다. 안 그러면 캡슐이 이전 위치에서
			// 새 위치까지 쓸고 지나가면서 도중의 벽/유닛과 충돌한다.
			Unit->SetActorTransform(Slot, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

			// 폰만 돌려서는 조준이 맞지 않는다 — 컨트롤 회전이 조준 방향이다.
			if (AController* C = Unit->GetController())
			{
				C->SetControlRotation(Slot.Rotator());
			}
		}
	}
}

FVector ASH_Arena::ProjectPointToGym(const FVector& InLocation) const
{
	const FVector Center = GetActorLocation();
	const float Limit = FMath::Max(0.0f, ArenaRadius - ClampMargin);

	FVector Offset = InLocation - Center;
	Offset.Z = 0.0f;

	const float Dist = Offset.Size();
	if (Dist > Limit && Dist > KINDA_SMALL_NUMBER)
	{
		Offset *= (Limit / Dist);
	}

	return FVector(Center.X + Offset.X, Center.Y + Offset.Y, Center.Z);
}

FVector ASH_Arena::GenerateRandomLocationInGym() const
{
	const FVector Center = GetActorLocation();

	if (!RandomStream.IsValid())
	{
		// GymsManager::Start() 가 스트림을 주입하기 전에 불린 경우.
		UE_LOG(LogTemp, Warning,
			TEXT("[SH_Arena] %s: RandomStream 이 없다. GymsManager->Start() 가 호출됐는지 확인할 것."),
			*GetName());
		return Center;
	}

	const float Limit = FMath::Max(0.0f, ArenaRadius - ClampMargin);

	// 면적 균등: 반지름에 sqrt 를 씌운다. 안 씌우면 점이 중심 쪽에 몰린다.
	const float Angle = RandomStream->FRandRange(0.0f, 2.0f * PI);
	const float Radius = Limit * FMath::Sqrt(RandomStream->FRand());

	return FVector(
		Center.X + Radius * FMath::Cos(Angle),
		Center.Y + Radius * FMath::Sin(Angle),
		Center.Z);
}

FTransform ASH_Arena::GetSpawnTransformForSlot(int32 Team, int32 SlotIndex, int32 LiveCount) const
{
	const FVector Center = GetActorLocation();
	const int32 Count = FMath::Max(1, LiveCount);
	const int32 Slot = FMath::Clamp(SlotIndex, 0, Count - 1);

	// 팀 0 은 -X 쪽, 팀 1 은 +X 쪽. 호의 중심을 기준으로 좌우 대칭 배치.
	const float BaseAngleDeg = (Team == 0) ? 180.0f : 0.0f;
	const float Offset = (static_cast<float>(Slot) - 0.5f * static_cast<float>(Count - 1)) * ArcStepDegrees;

	float AngleDeg = BaseAngleDeg + Offset;
	float Radius = SpawnRadius;

	if (RandomStream.IsValid())
	{
		if (SpawnAngleJitterDegrees > 0.0f)
		{
			AngleDeg += RandomStream->FRandRange(-SpawnAngleJitterDegrees, SpawnAngleJitterDegrees);
		}
		if (SpawnRadiusJitter > 0.0f)
		{
			Radius += RandomStream->FRandRange(-SpawnRadiusJitter, SpawnRadiusJitter);
		}
	}

	Radius = FMath::Clamp(Radius, 0.0f, FMath::Max(0.0f, ArenaRadius - ClampMargin));

	const float AngleRad = FMath::DegreesToRadians(AngleDeg);
	const FVector Location(
		Center.X + Radius * FMath::Cos(AngleRad),
		Center.Y + Radius * FMath::Sin(AngleRad),
		Center.Z + SpawnZOffset);

	// 원 위의 점에서 중심을 향하는 방향은 yaw = 각도 + 180.
	const FRotator Rotation(0.0f, AngleDeg + 180.0f, 0.0f);

	return FTransform(Rotation, Location, FVector::OneVector);
}

void ASH_Arena::GetSpawnTransforms(int32 Team, int32 LiveCount, TArray<FTransform>& OutTransforms) const
{
	OutTransforms.Reset();

	if (LiveCount <= 0)
	{
		return;
	}

	OutTransforms.Reserve(LiveCount);
	for (int32 Slot = 0; Slot < LiveCount; ++Slot)
	{
		OutTransforms.Add(GetSpawnTransformForSlot(Team, Slot, LiveCount));
	}
}

void ASH_Arena::DrawDebugArena(float Duration) const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Center = GetActorLocation();
	const FVector AxisY = FVector::RightVector;
	const FVector AxisX = FVector::ForwardVector;

	// 벽 안쪽 면.
	DrawDebugCircle(World, Center + FVector(0, 0, 10.0f), ArenaRadius, 64,
		FColor(120, 130, 140), false, Duration, 0, 4.0f, AxisY, AxisX, false);

	// 실제 클램프 반경.
	DrawDebugCircle(World, Center + FVector(0, 0, 12.0f), ArenaRadius - ClampMargin, 64,
		FColor(200, 90, 40), false, Duration, 0, 2.0f, AxisY, AxisX, false);

	// 스폰 원.
	DrawDebugCircle(World, Center + FVector(0, 0, 14.0f), SpawnRadius, 64,
		FColor(90, 90, 100), false, Duration, 0, 1.0f, AxisY, AxisX, false);

	// 팀별 슬롯.
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const FColor TeamColor = (Team == 0) ? FColor(40, 110, 190) : FColor(200, 80, 30);

		TArray<FTransform> Slots;
		GetSpawnTransforms(Team, UnitsPerTeam, Slots);

		for (const FTransform& Slot : Slots)
		{
			const FVector Loc = Slot.GetLocation();
			DrawDebugCapsule(World, Loc, 96.0f, 42.0f, FQuat::Identity,
				TeamColor, false, Duration, 0, 2.0f);
			DrawDebugDirectionalArrow(World, Loc,
				Loc + Slot.GetRotation().GetForwardVector() * 250.0f,
				60.0f, TeamColor, false, Duration, 0, 3.0f);
		}
	}

	// 아레나 중심 — "유닛이 월드 원점에 쌓이는" 증상과 구분하기 위한 기준점.
	DrawDebugSphere(World, Center, 40.0f, 12, FColor(240, 240, 240), false, Duration, 0, 2.0f);
	DrawDebugString(World, Center + FVector(0, 0, 320.0f), GetName(), nullptr,
		FColor::White, Duration, false, 1.5f);
#endif // ENABLE_DRAW_DEBUG
}
