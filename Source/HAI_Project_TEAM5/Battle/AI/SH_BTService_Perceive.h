// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "SH_BTService_Perceive.generated.h"

/**
 *  20 Hz 로 유닛의 시야를 스캔해 블랙보드를 채운다. 트리 맨 위 Selector 에 붙인다.
 *
 *  ★ 정책과 **같은 감각**만 쓴다:
 *    - 적: USH_VisionComponent::Scan → GetVisibleEnemies (90° 시야각 · 사거리 · 벽/엄폐물/아군 가림 판정)
 *    - 맞은 방향: ASH_RLCharacter::LastHitSourceLocation · LastHitTime (정책의 피격 기억 관측과 같은 값)
 *    - 구역: 아레나의 GetZoneRadius (정책의 구역 관측과 같은 값)
 *  벽 뒤 적 위치 같은 "정책이 못 보는 정보" 는 쓰지 않는다.
 *
 *  채우는 키: TargetActor · HasTarget · RecentlySeen · LastSeenLocation · UnderFire · ThreatLocation · OutsideZone · ZoneCenter
 */
UCLASS(meta = (DisplayName = "SH Perceive"))
class HAI_PROJECT_TEAM5_API USH_BTService_Perceive : public UBTService
{
	GENERATED_BODY()

public:
	USH_BTService_Perceive();

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual FString GetStaticDescription() const override;

protected:
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector TargetKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector HasTargetKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector RecentlySeenKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector LastSeenLocationKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector UnderFireKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector ThreatLocationKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector OutsideZoneKey;
	UPROPERTY(EditAnywhere, Category = "Blackboard") FBlackboardKeySelector ZoneCenterKey;

	/** 적을 놓친 뒤 이 시간(초) 동안은 마지막으로 본 곳을 쫓는다(RecentlySeen). */
	UPROPERTY(EditAnywhere, Category = "SH|Perceive", meta = (ClampMin = "0.0"))
	float RecentSeconds = 4.0f;

	/** 마지막으로 본 곳에 이만큼(cm) 다가가면 "확인했다" 로 보고 RecentlySeen 을 끈다. */
	UPROPERTY(EditAnywhere, Category = "SH|Perceive", meta = (ClampMin = "0.0"))
	float CheckedRadius = 200.0f;

	/** 맞은 뒤 이 시간(초) 동안 UnderFire. */
	UPROPERTY(EditAnywhere, Category = "SH|Perceive", meta = (ClampMin = "0.0"))
	float UnderFireSeconds = 1.5f;

	/** 구역 경계에서 이만큼(cm) 안쪽부터 "밖" 으로 본다 — 경계에 딱 붙어 들락날락하지 않게. */
	UPROPERTY(EditAnywhere, Category = "SH|Perceive", meta = (ClampMin = "0.0"))
	float ZoneMargin = 150.0f;

private:
	float LastSeenTime = -1.0f;
	bool bLastSeenChecked = true;
};
