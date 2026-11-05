// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"

/**
 *  보드 격자. 좌표는 보드 액터 기준 로컬(XY, cm). UObject 가 아니다 — 리플렉션 매크로가 없다.
 *
 *  기본값은 아레나 반경 1,750 cm 를 덮는 14 x 14 칸이다(3,500 / 250 = 14).
 *  ★ 보드를 회전시켜 배치하지 않는다는 가정이다(로컬 = 월드 − 보드 위치).
 */
struct FSHBoardGrid
{
	static constexpr double DefaultCellSize = 250.0;

	FVector2D Origin{ -1750.0, -1750.0 };   // (0,0) 칸의 바깥 모서리
	double    CellSize = DefaultCellSize;
	int32     Columns  = 14;                // X 방향 칸 수
	int32     Rows     = 14;                // Y 방향 칸 수

	[[nodiscard]] bool IsInside(FIntPoint Cell) const;
	[[nodiscard]] FVector2D CellCenter(FIntPoint Cell) const;

	/** 격자 밖이면 빈 값. */
	[[nodiscard]] TOptional<FIntPoint> LocalToCell(FVector2D Local) const;

	[[nodiscard]] static constexpr int32 ToIndex(int32 X, int32 Y, int32 InColumns) noexcept
	{
		return Y * InColumns + X;
	}
};

static_assert(FSHBoardGrid::ToIndex(0, 0, 14) == 0);
static_assert(FSHBoardGrid::ToIndex(1, 2, 14) == 29);
