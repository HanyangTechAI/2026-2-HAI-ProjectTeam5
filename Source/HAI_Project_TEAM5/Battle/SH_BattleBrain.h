// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SH_BattleBrain.generated.h"

class ASH_RLCharacter;
class ULearningAgentsManager;
class ULearningAgentsPolicy;
class USH_ShooterInteractor;

/**
 *  학습된 정책으로 유닛을 조종한다. 학습기 · 크리틱 · 파이썬 없이 추론만 한다
 *  (ASH_CombatTrainer 평가 모드의 조립 순서와 같다).
 *
 *  전투마다 새로 스폰하고 끝나면 Destroy 한다. 배치가 바뀔 때 에이전트를 하나씩 지우는 코드가 필요 없다.
 *
 *  ★ UObject 멤버에 UPROPERTY 를 빼면 컴파일은 되지만 다음 GC 때 지워질 수 있다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattleBrain : public AActor
{
	GENERATED_BODY()

public:
	ASH_BattleBrain();

	/** 유닛을 에이전트로 등록하고 정책을 불러온다. 실패하면 false — 이유는 로그에.
	 *  @param SnapshotDirectory 절대 경로 또는 프로젝트 폴더 기준 상대 경로. */
	[[nodiscard]] bool Assemble(const TArray<ASH_RLCharacter*>& Units, const FString& SnapshotDirectory);

	/** 정책 메모리를 비우고 추론을 시작한다. */
	void StartThinking();
	void StopThinking() { bThinking = false; }

	virtual void Tick(float DeltaSeconds) override;

private:
	[[nodiscard]] bool LoadSnapshot(const FString& Directory);

	UPROPERTY(VisibleAnywhere, Category = "SH|Battle")
	TObjectPtr<ULearningAgentsManager> Manager;

	UPROPERTY()
	TObjectPtr<USH_ShooterInteractor> Interactor;

	UPROPERTY()
	TObjectPtr<ULearningAgentsPolicy> Policy;

	TArray<int32> AgentIds;
	float DecisionAccumulator = 0.0f;
	bool bThinking = false;

	static constexpr float DecisionPeriod = 1.0f / 20.0f;   // 학습과 같은 20 Hz
};
