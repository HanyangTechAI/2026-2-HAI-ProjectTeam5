// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTTask_Search.h"

#include "Battle/AI/SH_BotSteering.h"
#include "RL/SH_Arena.h"
#include "RL/SH_RLCharacter.h"

USH_BTTask_Search::USH_BTTask_Search()
{
	NodeName = TEXT("SH Search");
}

FString USH_BTTask_Search::GetStaticDescription() const
{
	return FString::Printf(TEXT("%.1f초 동안 %.0f°/s 로 훑기%s"),
		DurationSeconds, SweepRateDeg, bAdvance ? TEXT(" + 반대편으로 전진") : TEXT(""));
}

EBTNodeResult::Type USH_BTTask_Search::Begin(UBehaviorTreeComponent& /*OwnerComp*/, ASH_RLCharacter& Unit)
{
	// 같은 유닛도 탐색할 때마다 방향을 바꾼다 — 한쪽만 돌면 반대쪽 적을 늦게 찾는다.
	SweepSign = (SweepSign > 0.0f ? -1.0f : 1.0f) * SHBot::PerUnitSign(Unit);
	// 유닛 id 로 고정된 배율(전역 난수를 쓰지 않는다) — 0.65~1.35 사이로 어긋난다.
	const float Hash = static_cast<float>((Unit.GetUniqueID() * 2654435761u) % 1000u) / 999.0f;
	SweepScale = 1.0f + SweepDesync * (2.0f * Hash - 1.0f);

	CheckpointLocation = Unit.GetActorLocation();
	CheckpointTime = 0.0f;
	DetourUntil = -1.0f;
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type USH_BTTask_Search::Decide(UBehaviorTreeComponent& /*OwnerComp*/, ASH_RLCharacter& Unit, const float ElapsedSeconds)
{
	if (ElapsedSeconds >= DurationSeconds)
	{
		return EBTNodeResult::Succeeded;
	}

	// --- 막힘 → 옆으로 우회 ---------------------------------------------------
	if (ElapsedSeconds < DetourUntil)
	{
		// 우회 중에는 가는 쪽을 본다 — 돌면서 비비지 않고, 시야도 새 길을 본다.
		Unit.PendingMoveInput = DetourDirection;
		Unit.PendingTurnRateDeg = SHBot::TurnRateFor(Unit, SHBot::YawErrorTo(Unit, Unit.GetActorLocation() + DetourDirection * 500.0));
		return EBTNodeResult::InProgress;
	}
	if (bAdvance && ElapsedSeconds - CheckpointTime >= StuckCheckSeconds)
	{
		const FVector Moved = Unit.GetActorLocation() - CheckpointLocation;
		if (Moved.Size2D() < StuckDistance && !Unit.PendingMoveInput.IsNearlyZero())
		{
			// 가려던 방향의 수직(좌우를 번갈아). 아레나 가장자리면 중심 쪽 성분을 섞어 벽에서 떨어진다.
			const FVector Tried = Unit.PendingMoveInput.GetSafeNormal2D();
			DetourSign = -DetourSign;
			FVector Detour = FVector(-Tried.Y, Tried.X, 0.0) * DetourSign;
			if (const ASH_Arena* Arena = SHBot::GetArena(Unit))
			{
				Detour += SHBot::FlatDirectionTo(Unit, Arena->GetActorLocation()) * 0.5;
			}
			DetourDirection = Detour.GetSafeNormal2D();
			DetourUntil = ElapsedSeconds + DetourSeconds;
		}
		CheckpointLocation = Unit.GetActorLocation();
		CheckpointTime = ElapsedSeconds;
	}

	Unit.PendingTurnRateDeg = FMath::Clamp(SweepSign * SweepRateDeg * SweepScale, -Unit.MaxTurnRateDeg, Unit.MaxTurnRateDeg);

	FVector Move = FVector::ZeroVector;
	if (bAdvance)
	{
		if (const ASH_Arena* Arena = SHBot::GetArena(Unit))
		{
			const FVector Center = Arena->GetActorLocation();
			const FVector FromCenter = Unit.GetActorLocation() - Center;
			// 대척점. 중심 근처면 대척점이 의미가 없으니 지금 보는 쪽으로 간다.
			const FVector Goal = FromCenter.Size2D() > 200.0
				? Center - FromCenter
				: Unit.GetActorLocation() + Unit.GetActorForwardVector() * 500.0;
			Move = SHBot::FlatDirectionTo(Unit, Goal);
		}
	}
	Unit.PendingMoveInput = Move;
	return EBTNodeResult::InProgress;
}
