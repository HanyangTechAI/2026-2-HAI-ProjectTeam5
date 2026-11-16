// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/AI/SH_BTTask_UnitBase.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "SH_BTTask_Engage.generated.h"

/**
 *  보이는 적과 교전한다 — 조준(회전 상한 안에서), 조준선이 맞으면 사격, 교전 거리 유지 + 좌우 이동.
 *  안전 구역 밖이면 이동만 구역 중심으로 바꾸고 조준 · 사격은 계속한다.
 *  표적이 없어지거나 죽으면 성공으로 끝난다(트리가 다음 행동을 고른다).
 */
UCLASS(meta = (DisplayName = "SH Engage"))
class HAI_PROJECT_TEAM5_API USH_BTTask_Engage : public USH_BTTask_UnitBase
{
	GENERATED_BODY()

public:
	USH_BTTask_Engage();

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type Begin(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit) override;
	virtual EBTNodeResult::Type Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, float ElapsedSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector OutsideZoneKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ZoneCenterKey;

	/** 조준 오차가 이 각(도) 안이면 쏜다. */
	UPROPERTY(EditAnywhere, Category = "SH|Engage", meta = (ClampMin = "0.0"))
	float AccuracyConeDeg = 3.0f;

	/** 유지하려는 거리(cm). */
	UPROPERTY(EditAnywhere, Category = "SH|Engage", meta = (ClampMin = "0.0"))
	float DesiredRange = 900.0f;

	/** 이 폭 안이면 다가가거나 물러나지 않고 좌우로만 움직인다(cm). */
	UPROPERTY(EditAnywhere, Category = "SH|Engage", meta = (ClampMin = "0.0"))
	float RangeTolerance = 200.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Engage")
	bool bStrafe = true;

	/** 좌우 이동 방향을 뒤집는 주기(초). */
	UPROPERTY(EditAnywhere, Category = "SH|Engage", meta = (ClampMin = "0.1"))
	float StrafeFlipSeconds = 2.0f;

private:
	float StrafeSign = 1.0f;
	float LastFlipTime = 0.0f;
};
