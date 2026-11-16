// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTTask_TurnToward.h"

#include "Battle/AI/SH_BotSteering.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "RL/SH_RLCharacter.h"

USH_BTTask_TurnToward::USH_BTTask_TurnToward()
{
	NodeName = TEXT("SH Turn Toward");
	LocationKey.SelectedKeyName = SHBot::Keys::ThreatLocation;
	LocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_TurnToward, LocationKey));
}

void USH_BTTask_TurnToward::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		LocationKey.ResolveSelectedKey(*Blackboard);
	}
}

FString USH_BTTask_TurnToward::GetStaticDescription() const
{
	return FString::Printf(TEXT("바라볼 곳: %s\n허용 %.0f° · 제한 %.1f초"),
		*LocationKey.SelectedKeyName.ToString(), AcceptAngleDeg, TimeoutSeconds);
}

EBTNodeResult::Type USH_BTTask_TurnToward::Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, const float ElapsedSeconds)
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		return EBTNodeResult::Failed;
	}
	const float Error = SHBot::YawErrorTo(Unit, Blackboard->GetValueAsVector(LocationKey.SelectedKeyName));
	if (FMath::Abs(Error) <= AcceptAngleDeg)
	{
		return EBTNodeResult::Succeeded;
	}
	if (ElapsedSeconds >= TimeoutSeconds)
	{
		return EBTNodeResult::Failed;
	}
	Unit.PendingMoveInput = FVector::ZeroVector;
	Unit.PendingTurnRateDeg = SHBot::TurnRateFor(Unit, Error);
	return EBTNodeResult::InProgress;
}
