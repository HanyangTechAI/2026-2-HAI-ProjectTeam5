// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTTask_FindCover.h"

#include "Battle/AI/SH_BotSteering.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Engine/World.h"
#include "RL/SH_Arena.h"
#include "RL/SH_RLCharacter.h"

USH_BTTask_FindCover::USH_BTTask_FindCover()
{
	NodeName = TEXT("SH Find Cover");
	bNotifyTick = false;   // 즉시 끝나는 태스크다(Begin 에서 결과를 돌려준다)

	using namespace SHBot::Keys;
	RecentlySeenKey.SelectedKeyName = RecentlySeen;
	RecentlySeenKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_FindCover, RecentlySeenKey));
	LastSeenLocationKey.SelectedKeyName = LastSeenLocation;
	LastSeenLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_FindCover, LastSeenLocationKey));
	CoverLocationKey.SelectedKeyName = CoverLocation;
	CoverLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_FindCover, CoverLocationKey));
	PeekLocationKey.SelectedKeyName = PeekLocation;
	PeekLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_FindCover, PeekLocationKey));
	FrontLocationKey.SelectedKeyName = FrontLocation;
	FrontLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_FindCover, FrontLocationKey));
}

void USH_BTTask_FindCover::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		for (FBlackboardKeySelector* Key : { &RecentlySeenKey, &LastSeenLocationKey, &CoverLocationKey, &PeekLocationKey, &FrontLocationKey })
		{
			Key->ResolveSelectedKey(*Blackboard);
		}
	}
}

FString USH_BTTask_FindCover::GetStaticDescription() const
{
	return FString::Printf(TEXT("엄폐물 끝 고르기 %.0f~%.0f cm\n→ %s / %s / %s"), MinHopDistance, MaxHopDistance,
		*CoverLocationKey.SelectedKeyName.ToString(), *PeekLocationKey.SelectedKeyName.ToString(),
		*FrontLocationKey.SelectedKeyName.ToString());
}

EBTNodeResult::Type USH_BTTask_FindCover::Begin(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const UWorld* World = OwnerComp.GetWorld();
	const ASH_Arena* Arena = SHBot::GetArena(Unit);
	if (!Blackboard || !World || !Arena)
	{
		return EBTNodeResult::Failed;
	}
	const FVector Self = Unit.GetActorLocation();

	// --- 1. 적이 있다고 보는 곳 --------------------------------------------------
	FVector Front;
	if (Blackboard->GetValueAsBool(RecentlySeenKey.SelectedKeyName))
	{
		Front = Blackboard->GetValueAsVector(LastSeenLocationKey.SelectedKeyName);
	}
	else if (World->GetTimeSeconds() - Unit.LastHitTime <= ThreatMemorySeconds)
	{
		Front = Unit.LastHitSourceLocation;
	}
	else
	{
		Front = SHBot::EnemySidePoint(Unit);
	}
	Front.Z = Self.Z;

	// --- 2. 후보 채점 ------------------------------------------------------------
	const double SafeRadius = Arena->GetZoneRadius() - 150.0;   // 구역 밖 자리는 고르지 않는다
	const FVector ArenaCenter = Arena->GetActorLocation();
	const double SelfToFront = FVector::Dist2D(Self, Front);

	const SHBot::FCoverCorner* Best = nullptr;
	double BestScore = -TNumericLimits<double>::Max();
	const TArray<SHBot::FCoverCorner> Corners = SHBot::GatherCoverCorners(*Arena, CornerOffset, Self.Z);
	for (const SHBot::FCoverCorner& Corner : Corners)
	{
		const double Hop = FVector::Dist2D(Self, Corner.Location);
		if (Hop < MinHopDistance || Hop > MaxHopDistance)
		{
			continue;
		}
		if (FVector::Dist2D(ArenaCenter, Corner.Location) > SafeRadius)
		{
			continue;
		}
		const bool bRecent = RecentPicks.ContainsByPredicate(
			[&Corner](const FVector& Picked) { return FVector::Dist2D(Picked, Corner.Location) < 50.0; });
		if (bRecent)
		{
			continue;
		}
		// 싼 판정부터 — 가려지는지(라인 1개) → 비어 있는지(오버랩) → 갈 수 있는지(스윕).
		if (!SHBot::IsLineBlockedByStatic(*World, Front, Corner.Location))
		{
			continue;   // 적 쪽에서 훤히 보이는 자리 = 엄폐가 아니다
		}
		if (SHBot::IsSpotBlocked(*World, Corner.Location)
			|| SHBot::IsSpotCrowded(*World, Corner.Location, CrowdRadius, &Unit))
		{
			continue;
		}
		if (SHBot::IsPathBlocked(*World, Self, Corner.Location))
		{
			continue;   // 직선으로 못 간다(NavMesh 가 없다) — 막히면 벽에 비비게 된다
		}

		const double Progress = SelfToFront - FVector::Dist2D(Corner.Location, Front);   // +면 적 쪽으로 다가간다
		const double Score = Progress - Hop * DistanceWeight;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = &Corner;
		}
	}
	if (!Best)
	{
		return EBTNodeResult::Failed;   // 갈 엄폐물이 없다 — 트리의 다음 가지(Search)가 맡는다
	}

	// --- 3. 앞을 확인할 자리: 엄폐물 끝에서 Front 와 수직인 방향(엄폐물 바깥쪽)으로 한 걸음 -------------
	const FVector ToFront = (Front - Best->Location).GetSafeNormal2D();
	FVector Side = Best->Outward - ToFront * FVector::DotProduct(Best->Outward, ToFront);
	Side = Side.GetSafeNormal2D();
	FVector Peek = Best->Location + Side * PeekStep;
	if (Side.IsNearlyZero() || SHBot::IsSpotBlocked(*World, Peek))
	{
		Peek = Best->Location;   // 옆으로 나갈 자리가 없으면 그 자리에서 본다
	}

	Blackboard->SetValueAsVector(CoverLocationKey.SelectedKeyName, Best->Location);
	Blackboard->SetValueAsVector(PeekLocationKey.SelectedKeyName, Peek);
	Blackboard->SetValueAsVector(FrontLocationKey.SelectedKeyName, Front);

	RecentPicks.Add(Best->Location);
	while (RecentPicks.Num() > RecentMemory)
	{
		RecentPicks.RemoveAt(0);
	}
	return EBTNodeResult::Succeeded;
}
