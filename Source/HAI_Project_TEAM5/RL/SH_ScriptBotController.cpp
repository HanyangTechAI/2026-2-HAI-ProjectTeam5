// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_ScriptBotController.h"

#include "Base_Character.h"
#include "Combat/CombatStatsComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gun.h"
#include "LearningAgentsGym.h"

ASH_ScriptBotController::ASH_ScriptBotController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// ★ 이걸 끄지 않으면 AAIController 가 매 프레임 컨트롤 회전을 폰의 방향으로
	//   덮어쓴다. 그러면 SetControlRotation 이 아무 효과도 없고, 에러도 나지 않는다.
	//   RL 정책이 조준할 때도 똑같이 걸리는 함정이다.
	bSetControlRotationFromPawnOrientation = false;

	// 포커스 기반 자동 회전을 쓰지 않는다 — 각속도 상한을 직접 걸어야 하기 때문.
	bAllowStrafe = true;
}

void ASH_ScriptBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// NOTE: 지역 변수 이름을 Character 로 쓰면 AController::Character 멤버를 가려
	//       C4458 이 뜬다 (UE 는 경고를 에러로 취급한다).
	ABase_Character* Bot = Cast<ABase_Character>(InPawn);
	if (!Bot)
	{
		return;
	}

	// 몸통이 컨트롤 회전을 따라가게 한다. 이게 곧 조준이다.
	//
	// bOrientRotationToMovement 와 동시에 켜면 안 된다 —
	// CharacterMovementComponent::PhysicsRotation 이 나중에 돌면서 덮어쓴다.
	// RotationRate.Yaw 가 각속도 상한이 되어 MaxTurnRateDeg 손잡이가 된다.
	if (UCharacterMovementComponent* Movement = Bot->GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
		Movement->RotationRate = FRotator(0.0f, MaxTurnRateDeg, 0.0f);
	}

	// 시작 시점의 컨트롤 회전을 폰 방향에 맞춰 둔다 (스폰 직후 한 프레임 튀는 것 방지).
	SetControlRotation(Bot->GetActorRotation());
	LastKnownAimPoint = Bot->GetActorLocation();
}

void ASH_ScriptBotController::OnUnPossess()
{
	CurrentTarget = nullptr;
	Super::OnUnPossess();
}

ABase_Character* ASH_ScriptBotController::FindNearestHostile(const ABase_Character* Self) const
{
	UWorld* World = GetWorld();
	if (!World || !Self)
	{
		return nullptr;
	}

	AActor* const SelfActor = const_cast<ABase_Character*>(Self);
	const FVector SelfLocation = Self->GetActorLocation();
	const float RadiusSq = TargetSearchRadius * TargetSearchRadius;

	// ★ 아레나 소속으로 후보를 자른다. 거리만으로 자르면 피치보다 큰 반경을 실수로
	//   설정했을 때 옆 아레나 유닛을 표적으로 잡고, 그 순간 두 에피소드가 서로 얽힌다.
	//   소속이 없으면(수동 배치 테스트 유닛) 거리 필터로 폴백한다.
	const ALearningAgentsGymBase* SelfGym = Self->CachedGym;

	ABase_Character* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (TActorIterator<ABase_Character> It(World); It; ++It)
	{
		ABase_Character* Candidate = *It;
		if (!Candidate || Candidate == Self)
		{
			continue;
		}

		// 풀에서 빠진(숨겨진) 유닛은 살아 있어도 표적이 아니다.
		if (!Candidate->bEntityEnabled)
		{
			continue;
		}

		const UCombatStatsComponent* Stats = Candidate->CombatStats;
		if (!Stats || !Stats->IsAlive())
		{
			continue;
		}

		if (!UCombatStatsComponent::AreHostile(SelfActor, Candidate))
		{
			continue;
		}

		if (SelfGym)
		{
			if (Candidate->CachedGym != SelfGym)
			{
				continue;
			}
		}

		// 소속 검사를 통과해도 거리 상한은 유지한다 (같은 아레나 안에서의 사거리 제한).
		const float DistSq = FVector::DistSquared(SelfLocation, Candidate->GetActorLocation());
		if (DistSq > RadiusSq || DistSq >= BestDistSq)
		{
			continue;
		}

		Best = Candidate;
		BestDistSq = DistSq;
	}

	return Best;
}

