// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_VisionComponent.h"

#include "Combat/CombatStatsComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

USH_VisionComponent::USH_VisionComponent()
{
	// 스스로 틱하지 않는다. Scan() 을 결정 주기에 맞춰 밖에서 부른다 —
	// 틱마다 훑으면 320 유닛 x 16 레이 x 60fps = 초당 307,200 트레이스가 된다.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool USH_VisionComponent::GetEyeAndAim(FVector& OutEye, FRotator& OutAim) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	OutEye = Owner->GetActorLocation() + FVector(0.0f, 0.0f, EyeHeight);

	// ★ 조준선은 컨트롤 회전이다. 몸통 방향이 아니다.
	//   AGun::GetAimOriginAndDirection (Gun.cpp:105) 이 쓰는 것과 같은 기준이어야
	//   "레이가 본 방향" 과 "총알이 가는 방향" 이 일치한다.
	//   bUseControllerDesiredRotation 때문에 몸통은 컨트롤 회전을 뒤늦게 따라오므로,
	//   몸통 방향을 쓰면 회전 중에 조준과 시야가 어긋난다.
	const APawn* OwnerPawn = Cast<APawn>(Owner);
	const AController* OwnerController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
	OutAim = OwnerController ? OwnerController->GetControlRotation() : Owner->GetActorRotation();
	return true;
}

void USH_VisionComponent::Scan(bool bForce)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	// 같은 프레임에 이미 훑었으면 결과를 그대로 쓴다.
	// RunTraining 은 한 프레임에 GatherRewards 와 GatherObservations 를 연달아 부른다.
	const uint64 Frame = GFrameCounter;
	if (!bForce && LastScanFrame == Frame)
	{
		return;
	}
	LastScanFrame = Frame;

	RayCount = FMath::Max(1, RayCount);
	Rays.SetNum(RayCount);

	bHostileVisible = false;
	HostileAngleDeg = 0.0f;
	HostileDepth01  = 1.0f;

	FVector Eye;
	FRotator Aim;
	if (!GetEyeAndAim(Eye, Aim))
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SHVisionScan), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(Owner);

	const float Half = 0.5f * FOVDegrees;
	// RayCount==1 이면 정면 한 발. 2 이상이면 양 끝을 포함해 균등 분할한다.
	const float Step = (RayCount > 1) ? (FOVDegrees / static_cast<float>(RayCount - 1)) : 0.0f;

	float BestDepth = 2.0f;

	for (int32 Index = 0; Index < RayCount; ++Index)
	{
		const float AngleDeg = -Half + Step * static_cast<float>(Index);   // 왼쪽 → 오른쪽
		const FVector Direction = FRotator(Aim.Pitch, Aim.Yaw + AngleDeg, 0.0f).Vector();
		const FVector End = Eye + Direction * MaxRange;

		FHitResult Hit;
		// ★ 총알과 같은 채널. 벽·엄폐물이 이 채널을 Block 하므로 차폐가 공짜로 따라온다.
		const bool bHit = World->LineTraceSingleByChannel(
			Hit, Eye, End, ECC_GameTraceChannel1, Params);

		FSHVisionRay& Ray = Rays[Index];
		Ray.Depth01 = bHit ? FMath::Clamp(Hit.Distance / MaxRange, 0.0f, 1.0f) : 1.0f;
		Ray.bHostile = bHit && UCombatStatsComponent::AreHostile(Owner, Hit.GetActor());

		// 가장 가까운 적대 레이를 대표로 삼는다.
		if (Ray.bHostile && Ray.Depth01 < BestDepth)
		{
			BestDepth       = Ray.Depth01;
			bHostileVisible = true;
			HostileAngleDeg = AngleDeg;
			HostileDepth01  = Ray.Depth01;
		}

#if ENABLE_DRAW_DEBUG
		if (bDrawDebug)
		{
			const FColor Color = Ray.bHostile ? FColor::Red
				: (bHit ? FColor(120, 120, 60) : FColor(50, 60, 70));
			DrawDebugLine(World, Eye, bHit ? Hit.ImpactPoint : End, Color,
				false, -1.0f, 0, Ray.bHostile ? 2.5f : 1.0f);
		}
#endif
	}

	if (bHostileVisible)
	{
		LastSeenAngleDeg = HostileAngleDeg;
		LastSeenTime     = World->GetTimeSeconds();
	}
}

