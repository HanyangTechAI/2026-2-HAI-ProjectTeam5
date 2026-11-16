// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/** 게임의 단계. Placement → Battle → Result → Placement 만 허용된다. */
enum class ESHBattlePhase : uint8
{
	Placement,
	Battle,
	Result,
};

/** 한 팀을 무엇이 조종하는가. 배치 단계에서 팀마다 고른다(HUD 드롭다운). */
enum class ESHTeamControl : uint8
{
	RLModel,        // 학습된 정책(ASH_BattleBrain)
	BehaviorTree,   // 행동 트리(ASH_BTBotController + BT_SHCombat)
};

[[nodiscard]] constexpr const TCHAR* ToDisplayText(const ESHTeamControl Control) noexcept
{
	return Control == ESHTeamControl::RLModel ? TEXT("RL 모델") : TEXT("행동 트리");
}

/** 허용되는 전이. constexpr 이라 static_assert 로 컴파일 타임 검사를 할 수 있다. */
[[nodiscard]] constexpr bool IsAllowedTransition(const ESHBattlePhase From, const ESHBattlePhase To) noexcept
{
	switch (From)
	{
	case ESHBattlePhase::Placement: return To == ESHBattlePhase::Battle;
	case ESHBattlePhase::Battle:    return To == ESHBattlePhase::Result;
	case ESHBattlePhase::Result:    return To == ESHBattlePhase::Placement;
	}
	return false;
}

static_assert(IsAllowedTransition(ESHBattlePhase::Placement, ESHBattlePhase::Battle));
static_assert(IsAllowedTransition(ESHBattlePhase::Battle, ESHBattlePhase::Result));
static_assert(IsAllowedTransition(ESHBattlePhase::Result, ESHBattlePhase::Placement));
static_assert(!IsAllowedTransition(ESHBattlePhase::Placement, ESHBattlePhase::Result));
static_assert(!IsAllowedTransition(ESHBattlePhase::Battle, ESHBattlePhase::Placement));
