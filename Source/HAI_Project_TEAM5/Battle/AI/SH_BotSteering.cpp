// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BotSteering.h"

#include "Combat/CombatStatsComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "RL/SH_Arena.h"
#include "RL/SH_RLCharacter.h"

namespace
{
	// 유닛 캡슐(ASH_BattleBoard::RebuildBlockedCells 와 같은 값)과 눈높이(USH_VisionComponent::EyeHeight).
	constexpr float CapsuleRadius = 42.0f;
	constexpr float CapsuleHalfHeight = 88.0f;
	constexpr float EyeHeight = 60.0f;

	FCollisionObjectQueryParams StaticObjects()
	{
		return FCollisionObjectQueryParams(ECC_WorldStatic);   // 벽 · 엄폐물만. 유닛(Pawn)은 보지 않는다
	}
}

namespace SHBot
{
	bool IsAlive(const ASH_RLCharacter& Unit)
	{
		return Unit.CombatStats && Unit.CombatStats->IsAlive();
	}

	const ASH_Arena* GetArena(const ASH_RLCharacter& Unit)
	{
		return Cast<ASH_Arena>(Unit.CachedGym.Get());
	}

	float YawErrorTo(const ASH_RLCharacter& Unit, const FVector& Point)
	{
		const AController* Controller = Unit.GetController();
		const float AimYaw = Controller ? static_cast<float>(Controller->GetControlRotation().Yaw)
		                                : static_cast<float>(Unit.GetActorRotation().Yaw);
		const FVector Delta = Point - Unit.GetActorLocation();
		const float WantYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
		return FMath::FindDeltaAngleDegrees(AimYaw, WantYaw);
	}

	float TurnRateFor(const ASH_RLCharacter& Unit, const float YawErrorDeg)
	{
		return FMath::Clamp(YawErrorDeg / DecisionPeriod, -Unit.MaxTurnRateDeg, Unit.MaxTurnRateDeg);
	}

	FVector FlatDirectionTo(const ASH_RLCharacter& Unit, const FVector& Point)
	{
		FVector Delta = Point - Unit.GetActorLocation();
		Delta.Z = 0.0;
		return Delta.SizeSquared() > 1.0 ? Delta.GetSafeNormal() : FVector::ZeroVector;
	}

	void Stop(ASH_RLCharacter& Unit)
	{
		Unit.ClearPendingInput();
	}

	float PerUnitSign(const ASH_RLCharacter& Unit)
	{
		return (Unit.GetUniqueID() & 1u) ? 1.0f : -1.0f;
	}

	TArray<FCoverCorner> GatherCoverCorners(const ASH_Arena& Arena, const float CornerOffset, const float Z)
	{
		TArray<FCoverCorner> Corners;
		TInlineComponentArray<UStaticMeshComponent*> Meshes(&Arena);
		int32 CoverIndex = 0;
		for (const UStaticMeshComponent* Mesh : Meshes)
		{
			const UStaticMesh* Asset = Mesh ? Mesh->GetStaticMesh() : nullptr;
			if (!Asset || !Mesh->IsCollisionEnabled())
			{
				continue;
			}
			// 바닥과 경계 벽은 엄폐물이 아니다(이름은 ASH_Arena / ALearningAgentsGymSimple 의 C++ 컴포넌트).
			const FString Name = Mesh->GetName();
			if (Name.StartsWith(TEXT("Wall_")) || Name.Contains(TEXT("Floor")))
			{
				continue;
			}

			// 메시의 로컬 상자를 컴포넌트 변환(위치 · 회전 · 크기)으로 옮긴다 — 회전한 벽(Ring_*)도 맞게 나온다.
			const FBox Local = Asset->GetBoundingBox();
			const FTransform& Transform = Mesh->GetComponentTransform();
			const FVector Center = Transform.TransformPosition(Local.GetCenter());
			for (const double SignX : { -1.0, 1.0 })
			{
				for (const double SignY : { -1.0, 1.0 })
				{
					const FVector LocalCorner = Local.GetCenter() + FVector(Local.GetExtent().X * SignX, Local.GetExtent().Y * SignY, 0.0);
					const FVector Corner = Transform.TransformPosition(LocalCorner);
					FVector Outward = Corner - Center;
					Outward.Z = 0.0;
					Outward = Outward.GetSafeNormal();

					FCoverCorner& Entry = Corners.AddDefaulted_GetRef();
					Entry.Location = FVector(Corner.X, Corner.Y, Z) + Outward * CornerOffset;
					Entry.CoverCenter = FVector(Center.X, Center.Y, Z);
					Entry.Outward = Outward;
					Entry.CoverIndex = CoverIndex;
				}
			}
			++CoverIndex;
		}
		return Corners;
	}

	bool IsLineBlockedByStatic(const UWorld& World, const FVector& From, const FVector& To)
	{
		const FVector Up(0.0, 0.0, EyeHeight);
		return World.LineTraceTestByObjectType(From + Up, To + Up, StaticObjects(),
			FCollisionQueryParams(SCENE_QUERY_STAT(SHBotCoverLine), /*bTraceComplex=*/false));
	}

	bool IsPathBlocked(const UWorld& World, const FVector& From, const FVector& To)
	{
		// ★ 반경을 실제보다 6 cm 작게 쓴다. 벽에 붙어 선 유닛은 시작점에서 이미 캡슐이 벽에 닿아 있어서,
		//   같은 반경으로 스윕하면 "시작부터 겹침" 으로 모든 경로가 막힌 것으로 나온다 — 바로 벽에 갇히는 상황이다.
		return World.SweepTestByObjectType(From, To, FQuat::Identity, StaticObjects(),
			FCollisionShape::MakeCapsule(CapsuleRadius - 6.0f, CapsuleHalfHeight),
			FCollisionQueryParams(SCENE_QUERY_STAT(SHBotPath), /*bTraceComplex=*/false));
	}

	bool IsSpotBlocked(const UWorld& World, const FVector& Location)
	{
		return World.OverlapAnyTestByObjectType(Location, FQuat::Identity, StaticObjects(),
			FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
			FCollisionQueryParams(SCENE_QUERY_STAT(SHBotSpot), /*bTraceComplex=*/false));
	}

	bool IsSpotCrowded(const UWorld& World, const FVector& Location, const float Radius, const AActor* Self)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SHBotCrowd), /*bTraceComplex=*/false);
		Params.AddIgnoredActor(Self);
		World.OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Radius), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const ASH_RLCharacter* Other = Cast<ASH_RLCharacter>(Overlap.GetActor());
			if (Other && IsAlive(*Other))
			{
				return true;
			}
		}
		return false;
	}

	FVector EnemySidePoint(const ASH_RLCharacter& Unit)
	{
		const ASH_Arena* Arena = GetArena(Unit);
		if (!Arena)
		{
			return Unit.GetActorLocation() + Unit.GetActorForwardVector() * 1000.0;
		}
		// 보드는 회전하지 않는다는 가정(ASH_BattleBoard 주석) — 팀 0 은 서쪽(−X)에서 시작해 동쪽을 본다.
		const bool bTeam0 = Unit.CombatStats && Unit.CombatStats->TeamId == 0;
		const FVector Center = Arena->GetActorLocation();
		return FVector(Center.X + (bTeam0 ? 1.0 : -1.0) * Arena->ArenaRadius * 0.6, Center.Y, Unit.GetActorLocation().Z);
	}
}
