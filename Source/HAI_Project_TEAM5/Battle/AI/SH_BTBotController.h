// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "RL/SH_RLBotController.h"
#include "SH_BTBotController.generated.h"

class UBehaviorTree;

/**
 *  행동 트리로 싸우는 유닛의 컨트롤러. 팀을 "행동 트리" 로 고르면 전투 시작 때 보드가 이 컨트롤러로 바꿔 끼운다.
 *
 *  ★ ASH_RLBotController 를 상속한다 — 컨트롤 회전을 폰 방향으로 덮어쓰지 않게 하는 설정과
 *  몸통이 조준을 따라가는 설정을 그대로 물려받는다. 그래서 조준 · 몸통 회전이 RL 유닛과 같다.
 *
 *  트리는 전투 시작 때만 돈다(StartCombat). 배치 중 · 결과 화면 · 사망 후에는 멈춰 있다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BTBotController : public ASH_RLBotController
{
	GENERATED_BODY()

public:
	/** 트리를 처음부터 실행한다. 블랙보드는 트리에 지정된 것(BB_SHCombat)을 쓴다. */
	bool StartCombat(UBehaviorTree* Tree);

	/** 트리를 멈추고 이동 · 조준 의도를 지운다. */
	void StopCombat();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	// OnDeath 는 동적 델리게이트라 UFUNCTION 이어야 묶을 수 있다.
	UFUNCTION()
	void HandlePawnDeath(AActor* Victim, AActor* Causer);
};
