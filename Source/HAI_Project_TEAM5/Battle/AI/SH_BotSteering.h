// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class ASH_Arena;
class ASH_RLCharacter;
class UWorld;

/**
 *  행동 트리 봇 공용 상수 · 함수. UObject 가 아니다.
 *
 *  ★ 공정성의 핵심: 행동 트리도 정책과 **같은 손잡이**로 유닛을 움직인다.
 *    이동 = ASH_RLCharacter::PendingMoveInput (월드 공간 단위벡터, 유닛 Tick 이 매 프레임 AddMovementInput)
 *    조준 = ASH_RLCharacter::PendingTurnRateDeg (도/초, |값| ≤ MaxTurnRateDeg, 유닛 Tick 이 DeltaTime 을 곱해 적용)
 *    사격 = AGun::PullTrigger (같은 쿨다운)
 *  그래서 이동 속도 · 회전 속도 상한 · 발사 간격이 RL 과 완전히 같다. 판단 방식만 다르다.
 */
namespace SHBot
{
	/** 블랙보드 키 이름. BB_SHCombat 에 같은 이름 · 타입으로 만들어야 한다(Docs/BehaviorTreeGuide.md). */
	namespace Keys
	{
		inline const FName TargetActor      = TEXT("TargetActor");       // Object(Actor)
		inline const FName HasTarget        = TEXT("HasTarget");         // Bool
		inline const FName RecentlySeen     = TEXT("RecentlySeen");      // Bool
		inline const FName LastSeenLocation = TEXT("LastSeenLocation");  // Vector
		inline const FName UnderFire        = TEXT("UnderFire");         // Bool
		inline const FName ThreatLocation   = TEXT("ThreatLocation");    // Vector
		inline const FName OutsideZone      = TEXT("OutsideZone");       // Bool
		inline const FName ZoneCenter       = TEXT("ZoneCenter");        // Vector
		inline const FName CoverLocation    = TEXT("CoverLocation");     // Vector — SH Find Cover 가 고른 엄폐물 끝
		inline const FName PeekLocation     = TEXT("PeekLocation");      // Vector — 그 끝에서 옆으로 한 걸음(앞을 확인하는 자리)
		inline const FName FrontLocation    = TEXT("FrontLocation");     // Vector — 적이 있다고 보는 쪽(바라볼 곳)
	}

	/**
	 *  엄폐물 모서리 하나 — 유닛이 설 수 있는 자리.
	 *  Location = 모서리에서 대각선 바깥으로 CornerOffset 만큼 나간 점(캡슐이 엄폐물에 닿지 않는 거리).
	 */
	struct FCoverCorner
	{
		FVector Location = FVector::ZeroVector;
		FVector CoverCenter = FVector::ZeroVector;   // 그 엄폐물의 중심
		FVector Outward = FVector::ZeroVector;       // 엄폐물 중심 → 모서리 수평 단위벡터
		int32 CoverIndex = INDEX_NONE;               // 같은 엄폐물끼리 구별용
	};

	/**
	 *  아레나(보드)의 엄폐물 모서리 전부. 바닥 · 경계 벽(Wall_xx)은 뺀다.
	 *  ★ 엄폐물 = 보드 액터의 콜리전이 켜진 StaticMeshComponent(BP 의 Cover_* · Pillar_* · Ring_* …).
	 *  꺼진 보드(bActiveBoard=false)의 컴포넌트는 콜리전이 꺼져 있어 자동으로 빠진다.
	 */
	[[nodiscard]] TArray<FCoverCorner> GatherCoverCorners(const ASH_Arena& Arena, float CornerOffset, float Z);

	/** 두 점 사이 직선에 벽 · 엄폐물(WorldStatic)이 있는가 — 눈높이에서 본다. 유닛은 무시한다. */
	[[nodiscard]] bool IsLineBlockedByStatic(const UWorld& World, const FVector& From, const FVector& To);

	/** 유닛 캡슐이 From → To 로 직선 이동할 때 벽 · 엄폐물에 걸리는가. */
	[[nodiscard]] bool IsPathBlocked(const UWorld& World, const FVector& From, const FVector& To);

	/** 그 자리에 캡슐을 세우면 벽 · 엄폐물과 겹치는가. */
	[[nodiscard]] bool IsSpotBlocked(const UWorld& World, const FVector& Location);

	/** 그 자리 근처(Radius cm)에 Self 말고 다른 유닛이 있는가 — 엄폐물 하나에 몰리지 않게. */
	[[nodiscard]] bool IsSpotCrowded(const UWorld& World, const FVector& Location, float Radius, const AActor* Self);

	/** 아는 적이 없을 때 적이 있다고 볼 곳: 상대 진영 쪽(팀 0 은 +X, 팀 1 은 −X) 아레나 반경의 60% 지점. */
	[[nodiscard]] FVector EnemySidePoint(const ASH_RLCharacter& Unit);

	/** 정책과 같은 판단 주기(20 Hz). */
	inline constexpr float DecisionPeriod = 1.0f / 20.0f;

	[[nodiscard]] bool IsAlive(const ASH_RLCharacter& Unit);

	/** 유닛이 속한 아레나(보드). 없으면 nullptr. */
	[[nodiscard]] const ASH_Arena* GetArena(const ASH_RLCharacter& Unit);

	/** 지금 조준(컨트롤 회전)에서 Point 를 보려면 돌아야 하는 각(도, −180~180, 왼쪽 음수). */
	[[nodiscard]] float YawErrorTo(const ASH_RLCharacter& Unit, const FVector& Point);

	/**
	 *  YawError 만큼 다음 결정 주기 안에 돌기 위한 각속도. MaxTurnRateDeg 로 자른다.
	 *  ★ 정책도 같은 상한을 받는다 — 행동 트리만 순간 회전하면 비교가 성립하지 않는다.
	 */
	[[nodiscard]] float TurnRateFor(const ASH_RLCharacter& Unit, float YawErrorDeg);

	/** Point 쪽 수평 단위벡터. 너무 가까우면 0. */
	[[nodiscard]] FVector FlatDirectionTo(const ASH_RLCharacter& Unit, const FVector& Point);

	/** 이동 · 조준 의도를 지운다. 태스크가 끝나거나 중단될 때 부른다 — 안 지우면 마지막 방향으로 계속 걷는다. */
	void Stop(ASH_RLCharacter& Unit);

	/** 유닛마다 다른 −1/+1. 모든 봇이 같은 방향으로 돌면 서로를 영영 못 보는 교착이 생긴다(스크립트 봇 실측). */
	[[nodiscard]] float PerUnitSign(const ASH_RLCharacter& Unit);
}
