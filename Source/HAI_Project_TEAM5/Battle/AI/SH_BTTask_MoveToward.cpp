// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTTask_MoveToward.h"

#include "Battle/AI/SH_BotSteering.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "RL/SH_RLCharacter.h"

USH_BTTask_MoveToward::USH_BTTask_MoveToward()
{
	NodeName = TEXT("SH Move Toward");
	LocationKey.SelectedKeyName = SHBot::Keys::LastSeenLocation;
	LocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_MoveToward, LocationKey));
}

void USH_BTTask_MoveToward::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		LocationKey.ResolveSelectedKey(*Blackboard);
	}
}

FString USH_BTTask_MoveToward::GetStaticDescription() const
{
	return FString::Printf(TEXT("목표: %s\n도착 %.0f cm · 제한 %.1f초"),
		*LocationKey.SelectedKeyName.ToString(), AcceptRadius, TimeoutSeconds);
}

EBTNodeResult::Type USH_BTTask_MoveToward::Begin(UBehaviorTreeComponent& /*OwnerComp*/, ASH_RLCharacter& Unit)
{
	CheckpointLocation = Unit.GetActorLocation();
	CheckpointTime = 0.0f;
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type USH_BTTask_MoveToward::Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, const float ElapsedSeconds)
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		return EBTNodeResult::Failed;
	}
	const FVector Goal = Blackboard->GetValueAsVector(LocationKey.SelectedKeyName);
	if (FVector::Dist2D(Unit.GetActorLocation(), Goal) <= AcceptRadius)
	{
		return EBTNodeResult::Succeeded;
	}
	if (ElapsedSeconds >= TimeoutSeconds)
	{
		return EBTNodeResult::Failed;
	}

	// 막힘 판정 — 엄폐물에 정면으로 붙으면 이동 입력은 있는데 위치가 그대로다.
	if (ElapsedSeconds - CheckpointTime >= StuckCheckSeconds)
	{
		if (FVector::Dist2D(Unit.GetActorLocation(), CheckpointLocation) < StuckDistance)
		{
			return EBTNodeResult::Failed;
		}
		CheckpointLocation = Unit.GetActorLocation();
		CheckpointTime = ElapsedSeconds;
	}

	Unit.PendingMoveInput = SHBot::FlatDirectionTo(Unit, Goal);
	Unit.PendingTurnRateDeg = SHBot::TurnRateFor(Unit, SHBot::YawErrorTo(Unit, Goal));
	return EBTNodeResult::InProgress;
}
