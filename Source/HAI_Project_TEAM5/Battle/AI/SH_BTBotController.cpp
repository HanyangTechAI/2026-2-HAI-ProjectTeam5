// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTBotController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BrainComponent.h"
#include "Combat/CombatStatsComponent.h"
#include "RL/SH_RLCharacter.h"

bool ASH_BTBotController::StartCombat(UBehaviorTree* Tree)
{
	if (!Tree)
	{
		return false;
	}
	if (!Tree->BlackboardAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BTBotController] %s 에 블랙보드가 지정되지 않았다(BB_SHCombat)."), *Tree->GetName());
		return false;
	}
	return RunBehaviorTree(Tree);
}

void ASH_BTBotController::StopCombat()
{
	if (BrainComponent)
	{
		BrainComponent->StopLogic(TEXT("SH combat stopped"));
	}
	if (ASH_RLCharacter* Unit = Cast<ASH_RLCharacter>(GetPawn()))
	{
		Unit->ClearPendingInput();   // 멈춘 뒤에도 마지막 방향으로 걷지 않게
	}
}

void ASH_BTBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (const ASH_RLCharacter* Unit = Cast<ASH_RLCharacter>(InPawn); Unit && Unit->CombatStats)
	{
		Unit->CombatStats->OnDeath.AddUniqueDynamic(this, &ThisClass::HandlePawnDeath);
	}
}

void ASH_BTBotController::OnUnPossess()
{
	if (const ASH_RLCharacter* Unit = Cast<ASH_RLCharacter>(GetPawn()); Unit && Unit->CombatStats)
	{
		Unit->CombatStats->OnDeath.RemoveDynamic(this, &ThisClass::HandlePawnDeath);
	}
	StopCombat();
	Super::OnUnPossess();
}

void ASH_BTBotController::HandlePawnDeath(AActor* /*Victim*/, AActor* /*Causer*/)
{
	// 죽은 유닛의 트리가 매 프레임 실패 → 재시작을 반복하지 않게 멈춘다. 다음 전투에서 StartCombat 이 다시 켠다.
	StopCombat();
}
