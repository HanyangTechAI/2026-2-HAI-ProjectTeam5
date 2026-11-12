// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BoardGrid.h"

FSHBoardGrid FSHBoardGrid::Covering(const double Radius, const double InCellSize)
{
	FSHBoardGrid Result;
	Result.CellSize = FMath::Max(InCellSize, 1.0);

	// 한쪽 절반에 필요한 칸 수를 올림한 뒤 두 배 → 항상 짝수.
	const int32 HalfCount = FMath::Max(1, FMath::CeilToInt32(Radius / Result.CellSize));
	Result.Columns = 2 * HalfCount;
	Result.Rows    = 2 * HalfCount;

	const double HalfExtent = static_cast<double>(HalfCount) * Result.CellSize;
	Result.Origin = FVector2D(-HalfExtent, -HalfExtent);
	return Result;
}

bool FSHBoardGrid::IsInside(const FIntPoint Cell) const
{
	return Cell.X >= 0 && Cell.X < Columns && Cell.Y >= 0 && Cell.Y < Rows;
}

FVector2D FSHBoardGrid::CellCenter(const FIntPoint Cell) const
{
	return FVector2D(
		Origin.X + (static_cast<double>(Cell.X) + 0.5) * CellSize,
		Origin.Y + (static_cast<double>(Cell.Y) + 0.5) * CellSize);
}

TOptional<FIntPoint> FSHBoardGrid::LocalToCell(const FVector2D Local) const
{
	// ★ static_cast<int32>(-0.3) 은 0 이다(0 쪽으로 자른다). 격자 바로 바깥(왼쪽) 점이 0번 칸으로 잘못 들어온다.
	//   내림(Floor)을 써야 −1 이 되어 IsInside 에서 걸러진다.
	const FIntPoint Cell{
		FMath::FloorToInt32((Local.X - Origin.X) / CellSize),
		FMath::FloorToInt32((Local.Y - Origin.Y) / CellSize) };

	if (!IsInside(Cell))
	{
		return {};
	}
	return Cell;
}
