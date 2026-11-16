// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/AI/SH_BTTask_UnitBase.h"
#include "SH_BTTask_Search.generated.h"

/**
 *  아는 적이 없을 때 — 시야를 좌우로 훑으며 아레나 반대편으로 걸어간다. Duration 이 지나면 성공(트리가 다시 고른다).
 *
 *  ★ 스크립트 봇에서 실측으로 확인된 두 함정을 피한다(ASH_ScriptBotController 주석):
 *   - 모두 같은 방향 · 같은 속도로 돌면 서로를 영영 못 본다 → 유닛마다 방향과 속도를 어긋나게 한다.
 *   - 아레나 중심으로 가면 엄폐물 고리에 모여 교착한다 → 자기 위치의 대척점(반대편)으로 간다.
 */
UCLASS(meta = (DisplayName = "SH Search"))
class HAI_PROJECT_TEAM5_API USH_BTTask_Search : public USH_BTTask_UnitBase
{
	GENERATED_BODY()

public:
	USH_BTTask_Search();

	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type Begin(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit) override;
	virtual EBTNodeResult::Type Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, float ElapsedSeconds) override;

	UPROPERTY(EditAnywhere, Category = "SH|Search", meta = (ClampMin = "0.1"))
	float DurationSeconds = 2.0f;

	/** 훑는 회전 속도(도/초). 유닛마다 ±Desync 만큼 다르다. MaxTurnRateDeg 를 넘지 않는다. */
	UPROPERTY(EditAnywhere, Category = "SH|Search", meta = (ClampMin = "1.0"))
	float SweepRateDeg = 120.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Search", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float SweepDesync = 0.35f;

	UPROPERTY(EditAnywhere, Category = "SH|Search")
	bool bAdvance = true;

	/**
	 *  막힘 판정: StuckCheckSeconds 동안 StuckDistance 도 못 움직였으면 DetourSeconds 동안
	 *  막힌 방향의 옆으로 돌아간다(회전도 멈춘다). ★ 이게 없으면 벽 · 엄폐물에 비비면서 제자리에서 돌기만 한다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Search", meta = (ClampMin = "0.1"))
	float StuckCheckSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Search", meta = (ClampMin = "0.0"))
	float StuckDistance = 40.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Search", meta = (ClampMin = "0.1"))
	float DetourSeconds = 1.5f;

private:
	float SweepSign = 1.0f;
	float SweepScale = 1.0f;

	FVector CheckpointLocation = FVector::ZeroVector;
	float CheckpointTime = 0.0f;
	FVector DetourDirection = FVector::ZeroVector;
	float DetourUntil = -1.0f;
	float DetourSign = 1.0f;
};
