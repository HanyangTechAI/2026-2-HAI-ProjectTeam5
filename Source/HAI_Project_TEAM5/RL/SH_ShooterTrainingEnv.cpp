// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_ShooterTrainingEnv.h"

#include "Combat/CombatStatsComponent.h"
#include "LearningAgentsGym.h"
#include "SH_Arena.h"
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
	const float Decayed = FMath::Clamp(1.0f - Progress, 0.0f, 1.0f);

	// ★ 0 까지 내리지 않는다 — 헤더의 ShapingFloor 주석 참조.
	return FMath::Max(Decayed, FMath::Clamp(ShapingFloor, 0.0f, 1.0f));
}

float USH_ShooterTrainingEnv::GetRoundRewardScale() const
{
	if (RoundRewardRampSteps <= 0)
	{
		return 1.0f;
	}
	return FMath::Clamp(static_cast<float>(RewardStepCount) / static_cast<float>(RoundRewardRampSteps), 0.0f, 1.0f);
}

float USH_ShooterTrainingEnv::GetRoundClockCost() const
{
	if (!bRoundClockCost)
	{
		return 0.0f;
	}
	return OutcomeReward * GetRoundRewardScale() * (1.0f - FMath::Clamp(ClockDiscount, 0.0f, 1.0f));
}

float USH_ShooterTrainingEnv::ComputeIndividualReward(ASH_RLCharacter* Unit)
{
	++RewardStepCount;

	UCombatStatsComponent* Stats = Unit ? Unit->CombatStats.Get() : nullptr;
	if (!Stats)
	{
		return 0.0f;
	}

	// ★ 사망 벌점은 죽은 그 스텝에 **한 번만** 들어간다(bDiedThisInterval).
	//   죽은 뒤에는 개인 보상이 0 이다. 팀 몫과 라운드 결과는 GatherAgentReward 가 더한다.
	const bool bAlive = Stats->IsAlive() && Unit->bEntityEnabled;

	// --- 진단 지표 집계 -------------------------------------------------------
	// ★ ConsumeRewardAccumulators() 보다 **먼저** 읽는다.
	DamageTakenInterval += Stats->DamageTakenAccum;
	DamageDealtInterval += Stats->DamageDealtAccum;
	++GathersInterval;

	float Reward = 0.0f;

	// --- 전투 (제로섬) --------------------------------------------------------
	Reward += DamageDealtWeight * (Stats->DamageDealtAccum / 100.0f);
	Reward -= DamageTakenWeight * (Stats->DamageTakenAccum / 100.0f);
	Reward += KillBonus * static_cast<float>(Stats->KillsAccum);

	const int32 Missed = FMath::Max(0, Stats->ShotsFiredAccum - Stats->ShotsHitAccum);
	Reward -= MissWeight * static_cast<float>(Missed);

	if (Stats->bDiedThisInterval)
	{
		Reward -= DeathPenalty;
	}

	if (bAlive)
	{
		Reward -= TimePenalty;

		// --- 셰이핑 -----------------------------------------------------------
		const float ShapingScale = GetShapingScale();
		if (ShapingScale > 0.0f)
		{
			if (USH_VisionComponent* Vision = Unit->Vision.Get())
			{
				// 같은 프레임의 관측 수집과 스캔 결과를 공유한다(프레임 가드).
				Vision->Scan();

				if (Vision->IsHostileVisible())
				{
					Reward += ShapingScale * SeeWeight;

					const float HalfFov = FMath::Max(0.5f * Vision->FOVDegrees, KINDA_SMALL_NUMBER);
					const float Alignment = 1.0f - FMath::Abs(Vision->GetHostileAngleDeg()) / HalfFov;
					Reward += ShapingScale * AlignWeight
						* FMath::Pow(FMath::Max(Alignment, 0.0f), 4.0f);
				}
			}
		}
	}

	// ★ 반드시 비운다. 안 비우면 같은 피해가 다음 스텝에 또 들어간다.
	Stats->ConsumeRewardAccumulators();

	return Reward;
}

