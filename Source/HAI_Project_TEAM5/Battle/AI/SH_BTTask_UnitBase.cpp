// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTTask_UnitBase.h"

#include "AIController.h"
#include "Battle/AI/SH_BotSteering.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "RL/SH_RLCharacter.h"

USH_BTTask_UnitBase::USH_BTTask_UnitBase()
{
	bNotifyTick = true;
	bCreateNodeInstance = true;
}

ASH_RLCharacter* USH_BTTask_UnitBase::GetUnit(const UBehaviorTreeComponent& OwnerComp)
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	return Controller ? Cast<ASH_RLCharacter>(Controller->GetPawn()) : nullptr;
}

EBTNodeResult::Type USH_BTTask_UnitBase::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	ASH_RLCharacter* Unit = GetUnit(OwnerComp);
	if (!Unit || !SHBot::IsAlive(*Unit))
	{
		return EBTNodeResult::Failed;
	}

	TaskElapsedSeconds = 0.0f;
	DecisionAccumulator = SHBot::DecisionPeriod;   // 첫 Tick 에서 바로 결정한다
	const EBTNodeResult::Type Result = Begin(OwnerComp, *Unit);
	if (Result != EBTNodeResult::InProgress)
	{
		SHBot::Stop(*Unit);
	}
	return Result;
}

void USH_BTTask_UnitBase::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/, const float DeltaSeconds)
{
	ASH_RLCharacter* Unit = GetUnit(OwnerComp);
	if (!Unit || !SHBot::IsAlive(*Unit))
	{
		if (Unit)
		{
			SHBot::Stop(*Unit);
		}
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	TaskElapsedSeconds += DeltaSeconds;
	DecisionAccumulator += DeltaSeconds;
	if (DecisionAccumulator < SHBot::DecisionPeriod)
	{
		return;   // 결정 사이에는 지난 의도를 유닛 Tick 이 계속 적용한다
	}
	// 한 프레임이 길어도 결정은 한 번만 — 밀린 결정을 몰아서 하지 않는다(정책도 프레임당 한 번).
	DecisionAccumulator = FMath::Fmod(DecisionAccumulator, SHBot::DecisionPeriod);

	const EBTNodeResult::Type Result = Decide(OwnerComp, *Unit, TaskElapsedSeconds);
	if (Result != EBTNodeResult::InProgress)
	{
		SHBot::Stop(*Unit);
		FinishLatentTask(OwnerComp, Result);
	}
}

EBTNodeResult::Type USH_BTTask_UnitBase::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* /*NodeMemory*/)
{
	// 더 높은 우선순위 분기(예: 적 발견)가 끼어들었다. 의도를 지워야 새 태스크가 깨끗하게 시작한다.
	if (ASH_RLCharacter* Unit = GetUnit(OwnerComp))
	{
		SHBot::Stop(*Unit);
	}
	return EBTNodeResult::Aborted;
}

EBTNodeResult::Type USH_BTTask_UnitBase::Begin(UBehaviorTreeComponent& /*OwnerComp*/, ASH_RLCharacter& /*Unit*/)
{
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type USH_BTTask_UnitBase::Decide(UBehaviorTreeComponent& /*OwnerComp*/, ASH_RLCharacter& /*Unit*/, float /*ElapsedSeconds*/)
{
	return EBTNodeResult::Succeeded;   // 하위 클래스가 덮어쓴다
}
