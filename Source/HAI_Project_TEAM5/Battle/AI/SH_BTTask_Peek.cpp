// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTTask_Peek.h"

#include "Battle/AI/SH_BotSteering.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "RL/SH_RLCharacter.h"

USH_BTTask_Peek::USH_BTTask_Peek()
{
	NodeName = TEXT("SH Peek");
	PeekLocationKey.SelectedKeyName = SHBot::Keys::PeekLocation;
	PeekLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_Peek, PeekLocationKey));
	FrontLocationKey.SelectedKeyName = SHBot::Keys::FrontLocation;
	FrontLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_Peek, FrontLocationKey));
}

void USH_BTTask_Peek::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		PeekLocationKey.ResolveSelectedKey(*Blackboard);
		FrontLocationKey.ResolveSelectedKey(*Blackboard);
	}
}

FString USH_BTTask_Peek::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s 로 나가 %s 쪽을 %.1f초 확인 (±%.0f°)"),
		*PeekLocationKey.SelectedKeyName.ToString(), *FrontLocationKey.SelectedKeyName.ToString(), HoldSeconds, SweepHalfAngle);
}

EBTNodeResult::Type USH_BTTask_Peek::Begin(UBehaviorTreeComponent& /*OwnerComp*/, ASH_RLCharacter& /*Unit*/)
{
	HoldStartTime = -1.0f;
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type USH_BTTask_Peek::Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, const float ElapsedSeconds)
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		return EBTNodeResult::Failed;
	}
	const FVector Peek = Blackboard->GetValueAsVector(PeekLocationKey.SelectedKeyName);
	const FVector Front = Blackboard->GetValueAsVector(FrontLocationKey.SelectedKeyName);

	// --- 1. 옆으로 나간다(앞을 보면서) -----------------------------------------
	if (HoldStartTime < 0.0f)
	{
		const bool bArrived = FVector::Dist2D(Unit.GetActorLocation(), Peek) <= AcceptRadius;
		if (!bArrived && ElapsedSeconds < StepOutSeconds)
		{
			Unit.PendingMoveInput = SHBot::FlatDirectionTo(Unit, Peek);
			Unit.PendingTurnRateDeg = SHBot::TurnRateFor(Unit, SHBot::YawErrorTo(Unit, Front));
			return EBTNodeResult::InProgress;
		}
		HoldStartTime = ElapsedSeconds;   // 도착했거나 시간이 다 됐다 — 그 자리에서 본다
	}

	// --- 2. 앞쪽을 좌우로 훑는다 ------------------------------------------------
	const float Held = ElapsedSeconds - HoldStartTime;
	if (Held >= HoldSeconds)
	{
		return EBTNodeResult::Succeeded;   // 아무도 없었다 — 안전
	}
	// 앞 → 왼쪽 → 앞 → 오른쪽 → 앞: 한 번의 사인 주기. 목표 각을 따라 돌되 회전 상한은 그대로다.
	const float Offset = SweepHalfAngle * FMath::Sin(2.0f * PI * Held / HoldSeconds);
	const float Error = FMath::FindDeltaAngleDegrees(0.0f, SHBot::YawErrorTo(Unit, Front) + Offset);
	Unit.PendingMoveInput = FVector::ZeroVector;
	Unit.PendingTurnRateDeg = SHBot::TurnRateFor(Unit, Error);
	return EBTNodeResult::InProgress;
}
