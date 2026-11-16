// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/AI/SH_BTTask_UnitBase.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "SH_BTTask_MoveToward.generated.h"

/**
 *  블랙보드의 위치(마지막으로 본 적 · 구역 중심 등)로 곧장 걸어간다. 가는 쪽을 바라본다(시야가 앞을 본다).
 *  도착하면 성공, 시간이 다 되거나 엄폐물에 막혀 거의 못 움직이면 실패.
 *
 *  ★ NavMesh 를 쓰지 않는다(이 프로젝트에는 없다 — 학습도 직선 이동만 한다). 그래서 막힘 판정이 필요하다.
 */
UCLASS(meta = (DisplayName = "SH Move Toward"))
class HAI_PROJECT_TEAM5_API USH_BTTask_MoveToward : public USH_BTTask_UnitBase
{
	GENERATED_BODY()

public:
	USH_BTTask_MoveToward();

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type Begin(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit) override;
	virtual EBTNodeResult::Type Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, float ElapsedSeconds) override;

	/** 갈 곳(Vector 키). 기본은 LastSeenLocation. 구역 복귀에 쓸 때는 ZoneCenter 로 바꾼다. */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector LocationKey;

	/** 이 거리(cm) 안이면 도착. */
	UPROPERTY(EditAnywhere, Category = "SH|Move", meta = (ClampMin = "10.0"))
	float AcceptRadius = 150.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Move", meta = (ClampMin = "0.1"))
	float TimeoutSeconds = 4.0f;

	/** 이 시간(초) 동안 StuckDistance 도 못 움직이면 막힌 것으로 보고 실패한다. */
	UPROPERTY(EditAnywhere, Category = "SH|Move", meta = (ClampMin = "0.1"))
	float StuckCheckSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Move", meta = (ClampMin = "0.0"))
	float StuckDistance = 40.0f;

private:
	FVector CheckpointLocation = FVector::ZeroVector;
	float CheckpointTime = 0.0f;
};
