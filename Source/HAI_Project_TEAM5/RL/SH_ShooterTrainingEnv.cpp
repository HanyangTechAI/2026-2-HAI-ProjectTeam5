// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_ShooterTrainingEnv.h"

#include "Combat/CombatStatsComponent.h"
#include "LearningAgentsGym.h"
#include "SH_RLCharacter.h"
#include "SH_VisionComponent.h"

ASH_RLCharacter* USH_ShooterTrainingEnv::ResolveUnit(const int32 AgentId) const
{
	return Cast<ASH_RLCharacter>(const_cast<USH_ShooterTrainingEnv*>(this)->GetAgent(AgentId));
}

float USH_ShooterTrainingEnv::GetShapingScale() const
{
	if (ShapingDecaySteps <= 0)
	{
		return 1.0f;   // 감쇠 없음
	}
	const float Progress = static_cast<float>(RewardStepCount) / static_cast<float>(ShapingDecaySteps);
	return FMath::Clamp(1.0f - Progress, 0.0f, 1.0f);
}

void USH_ShooterTrainingEnv::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	OutReward = 0.0f;
	++RewardStepCount;

	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	UCombatStatsComponent* Stats = Unit ? Unit->CombatStats.Get() : nullptr;
	if (!Stats)
	{
		return;
	}

	// ★ 사망 벌점은 죽은 그 스텝에 **한 번만** 들어간다 (bDiedThisInterval).
	//   그 뒤로는 보상 0 이다 — 죽은 유닛에 시간 벌점을 계속 물리면
	//   "죽으면 계속 손해" 가 되어 크리틱이 사망 상태의 가치를 과도하게 낮게 배운다.
	const bool bAlive = Stats->IsAlive() && Unit->bEntityEnabled;

	float Reward = 0.0f;

	// --- 주 보상 ------------------------------------------------------------
	Reward += DamageDealtWeight * (Stats->DamageDealtAccum / 100.0f);
	Reward -= DamageTakenWeight * (Stats->DamageTakenAccum / 100.0f);
	Reward += KillBonus * static_cast<float>(Stats->KillsAccum);
	// 누산기를 비우기 전에 더해 명중 보너스가 결정당 한 번만 반영되게 한다.
	Reward += HitBonus * static_cast<float>(Stats->ShotsHitAccum);

	const int32 Missed = FMath::Max(0, Stats->ShotsFiredAccum - Stats->ShotsHitAccum);
	Reward -= MissWeight * static_cast<float>(Missed);

	if (Stats->bDiedThisInterval)
	{
		Reward -= DeathPenalty;
	}

	if (bAlive)
	{
		Reward -= TimePenalty;

		// --- 셰이핑 -------------------------------------------------------
		// 시야 제한 때문에 주 보상이 희소해진다. MinimumAdvantage = 0 이라 벌점으로는
		// 끌 수 없으므로 양의 셰이핑으로 초기 탐색을 돕는다.
		const float ShapingScale = GetShapingScale();
		if (ShapingScale > 0.0f)
		{
			if (USH_VisionComponent* Vision = Unit->Vision.Get())
			{
				// 같은 프레임의 관측 수집과 스캔 결과를 공유한다 (프레임 가드).
				// RunTraining 은 한 프레임에 GatherRewards → GatherObservations 순으로
				// 부르므로, 여기가 그 프레임의 첫 스캔이 된다.
				Vision->Scan();

				if (Vision->IsHostileVisible())
				{
					Reward += ShapingScale * SeeWeight;

					// 조준선 중앙에 둘수록. 4제곱이라 대충 보는 것으로는 거의 못 받는다.
					const float HalfFov = FMath::Max(0.5f * Vision->FOVDegrees, KINDA_SMALL_NUMBER);
					const float Alignment = 1.0f - FMath::Abs(Vision->GetHostileAngleDeg()) / HalfFov;
					Reward += ShapingScale * AlignWeight
						* FMath::Pow(FMath::Max(Alignment, 0.0f), 4.0f);
				}
			}
		}
	}

	// ★ 반드시 비운다. 안 비우면 같은 데미지가 여러 결정 스텝에 걸쳐 중복 계산되어
	//   보상이 부풀고, 크리틱이 그 부푼 값을 학습한다. 에러는 나지 않는다.
	Stats->ConsumeRewardAccumulators();

	OutReward = Reward;
}

void USH_ShooterTrainingEnv::GatherAgentCompletion_Implementation(
	ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
	OutCompletion = ELearningAgentsCompletion::Running;

	if (!bTerminateOnDeath)
	{
		// 에피소드는 MaxEpisodeStepNum 으로만 끊긴다 (Truncation).
		return;
	}

	const ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	const UCombatStatsComponent* Stats = Unit ? Unit->CombatStats.Get() : nullptr;

	// ★ 사망은 Truncation 이 아니라 Termination 이다.
	//   Truncation 은 "끊겼지만 나머지 가치를 크리틱이 추정하라" 는 뜻이고
	//   Termination 은 "이후 보상이 진짜로 0" 이라는 뜻이다. 죽은 유닛은 후자다.
	//   ppo.py 가 gamma*V(s') 항을 Termination 에서만 0 으로 만든다.
	if (!Unit || !Stats || !Stats->IsAlive() || !Unit->bEntityEnabled)
	{
		OutCompletion = ELearningAgentsCompletion::Termination;
	}
}

void USH_ShooterTrainingEnv::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	if (!Unit)
	{
		return;
	}

	ALearningAgentsGymBase* Gym = Unit->CachedGym.Get();
	if (!bResetWholeGym || !Gym)
	{
		// 유닛 하나만 되살린다. 위치는 그대로다 — 아레나의 고정 슬롯으로 돌리려면
		// 아레나 전체 리셋이 필요하다.
		Unit->ResetForNewMatch();
		return;
	}

	// --- 아레나 전체를 한 번만 리셋한다 -------------------------------------
	// ★ 같은 프레임에 20개 에이전트가 각자 이걸 부르므로 중복 제거가 필수다.
	//   Gym->Reset() 은 ResetLearningComponent 를 거쳐 20 유닛 전부를
	//   ResetEntity + EnableEntity 하고, OnPostGymReset 에서 ASH_Arena 가
	//   ApplySpawnSlots() 로 고정 슬롯에 다시 깔아 준다.
	//
	// ★ 결과적으로 "에피소드 = 아레나 안 누군가의 첫 사망까지" 가 된다.
	//   팀 전멸까지 가는 긴 에피소드를 원하면 bTerminateOnDeath 를 끄고
	//   MaxEpisodeStepNum 으로 길이를 정할 것.
	const uint64 Frame = GFrameCounter;
	if (LastResetFrame != Frame)
	{
		LastResetFrame = Frame;
		GymsResetThisFrame.Reset();
	}

	if (GymsResetThisFrame.Contains(Gym))
	{
		return;
	}
	GymsResetThisFrame.Add(Gym);

	Gym->Reset();
}