bool USH_VisionComponent::CanSeeActor(const AActor* Target) const
{
	FName Blocker;
	return ClassifyVisibility(Target, Blocker) == ESHVisionResult::Visible;
}

ESHVisionResult USH_VisionComponent::ClassifyVisibility(const AActor* Target, FName& OutBlocker) const
{
	OutBlocker = NAME_None;

	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || !Target || Target == Owner)
	{
		return ESHVisionResult::Invalid;
	}

	FVector Eye;
	FRotator Aim;
	if (!GetEyeAndAim(Eye, Aim))
	{
		return ESHVisionResult::Invalid;
	}

	// 표적도 같은 눈높이를 노린다 — 수평 사격이 되어야 사거리 판정이 총알과 일치한다.
	const FVector TargetPoint = Target->GetActorLocation() + FVector(0.0f, 0.0f, EyeHeight);
	FVector ToTarget = TargetPoint - Eye;

	const float Distance = ToTarget.Size();
	if (Distance > MaxRange || Distance <= KINDA_SMALL_NUMBER)
	{
		return ESHVisionResult::OutOfRange;
	}
	ToTarget /= Distance;

	// --- 시야각 검사 --------------------------------------------------------
	// 부채꼴과 같은 기준: 조준선에서 좌우로 FOVDegrees/2 까지.
	// Yaw 평면에서만 잰다 — 전투가 평면이고 부채꼴도 Yaw 로만 벌어진다.
	const FVector AimForward = FRotator(0.0f, Aim.Yaw, 0.0f).Vector();
	FVector ToTargetFlat(ToTarget.X, ToTarget.Y, 0.0f);
	if (!ToTargetFlat.Normalize())
	{
		return ESHVisionResult::Invalid;   // 정확히 머리 위/아래 — 평면 전투에서는 없다
	}

	const float CosHalfFOV = FMath::Cos(FMath::DegreesToRadians(0.5f * FOVDegrees));
	if (FVector::DotProduct(AimForward, ToTargetFlat) < CosHalfFOV)
	{
		return ESHVisionResult::OutsideFOV;
	}

	// --- 차폐 검사 ----------------------------------------------------------
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SHVisionPoint), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(Owner);

	FHitResult Hit;
	// ★ 총알과 같은 채널. 처음 막는 것이 표적 자신이면 보이는 것이고,
	//   벽이나 엄폐물이면 가려진 것이다. 아군이 앞을 막아도 "못 쏜다" 로 처리된다 —
	//   총알도 똑같이 막히므로 그게 맞다.
	if (!World->LineTraceSingleByChannel(Hit, Eye, TargetPoint, ECC_GameTraceChannel1, Params))
	{
		// 아무것도 안 맞았다 = 표적 캡슐조차 못 맞혔다. 콜리전이 꺼진 시체/풀에서 빠진 유닛.
		return ESHVisionResult::NoHit;
	}

	if (Hit.GetActor() == Target)
	{
		return ESHVisionResult::Visible;
	}

	// ★ 액터 이름만으로는 부족하다 — 아레나는 벽 8개 + 엄폐물 7개 + 바닥을 한 액터에
	//   들고 있으므로 "무엇이 가렸는가" 를 알려면 컴포넌트 이름이 필요하다.
	OutBlocker = Hit.GetComponent() ? Hit.GetComponent()->GetFName()
		: (Hit.GetActor() ? Hit.GetActor()->GetFName() : NAME_None);
	return ESHVisionResult::Occluded;
}

float USH_VisionComponent::GetTimeSinceSeen() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return MemoryHorizonSeconds;
	}
	return FMath::Clamp(World->GetTimeSeconds() - LastSeenTime, 0.0f, MemoryHorizonSeconds);
}

void USH_VisionComponent::ResetMemory()
{
	bHostileVisible  = false;
	HostileAngleDeg  = 0.0f;
	HostileDepth01   = 1.0f;
	LastSeenAngleDeg = 0.0f;
	LastSeenTime     = -1000.0f;
	LastScanFrame    = 0;

	for (FSHVisionRay& Ray : Rays)
	{
		Ray = FSHVisionRay();
	}
}
