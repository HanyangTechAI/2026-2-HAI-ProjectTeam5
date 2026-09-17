// Fill out your copyright notice in the Description page of Project Settings.

#include "CombatStatsComponent.h"

#include "GameFramework/Actor.h"

UCombatStatsComponent::UCombatStatsComponent()
{
	// 틱하지 않는다. 16 아레나 x 20 유닛이면 틱 하나가 320개가 된다.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCombatStatsComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentHP = MaxHP;
}

float UCombatStatsComponent::ApplyDamage(float Amount, AActor* Causer)
{
	if (Amount <= 0.0f || !IsAlive())
	{
		return 0.0f;
	}

	// ★ 남은 HP 로 클램프한다. 오버킬을 그대로 보상에 넣으면
	//   "이미 죽은 적에게 계속 쏘기" 가 이득이 된다.
	const float Applied = FMath::Min(Amount, CurrentHP);
	CurrentHP -= Applied;
	DamageTakenAccum += Applied;

	AActor* const Victim = GetOwner();
	const bool bKilled = !IsAlive();

	if (UCombatStatsComponent* CauserStats = FindOn(Causer))
	{
		CauserStats->DamageDealtAccum += Applied;
		if (bKilled)
		{
			CauserStats->KillsAccum += 1;
		}
	}

	OnDamaged.Broadcast(Victim, Applied, Causer);

	if (bKilled)
	{
		bDiedThisInterval = true;
		OnDeath.Broadcast(Victim, Causer);
	}

	return Applied;
}

void UCombatStatsComponent::RecordShotFired(bool bHitHostile)
{
	ShotsFiredAccum += 1;
	if (bHitHostile)
	{
		ShotsHitAccum += 1;
	}
}

void UCombatStatsComponent::ResetForNewMatch()
{
	CurrentHP = MaxHP;
	ConsumeRewardAccumulators();
}

void UCombatStatsComponent::ConsumeRewardAccumulators()
{
	DamageDealtAccum = 0.0f;
	DamageTakenAccum = 0.0f;
	ShotsFiredAccum = 0;
	ShotsHitAccum = 0;
	KillsAccum = 0;
	bDiedThisInterval = false;
}

UCombatStatsComponent* UCombatStatsComponent::FindOn(AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UCombatStatsComponent>() : nullptr;
}

bool UCombatStatsComponent::AreHostile(AActor* A, AActor* B)
{
	if (!A || !B || A == B)
	{
		return false;
	}

	const UCombatStatsComponent* StatsA = FindOn(A);
	const UCombatStatsComponent* StatsB = FindOn(B);
	if (!StatsA || !StatsB)
	{
		return false;
	}

	if (StatsA->TeamId == StatsB->TeamId)
	{
		return StatsA->bAllowFriendlyFire && StatsB->bAllowFriendlyFire;
	}
	return true;
}
