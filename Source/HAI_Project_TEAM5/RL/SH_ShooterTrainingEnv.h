// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "SH_ShooterTrainingEnv.generated.h"

class ALearningAgentsGymBase;
class ASH_RLCharacter;

/**
 *  보상 · 완료 · 리셋. **라운드 단위**다 (2026-10-02 개편).
 *
 *  라운드 = 한 팀이 전멸하거나 ASH_Arena::RoundTimeLimitSeconds 가 지날 때까지.
 *  라운드가 끝나면 그 아레나의 에이전트 전원이 같은 스텝에 Termination 을 받고
 *  아레나가 리셋된다. 길이가 MaxEpisodeStepNum 을 넘는 라운드는 엔진이 Truncation 으로
 *  잘라 주지만 세계는 그대로 이어진다(엔진이 길이 절단에는 ResetAgents 를 부르지 않는다).
 *
 *  ★ 사망은 더 이상 완료가 아니다 — **사망 마스크**.
 *  죽은 에이전트는 라운드 끝까지 에피소드에 남고, 행동은 선택지 하나로 고정되며
 *  (USH_ShooterInteractor::MakeAgentActionModifier), 팀 몫 보상과 라운드 결과 보상을 받는다.
 *  죽는 순간 Termination 을 주면 그 에이전트의 반환값에서 팀 승패가 빠진다 —
 *  희생을 배울 수 없고 생존만 중요해져서 "버티기" 쪽으로 기운다.
 *
 *  ★ MinimumAdvantage = 0.0 (ppo.py) 이라 **음수 advantage 가 전부 0으로 잘린다.**
 *  벌점은 행동을 직접 억제하지 못한다 — 원하는 행동은 양의 보상으로 끌어야 한다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API USH_ShooterTrainingEnv : public ULearningAgentsTrainingEnvironment
{
	GENERATED_BODY()

public:
	//~ 개인 보상 — 제로섬 ------------------------------------------------------

	/** 준 피해 100 당 보상. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float DamageDealtWeight = 1.0f;

	/**
	 *  받은 피해 100 당 벌점.
	 *
	 *  ★ 준 피해와 **같은 값**이다(이전 0.7). 다르면 보상이 제로섬이 아니게 되고,
	 *  0.7 처럼 작으면 "맞는 것보다 쏘는 것이 더 중요" 하지만 사망 벌점과 합쳐지면
	 *  위험 회피 쪽으로 기울었다. 0.7 을 뒀던 이유("같으면 아무것도 안 하기가 기대값 0")는
	 *  라운드 시간 초과 = 양쪽 패배 규칙이 대신한다 — 아무것도 안 하면 확정 패배다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float DamageTakenWeight = 1.0f;

	/**
	 *  빗맞힌 발당 벌점.
	 *
	 *  ★★ 이 값을 키우면 **발사 자체가 산술적으로 손해**가 되어 정책이 사격을 포기한다.
	 *  명중 1발 = 피해 0.10 + 킬 보너스 몫 0.05 = 0.15.
	 *  발사의 기대 보상 = 0.15p − MissWeight(1 − p), 손익분기 p = MissWeight / (0.15 + MissWeight).
	 *      MissWeight 0.05  → p = 25.0%      MissWeight 0.005 → p = 3.2%
	 *  2026-09-29 실측: 0.05 에서 셰이핑이 빠지자 발사량이 10배 줄었다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float MissWeight = 0.005f;

	/** 킬 보너스(마지막 명중자). */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float KillBonus = 0.5f;

	/**
	 *  사망 벌점. 죽은 스텝에 한 번만 들어간다(bDiedThisInterval).
	 *
	 *  ★ KillBonus 와 **같은 값**이다(이전 1.0). 이전에는 죽는 손해(1.0)가 죽이는 이득(0.5)의
	 *  두 배라서 1대1 에서 지키는 상대를 노리는 기대값이 버티기보다 낮았다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float DeathPenalty = 0.5f;

	/** 살아 있는 결정마다 들어가는 시간 벌점. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward")
	float TimePenalty = 0.002f;

	//~ 팀 · 라운드 -------------------------------------------------------------

	/**
	 *  라운드 결과 보상 W. 승 +W, 패 −W. 시간 초과·동시 전멸은 ASH_Arena::bTimeoutIsLossForBoth 면
	 *  양 팀 −W, 아니면 0. **죽은 아군도 받는다.**
	 *
	 *  ★ 크기: 한 라운드에 유닛 하나가 주고받는 피해 보상은 대략 ±1 이다(HP 100 = 1.0).
	 *  W 가 그보다 커야 개인 교환보다 팀 승패가 우선한다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Round", meta = (ClampMin = "0.0"))
	float OutcomeReward = 2.0f;

	/**
	 *  팀 스피릿 τ. 받는 보상 = (1−τ)·개인 보상 + τ·같은 아레나 같은 팀 에이전트의 평균.
	 *
	 *  ★ 개인 항이 제로섬이면 τ = 1 의 팀 항은 Δ(아군 총 HP − 적 총 HP)/100 이 되어
	 *  퍼텐셜 기반 셰이핑(최적 정책을 바꾸지 않는 셰이핑)과 같아진다. 멀리 있는 승패로 가는
	 *  촘촘한 신호다. 처음부터 1 이면 개인 기여와 보상의 연결이 흐려져 조준 같은 개인 기술이
	 *  느려진다 — 0.3 에서 시작해 올린다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Round", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TeamSpirit = 0.3f;

	/**
	 *  결과 보상과 팀 스피릿을 0 에서 목표값까지 선형으로 올리는 데 걸리는 보상 수집 횟수.
	 *  0 이면 처음부터 목표값이다.
	 *
	 *  ★ 왜 필요한가 (2026-10-02 실측). 처음부터 결과 ±2.0, τ 0.3 으로 돌리면 학습 전 정책은
	 *  매 라운드 지고, 크리틱은 −2.0 이 "언제" 들어오는지 맞히지 못한다. 그 오차가 이점의
	 *  분산을 키우고 엔진이 이점을 표준편차로 나누므로, 명중 1발 같은 작은 개인 신호가 묻힌다.
	 *  같은 시점(약 310초) 명중률: 처음부터 켬 1.8% / 둘 다 끔 7.6% / 5회차 5.5%.
	 *  기본값은 셰이핑 감쇠와 같은 3,000,000 (에이전트 160 에서 약 30분).
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Round", meta = (ClampMin = "0"))
	int32 RoundRewardRampSteps = 3000000;

	/**
	 *  라운드 시계 비용. 라운드가 진행되는 동안 **모든 에이전트(죽은 쪽 포함)** 에게
	 *  스텝마다 c = OutcomeReward x 램프 x (1 − γ) 를 뺀다.
	 *
	 *  ★★ 왜 필요한가 — 결과 보상만으로는 대치를 못 막는다 (2026-10-02 실측).
	 *  라운드 끝의 −W 는 γ^d 로 할인되므로, 질 것이 확실하면 **늦게 지는 쪽이 보상이 크다.**
	 *  γ = 0.99 에서 100스텝(5초) 늦추면 벌점이 63% 준다. 시간 초과 = 양쪽 패배 규칙도
	 *  45초 뒤의 −W 라서 할인된다. 실측: 램프가 0.35 를 넘자 교전이 줄었다(살아 있는 유닛의
	 *  적 시야 비율 35% → 26%, 준 피해 103 → 80, 라운드 길이 12.9 → 22.9초).
	 *
	 *  c = W(1 − γ) 이면 d스텝 뒤에 지는 반환값이
	 *      −c(1 − γ^d)/(1 − γ) − W γ^d = −W(1 − γ^d) − W γ^d = −W
	 *  로 **시점과 무관해진다.** 이기는 반환값은 W(2γ^d − 1) 이라 빨리 이길수록 크다.
	 *  밀고 들어가 승률 p 로 d스텝 뒤에 결판나면 기대값 W(2pγ^d − 1) > −W (p > 0).
	 *
	 *  ★ 죽은 에이전트에게도 매긴다. 산 쪽만 내면 "빨리 죽어서 비용을 피하기" 가 이득이 된다.
	 *  ★ γ 는 ASH_CombatTrainer 가 TrainingSettings.DiscountFactor 로 맞춰 준다(ClockDiscount).
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Round")
	bool bRoundClockCost = true;

	/** 시계 비용 계산에 쓰는 할인율. 학습 설정의 DiscountFactor 와 같아야 상쇄가 정확하다. */
	UPROPERTY(EditAnywhere, Category = "SH|Round", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClockDiscount = 0.99f;

	//~ 셰이핑 (감쇠) ----------------------------------------------------------

	/** 적을 시야에 넣고 있는 것 자체의 보상. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward|Shaping")
	float SeeWeight = 0.02f;

	/** 적을 조준선 중앙에 둘수록 주는 보상. 정렬도의 4제곱을 곱한다. */
	UPROPERTY(EditAnywhere, Category = "SH|Reward|Shaping")
	float AlignWeight = 0.05f;

	/**
	 *  셰이핑이 하한으로 감쇠하는 데 걸리는 **보상 수집 총 횟수**(에이전트 수 x 결정 스텝).
	 *  ★ 에이전트 수를 바꾸면 이 값도 비례해서 바꿔야 한다. 0 이면 감쇠하지 않는다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Reward|Shaping", meta = (ClampMin = "0"))
	int32 ShapingDecaySteps = 3000000;

	/**
	 *  감쇠가 끝난 뒤에도 남겨 둘 셰이핑 하한 배율.
	 *  ★ 0 이면 "적을 향해 돈다" 를 유지할 신호가 사라진다(2026-09-29 실측).
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Reward|Shaping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ShapingFloor = 0.15f;

	//~ ULearningAgentsTrainingEnvironment ------------------------------------

	virtual void GatherAgentReward_Implementation(float& OutReward, const int32 AgentId) override;
	virtual void GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;
	virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;

	/** 현재 셰이핑 배율(1 → ShapingFloor). */
	UFUNCTION(BlueprintPure, Category = "SH|Reward")
	float GetShapingScale() const;

	/** 현재 라운드 보상 배율(0 → 1). 결과 보상과 팀 스피릿에 곱한다. */
	UFUNCTION(BlueprintPure, Category = "SH|Round")
	float GetRoundRewardScale() const;

	/** 현재 라운드 시계 비용(스텝당). bRoundClockCost 가 꺼져 있으면 0. */
	UFUNCTION(BlueprintPure, Category = "SH|Round")
	float GetRoundClockCost() const;

	/**
	 *  구간 누적 피해량을 읽고 **비운다**.
	 *
	 *  ★ 반드시 ConsumeRewardAccumulators() **전에** 읽어야 한다. 순서가 바뀌면 항상 0 이다.
	 *
	 *  @param OutDamageTaken   구간 동안 RL 에이전트들이 받은 총 피해량(구역 피해 포함)
	 *  @param OutDamageDealt   구간 동안 RL 에이전트들이 준 총 피해량
	 *  @param OutGathers       보상을 수집한 횟수 (에이전트 x 결정). 분모다
	 */
	void ConsumeDamageStats(double& OutDamageTaken, double& OutDamageDealt, int64& OutGathers);

	/** 보상 수집 누적 횟수. 셰이핑 감쇠와 라운드 보상 램프가 이 값으로 계산된다. */
	int64 GetRewardStepCount() const { return RewardStepCount; }

	/** 이어서 학습할 때 복원한다(ASH_CombatTrainer -SHResume). 안 하면 셰이핑이 1.0, 램프가 0 으로 돌아간다. */
	void SetRewardStepCount(const int64 InCount) { RewardStepCount = FMath::Max<int64>(0, InCount); }

protected:
	ASH_RLCharacter* ResolveUnit(const int32 AgentId) const;

	/**
	 *  유닛 하나의 개인 보상을 계산하고 그 유닛의 보상 누산기를 **비운다**.
	 *  ★ 누산기를 비우므로 프레임당 유닛마다 정확히 한 번만 불려야 한다.
	 */
	float ComputeIndividualReward(ASH_RLCharacter* Unit);

	/**
	 *  이번 프레임의 개인 보상과 팀별 합을 한 번에 계산해 캐시한다.
	 *
	 *  ★ 왜 캐시인가. 팀 스피릿은 같은 팀 전원의 개인 보상이 필요한데, 개인 보상 계산은
	 *  누산기를 비운다. 에이전트마다 따로 계산하면 두 번째부터 0 을 읽는다.
	 */
	void RefreshFrameRewards();

private:
	/** 지금까지 보상을 수집한 총 횟수. 셰이핑 감쇠의 분자다. */
	int64 RewardStepCount = 0;

	/** RefreshFrameRewards 캐시. */
	uint64 RewardFrame = 0;
	TMap<int32, float> IndividualRewards;
	/** (아레나, 팀) → (합, 개수). */
	TMap<TPair<const ALearningAgentsGymBase*, int32>, FVector2D> TeamRewardSums;

	/** 같은 프레임에 같은 아레나를 여러 번 리셋하지 않기 위한 중복 제거.
	 *  라운드가 끝나면 아레나의 에이전트 전원이 같은 프레임에 ResetAgentEpisode 를 부른다. */
	uint64 LastResetFrame = 0;
	TSet<TObjectPtr<ALearningAgentsGymBase>> GymsResetThisFrame;

	/** 구간 누적 피해량. ConsumeDamageStats 가 비운다. */
	double DamageTakenInterval = 0.0;
	double DamageDealtInterval = 0.0;
	int64 GathersInterval = 0;
};
