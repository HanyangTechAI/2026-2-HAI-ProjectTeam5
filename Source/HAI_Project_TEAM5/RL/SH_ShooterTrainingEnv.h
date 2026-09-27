// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "SH_ShooterTrainingEnv.generated.h"

class ALearningAgentsGymBase;
class ASH_RLCharacter;

/**
 *  보상 · 완료 · 리셋.
 *
 *  ★ MinimumAdvantage = 0.0 (ppo.py:130) 이라 **음수 advantage 가 전부 0으로 잘린다.**
 *  즉 벌점은 행동을 직접 억제하지 못한다 — "덜 좋은 것" 을 만들 뿐이다.
 *  그래서 원하는 행동은 반드시 **양의 보상**으로 끌어야 한다.
 *  시야 제한 + 엄폐물은 보상이 희소해지는 방향이라 이 문제가 정면으로 나온다.
 *
 *  ★ 셰이핑 항 두 개(SeeWeight / AlignWeight)는 감쇠시킨다.
 *  감쇠하지 않으면 "적을 화면에 두기만 하고 쏘지 않는" 정책이 나온다 —
 *  보기만 해도 보상이 들어오는데 쏘면 빗맞힐 위험이 있으니 합리적인 선택이 된다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API USH_ShooterTrainingEnv : public ULearningAgentsTrainingEnvironment
{
	GENERATED_BODY()

public:
	//~ 주 보상 ---------------------------------------------------------------

	/** 가한 데미지 100 당 보상. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float DamageDealtWeight = 1.0f;

	/** 받은 데미지 100 당 벌점. 가한 것보다 작게 둔다 — 같으면 "아무것도 안 하기" 가
	 *  기대값 0 으로 최적이 된다. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float DamageTakenWeight = 0.7f;

	/** 빗맞힌 발당 벌점. 무한 난사를 막는다. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float MissWeight = 0.05f;

	/** 킬 보너스. 오버킬이 보상에 안 잡히므로 마무리에 값을 붙여 준다. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float KillBonus = 0.5f;

	/** 사망 벌점. 한 번만 들어간다 (bDiedThisInterval). */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float DeathPenalty = 1.0f;

	/** 결정마다 들어가는 시간 벌점. 교착을 깬다. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float TimePenalty = 0.002f;

	//~ 셰이핑 (감쇠) ----------------------------------------------------------

	/** 적을 시야에 넣고 있는 것 자체의 보상. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward|Shaping")
	float SeeWeight = 0.02f;

	/** 적을 조준선 중앙에 둘수록 주는 보상. 정렬도의 4제곱을 곱한다 —
	 *  대충 보는 것으로는 거의 못 받고 정확히 겨눌 때만 받는다. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward|Shaping")
	float AlignWeight = 0.05f;

	/**
	 *  셰이핑이 0 으로 감쇠하는 데 걸리는 결정 스텝 수 (에이전트당).
	 *
	 *  ★ 0 이면 감쇠하지 않는다 — 권장하지 않는다. 헤더 위 주석의 실패 모드를 참조.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Reward|Shaping", meta = (ClampMin = "0"))
	int32 ShapingDecaySteps = 400000;

	//~ 완료 ------------------------------------------------------------------

	/**
	 *  죽었을 때 Termination 을 낼지.
	 *
	 *  ★ Termination 은 "이후 모든 보상이 0" 이라는 뜻이고, Truncation 은
	 *  "에피소드는 끊겼지만 크리틱이 나머지를 추정하라" 는 뜻이다 (ppo.py 가 gamma*V(s')
	 *  항을 Termination 에서만 0 으로 만든다). 사망은 진짜로 이후 보상이 없으므로
	 *  Termination 이 맞다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Completion")
	bool bTerminateOnDeath = true;

	/**
	 *  에피소드가 끝났을 때 아레나 **전체**를 리셋할지.
	 *
	 *  ★ 켜는 것이 기본이다. 유닛 하나만 되살리면 팀 대칭이 깨진다 — 한 명이 만피로
	 *  부활하고 나머지는 깎인 채로 계속 싸우게 되어, 정책이 "죽고 부활하는 게 이득" 을
	 *  학습할 수 있다.
	 *
	 *  ★ 다만 bTerminateOnDeath 와 같이 켜면 **아레나 안 누군가의 첫 사망이 20명 전원의
	 *  에피소드를 끝낸다.** 짧고 촘촘한 에피소드가 되므로 조준 학습 단계에는 맞지만,
	 *  "10v10 을 끝까지" 같은 긴 전투를 배우게 하려면 bTerminateOnDeath 를 끄고
	 *  MaxEpisodeStepNum 으로 길이를 정해야 한다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Completion")
	bool bResetWholeGym = true;

	//~ ULearningAgentsTrainingEnvironment ------------------------------------

	virtual void GatherAgentReward_Implementation(float& OutReward, const int32 AgentId) override;
	virtual void GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;
	virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;

	/** 현재 셰이핑 배율(1 → 0). 로그에 찍어 감쇠가 도는지 확인한다. */
	UFUNCTION(BlueprintPure, Category = "SH|Reward")
	float GetShapingScale() const;

protected:
	ASH_RLCharacter* ResolveUnit(const int32 AgentId) const;

private:
	/** 지금까지 보상을 수집한 총 횟수. 셰이핑 감쇠의 분자다. */
	int64 RewardStepCount = 0;

	/** 같은 프레임에 같은 아레나를 여러 번 리셋하지 않기 위한 중복 제거.
	 *  20개 에이전트가 각자 ResetAgentEpisode 를 부르기 때문에 반드시 필요하다. */
	uint64 LastResetFrame = 0;
	TSet<TObjectPtr<ALearningAgentsGymBase>> GymsResetThisFrame;
};
