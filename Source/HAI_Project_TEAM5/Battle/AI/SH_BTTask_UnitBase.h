// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "SH_BTTask_UnitBase.generated.h"

class ASH_RLCharacter;
class UBlackboardComponent;

/**
 *  게임 유닛 행동 트리 태스크의 공통 뼈대(템플릿 메서드 패턴).
 *
 *  ExecuteTask · TickTask · AbortTask 의 흐름은 여기서 한 번만 정한다.
 *    - 결정은 SHBot::DecisionPeriod(20 Hz)마다 한 번 — 정책과 같은 판단 빈도.
 *    - 유닛이 없거나 죽었으면 실패로 끝낸다.
 *    - 끝나거나 중단되면 이동 · 조준 의도를 지운다.
 *  하위 클래스는 Begin(선택)과 Decide 만 구현한다.
 *
 *  ★ bCreateNodeInstance = true: 유닛마다 노드 인스턴스가 따로 생겨 멤버 변수에 상태를 둘 수 있다.
 *  (기본값 false 면 모든 유닛이 노드 하나를 공유하므로 상태는 NodeMemory 에 둬야 한다.)
 */
UCLASS(Abstract)
class HAI_PROJECT_TEAM5_API USH_BTTask_UnitBase : public UBTTaskNode
{
	GENERATED_BODY()

public:
	USH_BTTask_UnitBase();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/** 태스크 시작 때 한 번. InProgress 가 아니면 바로 그 결과로 끝난다. */
	virtual EBTNodeResult::Type Begin(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit);

	/**
	 *  결정 주기마다 한 번. 유닛의 PendingMoveInput · PendingTurnRateDeg 를 정하고(필요하면 사격),
	 *  계속하려면 InProgress, 끝내려면 Succeeded / Failed 를 돌려준다.
	 *  ElapsedSeconds = 이 태스크가 시작된 뒤 흐른 게임 시간.
	 */
	virtual EBTNodeResult::Type Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, float ElapsedSeconds);

	[[nodiscard]] static ASH_RLCharacter* GetUnit(const UBehaviorTreeComponent& OwnerComp);

private:
	// ★ 이름을 Decide 의 매개변수(ElapsedSeconds)와 다르게 둔다. 같으면 하위 클래스에서 C4458(멤버를 가림)이
	//   나고 이 프로젝트는 경고를 에러로 처리한다 — private 멤버라도 이름 가림 경고는 난다.
	float DecisionAccumulator = 0.0f;
	float TaskElapsedSeconds = 0.0f;
};
