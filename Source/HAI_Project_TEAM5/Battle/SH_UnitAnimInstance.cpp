// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_UnitAnimInstance.h"

#include "GameFramework/Pawn.h"

void USH_UnitAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	SmoothedLocalVelocity = FVector2D::ZeroVector;
	LastMovingLocalVelocity = FVector2D::ZeroVector;
	StoppedSeconds = 0.0f;
	SmoothedSpeed = 0.0f;
	SmoothedAngle = 0.0f;
}

void USH_UnitAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const APawn* Pawn = TryGetPawnOwner();
	if (!Pawn || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 월드 속도를 몸 기준으로 돌린다. 수평 성분만 본다(점프 · 낙하는 ABP 의 IsAirBorne 이 따로 다룬다).
	const FVector Local = Pawn->GetActorTransform().InverseTransformVectorNoScale(Pawn->GetVelocity());
	const FVector2D RawLocal(Local.X, Local.Y);

	// 1) 정지 유예 — 짧은 멈춤은 직전 이동으로 메운다.
	FVector2D Target = RawLocal;
	if (RawLocal.Size() < StopSpeedThreshold)
	{
		StoppedSeconds += DeltaSeconds;
		if (StoppedSeconds < StopHoldSeconds)
		{
			Target = LastMovingLocalVelocity;
		}
	}
	else
	{
		StoppedSeconds = 0.0f;
		LastMovingLocalVelocity = RawLocal;
	}

	// 2) 지수 평활. alpha = 1 − e^(−dt/τ) 는 프레임률이 달라도 같은 속도로 따라간다
	//    (dt 를 그대로 곱하는 선형 보간은 프레임률에 따라 결과가 달라진다).
	const float Alpha = SpeedSmoothingSeconds > UE_KINDA_SMALL_NUMBER
		? 1.0f - FMath::Exp(-DeltaSeconds / SpeedSmoothingSeconds)
		: 1.0f;
	SmoothedLocalVelocity += (Target - SmoothedLocalVelocity) * Alpha;

	SmoothedSpeed = static_cast<float>(SmoothedLocalVelocity.Size());

	// 3) 거의 멈췄으면 방향을 그대로 둔다.
	if (SmoothedSpeed > 1.0f)
	{
		SmoothedAngle = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(SmoothedLocalVelocity.Y, SmoothedLocalVelocity.X)));
	}
}
