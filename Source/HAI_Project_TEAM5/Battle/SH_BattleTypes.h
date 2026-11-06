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
