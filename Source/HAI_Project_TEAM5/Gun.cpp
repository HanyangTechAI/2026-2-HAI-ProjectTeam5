// Fill out your copyright notice in the Description page of Project Settings.

#include "Gun.h"

#include "Combat/CombatStatsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "LearningAgentsEntityInterface.h"
#include "LearningAgentsGym.h"

AGun::AGun()
{
	// 틱 불필요. 320개 액터가 빈 Tick 을 도는 것을 막는다.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	SetRootComponent(SceneRoot);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SceneRoot);
	// 총 메시가 자기 소유자의 총알을 막지 않도록. 트레이스는 캡슐만 맞춘다.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

AController* AGun::ResolveOwnerController() const
{
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		return OwnerPawn->GetController();
	}
	return nullptr;
}

bool AGun::IsSameGymAsOwner(AActor* Other) const
{
	AActor* const OwnerActor = GetOwner();
	if (!OwnerActor || !Other)
	{
		return true;
	}

	// 벽·바닥처럼 풀에 속하지 않은 액터는 검사 대상이 아니다.
	if (!OwnerActor->Implements<ULearningAgentsEntityInterface>()
		|| !Other->Implements<ULearningAgentsEntityInterface>())
	{
		return true;
	}

	const ALearningAgentsGymBase* OwnerGym = ILearningAgentsEntityInterface::Execute_GetGym(OwnerActor);
	const ALearningAgentsGymBase* OtherGym = ILearningAgentsEntityInterface::Execute_GetGym(Other);

	// 둘 다 Gym 이 없으면(수동 배치 테스트 유닛) 판정하지 않는다 —
	// 그 경우 아레나 격리 자체가 적용되지 않는 상황이므로 기존 동작을 유지한다.
	if (!OwnerGym && !OtherGym)
	{
		return true;
	}

	return OwnerGym == OtherGym;
}

bool AGun::CanFire() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	return (World->GetTimeSeconds() - LastFireTime) >= FireInterval;
}

float AGun::GetCooldownRemaining() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, FireInterval - (World->GetTimeSeconds() - LastFireTime));
}

void AGun::ResetForNewMatch()
{
	LastFireTime = -1000.0f;
}

bool AGun::GetAimOriginAndDirection(FVector& OutOrigin, FVector& OutDirection) const
{
	const AActor* OwnerActor = GetOwner();
	const AController* OwnerController = ResolveOwnerController();
	if (!OwnerActor || !OwnerController)
	{
		return false;
	}

	// 원점: 폰 캡슐 중심 기준 고정 오프셋. 애니메이션 포즈에 의존하지 않는다.
	OutOrigin = OwnerActor->GetActorLocation() + FVector(0.0f, 0.0f, MuzzleHeight);

	// 방향: 컨트롤 회전. AI 컨트롤러의 SetControlRotation 이 조준이고,
	// 플레이어는 카메라 회전이 그대로 컨트롤 회전이다.
	OutDirection = OwnerController->GetControlRotation().Vector();
	return true;
}

FGunFireResult AGun::PullTrigger()
{
	FGunFireResult Result;

	UWorld* World = GetWorld();
	if (!World)
	{
		return Result;
	}

	if (!CanFire())
	{
		// 쿨다운 중 — 아무 일도 일어나지 않는다. 누산기도 건드리지 않는다.
		return Result;
	}

	FVector Origin, Direction;
	if (!GetAimOriginAndDirection(Origin, Direction))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AGun] %s: 소유자 또는 컨트롤러가 없어 발사할 수 없다."), *GetName());
		return Result;
	}

	LastFireTime = World->GetTimeSeconds();
	Result.bFired = true;

	if (SpreadDegrees > 0.0f)
	{
		Direction = FMath::VRandCone(Direction, FMath::DegreesToRadians(SpreadDegrees));
	}

	AActor* const OwnerActor = GetOwner();
	const FVector EndLocation = Origin + Direction * Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GunTrace), /*bTraceComplex=*/false);
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(OwnerActor);

	FHitResult Hit;
	// ECC_GameTraceChannel1 = "Bullet" (DefaultEngine.ini 에 등록됨).
	// bTraceComplex=false 이므로 단순 콜리전만 맞는다 — 캐릭터는 캡슐, 벽은 박스.
	Result.bHitSomething = World->LineTraceSingleByChannel(
		Hit, Origin, EndLocation, ECC_GameTraceChannel1, Params);

	Result.ImpactPoint = Result.bHitSomething ? Hit.ImpactPoint : EndLocation;
	Result.HitActor = Result.bHitSomething ? Hit.GetActor() : nullptr;

	if (Result.bHitSomething && Result.HitActor && !IsSameGymAsOwner(Result.HitActor))
	{
		// ★ 다른 아레나의 유닛을 맞혔다. 데미지를 주지 않고 크게 기록한다 —
		//   조용히 무시하면 원인(벽 콜리전/GymsSpacing)을 영영 못 찾는다.
		++CrossGymHitCount;
		UE_LOG(LogTemp, Warning,
			TEXT("[AGun] CROSS-GYM HIT #%d — %s 가 다른 아레나의 %s 를 맞혔다. ")
			TEXT("벽의 Bullet 채널 응답과 GymsManager 의 GymsSpacing 을 확인할 것."),
			CrossGymHitCount,
			OwnerActor ? *OwnerActor->GetName() : TEXT("(없음)"),
			*Result.HitActor->GetName());
	}
	else if (Result.bHitSomething && UCombatStatsComponent::AreHostile(OwnerActor, Result.HitActor))
	{
		if (UCombatStatsComponent* VictimStats = UCombatStatsComponent::FindOn(Result.HitActor))
		{
			Result.DamageApplied = VictimStats->ApplyDamage(Damage, OwnerActor);
			// 이미 죽은 대상을 쏘면 ApplyDamage 가 0 을 돌려준다 — 명중으로 치지 않는다.
			Result.bHitHostile = Result.DamageApplied > 0.0f;
		}
	}

	// 사격 기록은 명중 여부와 무관하게 남긴다 (명중률 보상/지표의 분모).
	if (UCombatStatsComponent* OwnerStats = UCombatStatsComponent::FindOn(OwnerActor))
	{
		OwnerStats->RecordShotFired(Result.bHitHostile);
	}

#if ENABLE_DRAW_DEBUG
	if (bDrawDebugTrace)
	{
		const FColor LineColor = Result.bHitHostile ? FColor::Red
			: (Result.bHitSomething ? FColor::Yellow : FColor(90, 90, 100));
		DrawDebugLine(World, Origin, Result.ImpactPoint, LineColor, false, DebugTraceDuration, 0, 1.5f);
		if (Result.bHitSomething)
		{
			DrawDebugPoint(World, Result.ImpactPoint, 8.0f, LineColor, false, DebugTraceDuration);
		}
	}
#endif

	return Result;
}
