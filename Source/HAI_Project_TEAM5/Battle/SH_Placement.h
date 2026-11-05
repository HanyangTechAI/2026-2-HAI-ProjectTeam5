// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ASH_RLCharacter;

enum class ESHPlaceResult : uint8
{
	Ok,
	Occupied,   // 다른 유닛이 있다
	Full,       // 이미 최대 인원
	NotPlaced,  // 이동/제거하려는 유닛이 배치되어 있지 않다
};

/**
 *  어느 칸에 어느 유닛이 있는가. 지형(막힌 칸, 아군 구역)은 모른다 — 그 판단은 보드가 한다.
 *
 *  불변식: 한 유닛은 최대 한 칸에 있고, 한 칸에는 최대 한 유닛이 있다.
 *  유닛의 수명을 소유하지 않으므로 약한 참조로 들고 있다.
 */
class FSHPlacement
{
public:
	explicit FSHPlacement(int32 InMaxUnits) : MaxUnits(InMaxUnits) {}

	/** 새 유닛을 빈 칸에 둔다. */
	[[nodiscard]] ESHPlaceResult Add(ASH_RLCharacter* Unit, FIntPoint Cell);

	/** 배치된 유닛을 다른 빈 칸으로 옮긴다. 같은 칸이면 Ok. */
	[[nodiscard]] ESHPlaceResult Move(const ASH_RLCharacter* Unit, FIntPoint ToCell);

	[[nodiscard]] ESHPlaceResult Remove(const ASH_RLCharacter* Unit);

	[[nodiscard]] ASH_RLCharacter* FindUnit(FIntPoint Cell) const;
	[[nodiscard]] TOptional<FIntPoint> FindCell(const ASH_RLCharacter* Unit) const;

	[[nodiscard]] int32 Num() const { return CellToUnit.Num(); }
	[[nodiscard]] int32 GetMaxUnits() const { return MaxUnits; }
	[[nodiscard]] bool IsFull() const { return Num() >= MaxUnits; }

	/** 배치된 (칸, 유닛) 을 하나씩 넘겨준다. 내부 TMap 을 밖에 내주지 않는다. */
	void ForEach(TFunctionRef<void(FIntPoint, ASH_RLCharacter&)> Visit) const;

private:
	TMap<FIntPoint, TWeakObjectPtr<ASH_RLCharacter>> CellToUnit;
	int32 MaxUnits = 10;
};
