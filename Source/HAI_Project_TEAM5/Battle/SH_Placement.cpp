// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_Placement.h"

#include "RL/SH_RLCharacter.h"

ESHPlaceResult FSHPlacement::Add(ASH_RLCharacter* Unit, const FIntPoint Cell)
{
	if (IsFull())
	{
		return ESHPlaceResult::Full;
	}
	if (FindUnit(Cell))
	{
		return ESHPlaceResult::Occupied;
	}
	CellToUnit.Add(Cell, Unit);
	return ESHPlaceResult::Ok;
}

ESHPlaceResult FSHPlacement::Move(const ASH_RLCharacter* Unit, const FIntPoint ToCell)
{
	const TOptional<FIntPoint> FromCell = FindCell(Unit);
	if (!FromCell.IsSet())
	{
		return ESHPlaceResult::NotPlaced;
	}
	if (FromCell.GetValue() == ToCell)
	{
		return ESHPlaceResult::Ok;
	}
	if (FindUnit(ToCell))
	{
		return ESHPlaceResult::Occupied;
	}

	TWeakObjectPtr<ASH_RLCharacter> Moved = CellToUnit.FindAndRemoveChecked(FromCell.GetValue());
	CellToUnit.Add(ToCell, MoveTemp(Moved));
	return ESHPlaceResult::Ok;
}

ESHPlaceResult FSHPlacement::Remove(const ASH_RLCharacter* Unit)
{
	const TOptional<FIntPoint> Cell = FindCell(Unit);
	if (!Cell.IsSet())
	{
		return ESHPlaceResult::NotPlaced;
	}
	CellToUnit.Remove(Cell.GetValue());
	return ESHPlaceResult::Ok;
}

ASH_RLCharacter* FSHPlacement::FindUnit(const FIntPoint Cell) const
{
	const TWeakObjectPtr<ASH_RLCharacter>* Found = CellToUnit.Find(Cell);
	return Found ? Found->Get() : nullptr;
}

TOptional<FIntPoint> FSHPlacement::FindCell(const ASH_RLCharacter* Unit) const
{
	if (!Unit)
	{
		return {};
	}
	// 유닛이 최대 10명이라 선형 탐색으로 충분하다. 역방향 맵은 불변식만 하나 더 늘린다.
	for (const TPair<FIntPoint, TWeakObjectPtr<ASH_RLCharacter>>& Pair : CellToUnit)
	{
		if (Pair.Value.Get() == Unit)
		{
			return Pair.Key;
		}
	}
	return {};
}

void FSHPlacement::ForEach(TFunctionRef<void(FIntPoint, ASH_RLCharacter&)> Visit) const
{
	for (const TPair<FIntPoint, TWeakObjectPtr<ASH_RLCharacter>>& Pair : CellToUnit)
	{
		if (ASH_RLCharacter* Unit = Pair.Value.Get())   // 파괴된 유닛은 건너뛴다
		{
			Visit(Pair.Key, *Unit);
		}
	}
}