void USH_ShooterTrainingEnv::RefreshFrameRewards()
{
	const uint64 Frame = GFrameCounter;
	if (RewardFrame == Frame)
	{
		return;
	}
	RewardFrame = Frame;
	IndividualRewards.Reset();
	TeamRewardSums.Reset();

	if (!HasAgentManager())
	{
		return;
	}

	// ★ RunTraining 은 한 프레임에 등록된 에이전트 전원의 보상을 모은다(GatherRewards).
	//   그래서 첫 GatherAgentReward 호출에서 전원을 한 번에 계산해 두면 된다.
	TArray<UObject*> AllAgents;
	TArray<int32> AllAgentIds;
	GetAllAgents(AllAgents, AllAgentIds, ASH_RLCharacter::StaticClass());

	for (int32 Index = 0; Index < AllAgents.Num(); ++Index)
	{
		ASH_RLCharacter* Unit = Cast<ASH_RLCharacter>(AllAgents[Index]);
		const float Reward = ComputeIndividualReward(Unit);
		IndividualRewards.Add(AllAgentIds[Index], Reward);

		if (Unit && Unit->CombatStats)
		{
			const TPair<const ALearningAgentsGymBase*, int32> Key(
				Unit->CachedGym.Get(), static_cast<int32>(Unit->CombatStats->TeamId));
			FVector2D& Sum = TeamRewardSums.FindOrAdd(Key, FVector2D::ZeroVector);
			Sum.X += Reward;
			Sum.Y += 1.0;
		}
	}
}

void USH_ShooterTrainingEnv::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	OutReward = 0.0f;

	RefreshFrameRewards();

	const float* IndividualPtr = IndividualRewards.Find(AgentId);
	const float Individual = IndividualPtr ? *IndividualPtr : 0.0f;

	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	if (!Unit || !Unit->CombatStats)
	{
		OutReward = Individual;
		return;
	}

	const int32 Team = static_cast<int32>(Unit->CombatStats->TeamId);

	// --- 팀 스피릿 ------------------------------------------------------------
	float TeamMean = Individual;
	const TPair<const ALearningAgentsGymBase*, int32> Key(Unit->CachedGym.Get(), Team);
	if (const FVector2D* Sum = TeamRewardSums.Find(Key))
	{
		if (Sum->Y > 0.0)
		{
			TeamMean = static_cast<float>(Sum->X / Sum->Y);
		}
	}

	const float RampScale = GetRoundRewardScale();
	const float Tau = FMath::Clamp(TeamSpirit, 0.0f, 1.0f) * RampScale;
	float Reward = (1.0f - Tau) * Individual + Tau * TeamMean;

	// --- 라운드 결과 · 시계 비용 ---------------------------------------------
	// ★ GatherCompletion 이 같은 프레임에 이 아레나 전원에게 Termination 을 냈다.
	//   리셋은 그 뒤 ProcessExperience 안에서 일어나므로 결과는 정확히 한 번 들어간다.
	//   죽은 에이전트도 받는다(사망 마스크).
	// ★ 진행 중인 스텝에는 시계 비용을 뺀다 — 죽은 에이전트 포함(헤더 bRoundClockCost 참조).
	if (const ASH_Arena* Arena = Cast<ASH_Arena>(Unit->CachedGym.Get()))
	{
		if (Arena->IsRoundOver())
		{
			Reward += OutcomeReward * RampScale * Arena->GetRoundOutcome(Team);
		}
		else
		{
			Reward -= GetRoundClockCost();
		}
	}

	OutReward = Reward;
}

void USH_ShooterTrainingEnv::GatherAgentCompletion_Implementation(
	ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
	OutCompletion = ELearningAgentsCompletion::Running;

	const ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	const ASH_Arena* Arena = Unit ? Cast<ASH_Arena>(Unit->CachedGym.Get()) : nullptr;

	// ★ 라운드가 끝났을 때만 완료다. 사망은 완료가 아니다(사망 마스크).
	//   라운드 끝은 진짜로 이후 보상이 없으므로 Termination 이다 — 다음 라운드는 새 에피소드다.
	if (Arena && Arena->IsRoundOver())
	{
		OutCompletion = ELearningAgentsCompletion::Termination;
	}
}

void USH_ShooterTrainingEnv::ConsumeDamageStats(
	double& OutDamageTaken, double& OutDamageDealt, int64& OutGathers)
{
	OutDamageTaken = DamageTakenInterval;
	OutDamageDealt = DamageDealtInterval;
	OutGathers = GathersInterval;
	DamageTakenInterval = 0.0;
	DamageDealtInterval = 0.0;
	GathersInterval = 0;
}

void USH_ShooterTrainingEnv::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	if (!Unit)
	{
		return;
	}

	ALearningAgentsGymBase* Gym = Unit->CachedGym.Get();
	if (!Gym)
	{
		// 아레나 소속이 없는 유닛(수동 배치 테스트)은 혼자 되살린다.
		Unit->ResetForNewMatch();
		return;
	}

	// --- 아레나 전체를 한 번만 리셋한다 -------------------------------------
	// ★ 라운드가 끝나면 아레나의 에이전트 전원이 같은 프레임에 이걸 부른다. 중복 제거가 필수다.
	//   Gym->Reset() → 유닛 전원 ResetEntity + EnableEntity → OnPostGymReset 에서
	//   ASH_Arena 가 스폰 슬롯 배치와 BeginRound() 를 한다.
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
