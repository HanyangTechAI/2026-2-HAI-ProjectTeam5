// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTService_Perceive.h"

#include "AIController.h"
#include "Battle/AI/SH_BotSteering.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/World.h"
#include "RL/SH_Arena.h"
#include "RL/SH_RLCharacter.h"
#include "RL/SH_VisionComponent.h"

USH_BTService_Perceive::USH_BTService_Perceive()
{
	NodeName = TEXT("SH Perceive");
	Interval = SHBot::DecisionPeriod;     // 정책과 같은 20 Hz
	RandomDeviation = 0.0f;
	bCallTickOnSearchStart = true;        // 트리가 자식을 고르기 전에 한 번 채운다
	bNotifyBecomeRelevant = true;
	bCreateNodeInstance = true;           // LastSeenTime 을 유닛마다 따로 가진다

	using namespace SHBot::Keys;
	TargetKey.SelectedKeyName = TargetActor;
	TargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, TargetKey), AActor::StaticClass());
	HasTargetKey.SelectedKeyName = HasTarget;
	HasTargetKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, HasTargetKey));
	RecentlySeenKey.SelectedKeyName = RecentlySeen;
	RecentlySeenKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, RecentlySeenKey));
	LastSeenLocationKey.SelectedKeyName = LastSeenLocation;
	LastSeenLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, LastSeenLocationKey));
	UnderFireKey.SelectedKeyName = UnderFire;
	UnderFireKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, UnderFireKey));
	ThreatLocationKey.SelectedKeyName = ThreatLocation;
	ThreatLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, ThreatLocationKey));
	OutsideZoneKey.SelectedKeyName = OutsideZone;
	OutsideZoneKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, OutsideZoneKey));
	ZoneCenterKey.SelectedKeyName = ZoneCenter;
	ZoneCenterKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTService_Perceive, ZoneCenterKey));
}

void USH_BTService_Perceive::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		for (FBlackboardKeySelector* Key : { &TargetKey, &HasTargetKey, &RecentlySeenKey, &LastSeenLocationKey,
		                                     &UnderFireKey, &ThreatLocationKey, &OutsideZoneKey, &ZoneCenterKey })
		{
			Key->ResolveSelectedKey(*Blackboard);
		}
	}
}

FString USH_BTService_Perceive::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s\n놓친 적 추적 %.1f초 · 피격 반응 %.1f초 · 구역 여유 %.0f cm"),
		*Super::GetStaticDescription(), RecentSeconds, UnderFireSeconds, ZoneMargin);
}

void USH_BTService_Perceive::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	// 전투마다 트리가 다시 시작된다 — 지난 전투에서 본 적의 기억이 새지 않게 지운다.
	LastSeenTime = -1.0f;
	bLastSeenChecked = true;
}

void USH_BTService_Perceive::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, const float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	const AAIController* Controller = OwnerComp.GetAIOwner();
	ASH_RLCharacter* Unit = Controller ? Cast<ASH_RLCharacter>(Controller->GetPawn()) : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const UWorld* World = OwnerComp.GetWorld();
	if (!Unit || !Blackboard || !World || !Unit->Vision)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const FVector Self = Unit->GetActorLocation();

	// --- 적 --------------------------------------------------------------------
	Unit->Vision->Scan();   // 같은 프레임에 이미 스캔했으면 트레이스를 다시 돌리지 않는다
	AActor* Target = nullptr;
	for (const FSHVisibleEnemy& Enemy : Unit->Vision->GetVisibleEnemies())   // 가까운 순
	{
		if (AActor* Actor = Enemy.Actor.Get())
		{
			Target = Actor;
			break;
		}
	}

	if (Target)
	{
		Blackboard->SetValueAsObject(TargetKey.SelectedKeyName, Target);
		Blackboard->SetValueAsVector(LastSeenLocationKey.SelectedKeyName, Target->GetActorLocation());
		LastSeenTime = Now;
		bLastSeenChecked = false;
	}
	else
	{
		Blackboard->ClearValue(TargetKey.SelectedKeyName);
		if (!bLastSeenChecked
			&& FVector::Dist2D(Self, Blackboard->GetValueAsVector(LastSeenLocationKey.SelectedKeyName)) <= CheckedRadius)
		{
			bLastSeenChecked = true;   // 가 봤는데 없다 — 더 쫓지 않는다
		}
	}
	Blackboard->SetValueAsBool(HasTargetKey.SelectedKeyName, Target != nullptr);
	Blackboard->SetValueAsBool(RecentlySeenKey.SelectedKeyName,
		!Target && !bLastSeenChecked && LastSeenTime >= 0.0f && Now - LastSeenTime <= RecentSeconds);

	// --- 맞은 방향 -------------------------------------------------------------
	Blackboard->SetValueAsBool(UnderFireKey.SelectedKeyName, !Target && Now - Unit->LastHitTime <= UnderFireSeconds);
	Blackboard->SetValueAsVector(ThreatLocationKey.SelectedKeyName, Unit->LastHitSourceLocation);

	// --- 구역 ------------------------------------------------------------------
	if (const ASH_Arena* Arena = SHBot::GetArena(*Unit))
	{
		const FVector Center = Arena->GetActorLocation();
		Blackboard->SetValueAsVector(ZoneCenterKey.SelectedKeyName, Center);
		Blackboard->SetValueAsBool(OutsideZoneKey.SelectedKeyName,
			FVector::Dist2D(Self, Center) > Arena->GetZoneRadius() - ZoneMargin);
	}
}
