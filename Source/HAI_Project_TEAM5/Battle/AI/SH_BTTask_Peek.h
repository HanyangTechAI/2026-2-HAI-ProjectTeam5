// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/AI/SH_BTTask_UnitBase.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "SH_BTTask_Peek.generated.h"

/**
 *  엄폐물 끝에서 앞을 확인한다.
 *   1) 앞(FrontLocation)을 보면서 PeekLocation(엄폐물 끝에서 옆으로 한 걸음)으로 나간다.
 *   2) 거기서 HoldSeconds 동안 앞쪽을 ±SweepHalfAngle 로 훑는다.
 *  그동안 적이 보이면 트리의 교전 가지(HasTarget, Observer aborts)가 이 태스크를 끊는다.
 *  끝까지 아무도 안 보이면 = 안전 → 성공(다음 엄폐물로 간다).
 */
UCLASS(meta = (DisplayName = "SH Peek"))
class HAI_PROJECT_TEAM5_API USH_BTTask_Peek : public USH_BTTask_UnitBase
{
	GENERATED_BODY()

public:
	USH_BTTask_Peek();

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type Begin(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit) override;
	virtual EBTNodeResult::Type Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, float ElapsedSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector PeekLocationKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector FrontLocationKey;

	/** 옆으로 나가는 데 쓸 최대 시간(초). 막혀서 못 가면 그 자리에서 본다. */
	UPROPERTY(EditAnywhere, Category = "SH|Peek", meta = (ClampMin = "0.0"))
	float StepOutSeconds = 0.8f;

	/** 나간 자리에서 앞을 보는 시간(초). */
	UPROPERTY(EditAnywhere, Category = "SH|Peek", meta = (ClampMin = "0.1"))
	float HoldSeconds = 1.0f;

	/** 앞을 기준으로 좌우로 훑는 각(도). 시야각 90° 의 바깥까지 한 번씩 본다. */
	UPROPERTY(EditAnywhere, Category = "SH|Peek", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SweepHalfAngle = 30.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Peek", meta = (ClampMin = "10.0"))
	float AcceptRadius = 40.0f;

private:
	float HoldStartTime = -1.0f;   // 음수 = 아직 나가는 중
};
