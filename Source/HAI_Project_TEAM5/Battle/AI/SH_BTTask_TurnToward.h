// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/AI/SH_BTTask_UnitBase.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "SH_BTTask_TurnToward.generated.h"

/**
 *  블랙보드의 위치 쪽으로 돌아선다(이동은 하지 않는다). 기본은 ThreatLocation — 나를 쏜 곳.
 *  ★ 시야는 90° 라 옆 · 뒤에서 쏘는 적은 보이지 않는다. 정책은 "맞은 방향" 관측으로 이걸 배운다.
 *  행동 트리도 같은 정보(ASH_RLCharacter::LastHitSourceLocation)만 쓴다.
 */
UCLASS(meta = (DisplayName = "SH Turn Toward"))
class HAI_PROJECT_TEAM5_API USH_BTTask_TurnToward : public USH_BTTask_UnitBase
{
	GENERATED_BODY()

public:
	USH_BTTask_TurnToward();

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, float ElapsedSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector LocationKey;

	/** 남은 각이 이 안(도)이면 성공. */
	UPROPERTY(EditAnywhere, Category = "SH|Turn", meta = (ClampMin = "0.5"))
	float AcceptAngleDeg = 10.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Turn", meta = (ClampMin = "0.1"))
	float TimeoutSeconds = 1.5f;
};
