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
#include "SH_VisionComponent.h"

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

	// ★ 선회 속도를 유닛마다 어긋나게 한다. 폰의 고유 id 에서 뽑으므로 전역 RNG 를
	//   쓰지 않고, 같은 실행에서 같은 유닛은 항상 같은 값을 받는다.
	//   동기화되면 상대 방위가 고정되어 영원히 못 만난다 — 헤더의 실측 근거 참조.
	const float Normalized = static_cast<float>(Bot->GetUniqueID() % 97) / 96.0f;   // 0 … 1
	SweepRateScale = 1.0f + SearchSweepDesync * (2.0f * Normalized - 1.0f);

	// 시작 시점의 컨트롤 회전을 폰 방향에 맞춰 둔다 (스폰 직후 한 프레임 튀는 것 방지).
	SetControlRotation(Bot->GetActorRotation());
	LastKnownAimPoint = Bot->GetActorLocation();
}

void ASH_ScriptBotController::OnUnPossess()
{
	CurrentTarget = nullptr;
	// ★ 폰이 풀에서 재사용되므로 기억을 비운다. 안 비우면 지난 판의
	//   "마지막으로 본 지점" 으로 새 판을 시작한다.
	bEverAcquiredTarget = false;
	TimeSinceTargetLost = 0.0f;
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

	// 시야 제한. 컴포넌트를 클래스로 찾으므로 폰이 ASH_RLCharacter 든 아니든 동작한다.
	const USH_VisionComponent* Vision = nullptr;
	if (bRequireLineOfSight)
	{
		Vision = Self->FindComponentByClass<USH_VisionComponent>();
		if (!Vision)
		{
			// ★ 조용히 전지적으로 돌아가면 안 된다. 그 상태로 학습하면 정책이
			//   비대칭을 학습하고, 로그가 없으면 원인을 영영 못 찾는다.
			UE_LOG(LogTemp, Error,
				TEXT("[SH_ScriptBotController] %s: bRequireLineOfSight 가 켜져 있는데 폰 %s 에 ")
				TEXT("USH_VisionComponent 가 없다. 전지적으로 동작한다 — BP 유닛의 부모를 ")
				TEXT("ASH_RLCharacter 로 두거나 컴포넌트를 직접 추가할 것."),
				*GetName(), *Self->GetName());
		}
	}

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

		// ★ 시야 검사는 맨 마지막이다 — 레이 트레이스라 가장 비싸고,
		//   "지금까지의 최선보다 가까운" 후보에만 돌면 된다.
		if (Vision && !Vision->CanSeeActor(Candidate))
		{
			continue;
		}

		Best = Candidate;
		BestDistSq = DistSq;
	}

	return Best;
}

void ASH_ScriptBotController::UpdateSearchBehavior(ABase_Character* Self, const FVector& Origin, float DeltaTime)
{
	if (TimeSinceTargetLost < SearchSweepDelay && bEverAcquiredTarget)
	{
		// 마지막으로 본 지점을 계속 겨눈다 — 적이 엄폐물 뒤로 들어간 직후 다시
		// 나오는 것을 노린다. 이게 없으면 엄폐물이 곧 무적이 된다.
		const FVector ToAim = LastKnownAimPoint - Origin;
		if (!ToAim.IsNearlyZero())
		{
			SetControlRotation(FMath::RInterpConstantTo(
				GetControlRotation(), ToAim.Rotation(), DeltaTime, MaxTurnRateDeg));
		}
		return;
	}

	// --- 선회로 훑는다. 한 방향으로 계속 돌면 360도를 전부 덮는다 -------------
	// NOTE: 지역 변수 이름을 ControlRotation 으로 쓰면 AController::ControlRotation 멤버를
	//       가려 C4458 이 뜬다.
	FRotator SweepRotation = GetControlRotation();
	SweepRotation.Yaw += SearchSweepSign * SearchSweepRateDeg * SweepRateScale * DeltaTime;
	SetControlRotation(SweepRotation);

	// --- 전진 ---------------------------------------------------------------
	// ★ 이게 없으면 시야가 막힌 짝이 영원히 교착한다 (헤더의 실측값 참조).
	if (!bAdvanceWhileSearching || !Self)
	{
		return;
	}

	const FVector SelfLocation = Self->GetActorLocation();

	// --- 목표 결정 ----------------------------------------------------------
	FVector Goal;
	if (bEverAcquiredTarget && TimeSinceTargetLost < SearchCommitSeconds)
	{
		// 방금 놓쳤다 — 마지막으로 본 지점으로 간다.
		Goal = LastKnownAimPoint;
	}
	else
	{
		// ★ 아레나 **반대편**이다. 중심이 아니다 — 헤더의 실측 근거 참조.
		//   매 프레임 대척점을 다시 계산하므로 목표에 도달하는 일이 없고,
		//   따라서 엄폐물 사이에 멈춰 서지 않는다.
		const ALearningAgentsGymBase* Gym = Self->CachedGym;
		if (!Gym)
		{
			return;   // 소속 아레나가 없으면 어디로 갈지 알 수 없다
		}
		const FVector Center = Gym->GetActorLocation();
		Goal = Center + (Center - SelfLocation);
	}

	FVector ToGoal = Goal - SelfLocation;
	ToGoal.Z = 0.0f;

	// 사실상 목표에 서 있으면 밀지 않는다 (0 벡터 정규화 방지).
	if (ToGoal.SizeSquared() < FMath::Square(50.0f))
	{
		return;
	}

	Self->AddMovementInput(ToGoal.GetSafeNormal(), 1.0f);
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
			bEverAcquiredTarget = true;
		}
	}

	ABase_Character* Target = CurrentTarget.Get();
	if (!Target || !Target->CombatStats || !Target->CombatStats->IsAlive())
	{
		// 막 잃은 프레임에만 탐색 방향을 뒤집는다 (표적이 있는 동안은 0 으로 유지되므로).
		if (TimeSinceTargetLost <= 0.0f)
		{
			SearchSweepSign = -SearchSweepSign;
		}

		CurrentTarget = nullptr;
		TimeSinceTargetLost += DeltaTime;

		// 제자리에서 탐색한다. 이동은 하지 않는다 — 표적 위치를 모르는 상태에서
		// 움직이면 엄폐물에 끼거나 벽을 향해 걷는다.
		UpdateSearchBehavior(Self, Origin, DeltaTime);
		return;
	}

	TimeSinceTargetLost = 0.0f;

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
