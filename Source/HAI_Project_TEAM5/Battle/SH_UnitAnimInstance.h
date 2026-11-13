// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SH_UnitAnimInstance.generated.h"

/**
 *  게임 유닛(BP_GameUnit_Team0/1) 애니메이션의 부모 클래스. ABP_GameUnit 이 상속한다.
 *
 *  ★ 왜 필요한가. 정책은 0.05초마다 이동 방향을 새로 고르고, 가끔 "정지" 를 고른다.
 *  ABP_Wraith 는 속도를 그대로 블렌드스페이스에 넣으므로 0.05~0.1초짜리 멈춤마다
 *  대기 포즈로 떨어졌다가 다시 걷고, 방향도 매 결정마다 튄다 — 그래서 걸음이 끊겨 보인다.
 *
 *  ★ 학습을 건드리지 않고 **보이는 값만** 고친다. 유닛의 실제 이동(캡슐)은 그대로다.
 *   1) 정지 유예: 실제 속도가 거의 0 이어도 StopHoldSeconds 동안은 직전 이동 속도를 목표로 둔다.
 *   2) 지수 평활: 로컬 수평 속도 벡터를 SpeedSmoothingSeconds 시간상수로 따라간다.
 *      각도가 아니라 **벡터**를 평활하므로 −180°/+180° 경계에서 튀지 않는다.
 *   3) 거의 멈췄을 때는 방향 각을 마지막 값으로 고정한다(0 벡터의 각도는 의미가 없다).
 *
 *  DeltaSeconds 는 전역 시간 배율이 적용된 값이라 배속을 낮춰도 같은 모양으로 보인다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API USH_UnitAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** 블렌드스페이스에 넣을 속도(cm/s). ABP 의 speed 대신 쓴다. */
	UPROPERTY(BlueprintReadOnly, Category = "SH|Locomotion")
	float SmoothedSpeed = 0.0f;

	/** 몸 기준 이동 방향(도, −180~180, 0 = 정면). ABP 의 angle 대신 쓴다. */
	UPROPERTY(BlueprintReadOnly, Category = "SH|Locomotion")
	float SmoothedAngle = 0.0f;

	/** 평활 시간상수(초). 클수록 부드럽지만 실제 움직임보다 늦게 따라간다. */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Locomotion", meta = (ClampMin = "0.0"))
	float SpeedSmoothingSeconds = 0.15f;

	/** 이 속도(cm/s) 미만이면 "멈춤" 으로 본다. */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Locomotion", meta = (ClampMin = "0.0"))
	float StopSpeedThreshold = 20.0f;

	/** 멈춤이 이 시간(초)보다 짧으면 걷기 동작을 유지한다. 결정 주기 0.05초의 몇 배로 둔다. */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Locomotion", meta = (ClampMin = "0.0"))
	float StopHoldSeconds = 0.25f;

private:
	/** 몸 기준 로컬 수평 속도(평활된 값). */
	FVector2D SmoothedLocalVelocity = FVector2D::ZeroVector;

	/** 마지막으로 움직이던 때의 로컬 속도. 정지 유예 동안의 목표다. */
	FVector2D LastMovingLocalVelocity = FVector2D::ZeroVector;

	float StoppedSeconds = 0.0f;
};