void ASH_ScriptBotController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	ABase_Character* Self = Cast<ABase_Character>(GetPawn());
	if (!Self || !Self->bEntityEnabled || !Self->CombatStats || !Self->CombatStats->IsAlive())
	{
		return;
	}

	const float MuzzleHeight = Self->gun ? Self->gun->MuzzleHeight : 60.0f;
	const FVector Origin = Self->GetActorLocation() + FVector(0.0f, 0.0f, MuzzleHeight);

	// --- 표적 갱신 (ReactionDelay 마다) --------------------------------------
	TimeSinceRetarget += DeltaTime;
	if (TimeSinceRetarget >= ReactionDelay || !CurrentTarget.IsValid())
	{
		TimeSinceRetarget = 0.0f;
		CurrentTarget = FindNearestHostile(Self);

		if (ABase_Character* Target = CurrentTarget.Get())
		{
			// 같은 높이를 노린다 — 원점과 표적점의 Z 를 맞춰 수평 사격이 되게 한다.
			LastKnownAimPoint = Target->GetActorLocation() + FVector(0.0f, 0.0f, MuzzleHeight);
		}
	}

	ABase_Character* Target = CurrentTarget.Get();
	if (!Target || !Target->CombatStats || !Target->CombatStats->IsAlive())
	{
		CurrentTarget = nullptr;
		return;
	}

	// --- 조준: 각속도 상한을 걸고 추종 ----------------------------------------
	// 정책이 아니라 실행부가 각속도를 제한한다. RL 단계에서도 같은 구조를 쓴다.
	const FVector ToAim = LastKnownAimPoint - Origin;
	const FRotator DesiredRotation = ToAim.Rotation();
	const FRotator NewControlRotation =
		FMath::RInterpConstantTo(GetControlRotation(), DesiredRotation, DeltaTime, MaxTurnRateDeg);
	SetControlRotation(NewControlRotation);

	// --- 이동: 거리 유지 + 선회 ----------------------------------------------
	const FVector SelfLocation = Self->GetActorLocation();
	FVector ToTarget = Target->GetActorLocation() - SelfLocation;
	ToTarget.Z = 0.0f;
	const float Distance = ToTarget.Size();
	const FVector ToTargetDir = (Distance > KINDA_SMALL_NUMBER) ? (ToTarget / Distance) : FVector::ForwardVector;

	TimeSinceStrafeFlip += DeltaTime;
	if (TimeSinceStrafeFlip >= StrafeFlipInterval)
	{
		TimeSinceStrafeFlip = 0.0f;
		StrafeSign = -StrafeSign;
	}

	if (Distance > DesiredRange + RangeTolerance)
	{
		Self->AddMovementInput(ToTargetDir, 1.0f);
	}
	else if (Distance < DesiredRange - RangeTolerance)
	{
		Self->AddMovementInput(-ToTargetDir, 1.0f);
	}
	else if (bStrafe)
	{
		const FVector RightDir = FVector::CrossProduct(FVector::UpVector, ToTargetDir);
		Self->AddMovementInput(RightDir, StrafeSign);
	}

	// --- 사격: 조준 오차가 콘 안이고 쿨다운이 끝났을 때 -------------------------
	const float AimErrorDeg = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(
			FVector::DotProduct(NewControlRotation.Vector(), ToAim.GetSafeNormal()), -1.0f, 1.0f)));

	const bool bInCone = AimErrorDeg <= AccuracyConeDeg;

	if (bInCone && Self->gun && Self->gun->CanFire())
	{
		Self->gun->PullTrigger();
	}

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		UWorld* World = GetWorld();
		const FColor Color = bInCone ? FColor::Red : FColor(150, 150, 160);
		DrawDebugLine(World, Origin, LastKnownAimPoint, Color, false, -1.0f, 0, 2.0f);
		DrawDebugLine(World, Origin, Origin + NewControlRotation.Vector() * 400.0f,
			FColor::Cyan, false, -1.0f, 0, 2.0f);
		DrawDebugString(World, SelfLocation + FVector(0, 0, 140),
			FString::Printf(TEXT("T%d HP%.0f err%.1f"),
				Self->CombatStats->TeamId, Self->CombatStats->CurrentHP, AimErrorDeg),
			nullptr, FColor::White, 0.0f, true, 1.2f);
	}
#endif
}
