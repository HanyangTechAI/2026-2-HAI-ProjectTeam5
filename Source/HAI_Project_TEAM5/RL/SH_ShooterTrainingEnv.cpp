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

float USH_ShooterTrainingEnv::GatherProgressReward(
	const int32 AgentId, ASH_RLCharacter* Unit, USH_VisionComponent* Vision)
{
	if (!Unit || !Vision)
	{
		ResetAgentRewardState(AgentId);
		return 0.0f;
	}

	FSHShooterAgentRewardState& State = AgentRewardStates.FindOrAdd(AgentId);
	if (State.Unit.Get() != Unit)
	{
		State = FSHShooterAgentRewardState();
		State.Unit = Unit;
	}

	const float ControlYaw = Unit->GetControlRotation().Yaw;
	if (!FMath::IsFinite(ControlYaw))
	{
		State = FSHShooterAgentRewardState();
		State.Unit = Unit;
		return 0.0f;
	}

	if (Vision->IsHostileVisible())
	{
		const float HalfFov = 0.5f * Vision->FOVDegrees;
		const float HostileAngle = Vision->GetHostileAngleDeg();
		if (!FMath::IsFinite(HalfFov) || HalfFov <= KINDA_SMALL_NUMBER
			|| !FMath::IsFinite(HostileAngle))
		{
			State.bInitialized = true;
			State.bWasVisible = true;
			State.bHasPreviousAimError = false;
			State.bHasPreviousMemoryError = false;
			State.bHasLastEnemyWorldYaw = false;
			State.bReacquirePending = false;
			return 0.0f;
		}

		const float AimError = FMath::Clamp(FMath::Abs(HostileAngle) / HalfFov, 0.0f, 1.0f);
		float Reward = 0.0f;
		const bool bReacquired = State.bInitialized && !State.bWasVisible && State.bReacquirePending;

		if (State.bInitialized && State.bWasVisible && State.bHasPreviousAimError)
		{
			Reward += AimProgressWeight * (State.PreviousAimError - AimError);
		}
	else if (bReacquired)
		{
			Reward += ReacquireBonus;
		}

		State.PreviousAimError = AimError;
		State.bHasPreviousAimError = true;
		State.bHasPreviousMemoryError = false;
		State.LastEnemyWorldYaw = FMath::UnwindDegrees(ControlYaw + HostileAngle);
		State.bHasLastEnemyWorldYaw = FMath::IsFinite(State.LastEnemyWorldYaw);
		State.bReacquirePending = false;
		State.bWasVisible = true;
		State.bInitialized = true;
		return Reward;
	}

	const float MemoryHorizon = Vision->MemoryHorizonSeconds;
	const float TimeSinceSeen = Vision->GetTimeSinceSeen();
	const bool bMemoryValid = State.bHasLastEnemyWorldYaw
		&& FMath::IsFinite(MemoryHorizon) && MemoryHorizon > 0.0f
		&& FMath::IsFinite(TimeSinceSeen) && TimeSinceSeen < MemoryHorizon;
	if (!bMemoryValid)
	{
		State.bHasPreviousAimError = false;
		State.bHasPreviousMemoryError = false;
		State.bReacquirePending = false;
		State.bWasVisible = false;
		State.bInitialized = true;
		return 0.0f;
	}

	const float MemoryError = FMath::Clamp(
		FMath::Abs(FMath::FindDeltaAngleDegrees(ControlYaw, State.LastEnemyWorldYaw)) / 180.0f,
		0.0f, 1.0f);
	const bool bVisibilityChanged = !State.bInitialized || State.bWasVisible;
	float Reward = 0.0f;
	if (!bVisibilityChanged && State.bHasPreviousMemoryError)
	{
		Reward += MemoryTurnProgressWeight * (State.PreviousMemoryError - MemoryError);
	}

	if (State.bInitialized && State.bWasVisible)
	{
		State.bReacquirePending = true;
	}
	State.PreviousMemoryError = MemoryError;
	State.bHasPreviousMemoryError = true;
	State.bHasPreviousAimError = false;
	State.bWasVisible = false;
	State.bInitialized = true;
	return Reward;
}

void USH_ShooterTrainingEnv::ResetAgentRewardState(const int32 AgentId)
{
	AgentRewardStates.Remove(AgentId);
}

void USH_ShooterTrainingEnv::ResetGymAgentRewardStates(ALearningAgentsGymBase* Gym)
{
	for (auto It = AgentRewardStates.CreateIterator(); It; ++It)
	{
		ASH_RLCharacter* Unit = It.Value().Unit.Get();
		if (!Unit || Unit->CachedGym.Get() == Gym)
		{
			It.RemoveCurrent();
		}
	}
}

void USH_ShooterTrainingEnv::OnAgentsAdded_Implementation(const TArray<int32>& AgentIds)
{
	Super::OnAgentsAdded_Implementation(AgentIds);
	for (const int32 AgentId : AgentIds)
	{
		ResetAgentRewardState(AgentId);
	}
}

void USH_ShooterTrainingEnv::OnAgentsRemoved_Implementation(const TArray<int32>& AgentIds)
{
	for (const int32 AgentId : AgentIds)
	{
		ResetAgentRewardState(AgentId);
	}
	Super::OnAgentsRemoved_Implementation(AgentIds);
}

void USH_ShooterTrainingEnv::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	OutReward = 0.0f;
	++RewardStepCount;

	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	UCombatStatsComponent* Stats = Unit ? Unit->CombatStats.Get() : nullptr;
	if (!Stats)
	{
		ResetAgentRewardState(AgentId);
		return;
	}

	// ★ 사망 벌점은 죽은 그 스텝에 **한 번만** 들어간다 (bDiedThisInterval).
	//   그 뒤로는 보상 0 이다 — 죽은 유닛에 시간 벌점을 계속 물리면
	//   "죽으면 계속 손해" 가 되어 크리틱이 사망 상태의 가치를 과도하게 낮게 배운다.
	const bool bAlive = Stats->IsAlive() && Unit->bEntityEnabled;
	if (!bAlive)
	{
		ResetAgentRewardState(AgentId);
	}

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
		if (USH_VisionComponent* Vision = Unit->Vision.Get())
		{
			// Rewards and observations use one cached scan for the current frame.
			Vision->Scan();

			const float ShapingScale = GetShapingScale();
			if (ShapingScale > 0.0f && Vision->IsHostileVisible())
			{
				Reward += ShapingScale * SeeWeight;
			}

			Reward += GatherProgressReward(AgentId, Unit, Vision);
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
		ResetAgentRewardState(AgentId);
		OutCompletion = ELearningAgentsCompletion::Termination;
	}
}

void USH_ShooterTrainingEnv::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	if (!Unit)
	{
		ResetAgentRewardState(AgentId);
		return;
	}

	ALearningAgentsGymBase* Gym = Unit->CachedGym.Get();
	if (!bResetWholeGym || !Gym)
	{
		ResetAgentRewardState(AgentId);
		// 유닛 하나만 되살린다. 위치는 그대로다 — 아레나의 고정 슬롯으로 돌리려면
		// 아레나 전체 리셋이 필요하다.
		Unit->ResetForNewMatch();
		return;
	}

	// --- 아레나 전체를 한 번만 리셋한다 -------------------------------------
	ResetGymAgentRewardStates(Gym);

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
