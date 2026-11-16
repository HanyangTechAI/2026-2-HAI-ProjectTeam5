// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/AI/SH_BTTask_UnitBase.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "SH_BTTask_FindCover.generated.h"

/**
 *  다음으로 갈 엄폐물 끝을 고른다(즉시 끝나는 태스크). 고르면 성공, 갈 곳이 없으면 실패.
 *
 *  "적이 있다고 보는 곳(Front)" 을 먼저 정한다:
 *    방금 놓친 적(RecentlySeen → LastSeenLocation) > 최근에 나를 쏜 곳(ThreatLocation) > 상대 진영 쪽.
 *  그 다음 보드의 엄폐물 모서리(SHBot::GatherCoverCorners) 중에서 고른다:
 *    - 가려져야 한다: Front 에서 그 자리까지 직선을 엄폐물이 막는다(= 엄폐물의 뒤쪽 끝).
 *    - 직선으로 갈 수 있어야 한다(캡슐 스윕) · 그 자리가 비어 있어야 한다(다른 유닛 · 벽과 겹치지 않음).
 *    - 점수 = Front 쪽으로 다가가는 정도 − 이동 거리 × DistanceWeight. 최근에 간 곳은 제외.
 *  고른 자리 → CoverLocation, 그 끝에서 옆으로 한 걸음 나간 자리 → PeekLocation, Front → FrontLocation.
 */
UCLASS(meta = (DisplayName = "SH Find Cover"))
class HAI_PROJECT_TEAM5_API USH_BTTask_FindCover : public USH_BTTask_UnitBase
{
	GENERATED_BODY()

public:
	USH_BTTask_FindCover();

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type Begin(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector RecentlySeenKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector LastSeenLocationKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector CoverLocationKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector PeekLocationKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector FrontLocationKey;

	/** 한 번에 옮겨 갈 거리 범위(cm). 너무 가까우면 제자리, 너무 멀면 오래 노출된다. */
	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "0.0"))
	float MinHopDistance = 250.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "100.0"))
	float MaxHopDistance = 1300.0f;

	/** 엄폐물 모서리에서 대각선으로 이만큼(cm) 나간 자리에 선다. 캡슐 반경(42)보다 커야 엄폐물에 끼지 않는다. */
	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "60.0"))
	float CornerOffset = 90.0f;

	/** 앞을 확인할 때 엄폐물 끝에서 옆으로 나가는 거리(cm). */
	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "0.0"))
	float PeekStep = 70.0f;

	/** 이동 거리 1 cm 당 점수 감점(전진 1 cm = +1점). */
	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "0.0"))
	float DistanceWeight = 0.3f;

	/** 이 거리(cm) 안에 다른 유닛이 있는 자리는 고르지 않는다(엄폐물 하나에 몰려 한꺼번에 맞지 않게). */
	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "0.0"))
	float CrowdRadius = 150.0f;

	/** 맞은 뒤 이 시간(초) 안이면 쏜 곳을 Front 로 본다. */
	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "0.0"))
	float ThreatMemorySeconds = 5.0f;

	/** 최근에 고른 자리 몇 개를 다시 고르지 않을지. 같은 두 엄폐물 사이를 왕복하지 않게. */
	UPROPERTY(EditAnywhere, Category = "SH|Cover", meta = (ClampMin = "0", ClampMax = "16"))
	int32 RecentMemory = 4;

private:
	TArray<FVector> RecentPicks;
};
