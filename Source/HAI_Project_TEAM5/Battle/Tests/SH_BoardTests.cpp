// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BoardGrid.h"
#include "Battle/SH_Placement.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// 실행: Tools → Session Frontend → Automation 에서 TEAM5 검색.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSHBoardGridTest, "TEAM5.Board.Grid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSHBoardGridTest::RunTest(const FString& Parameters)
{
	const FSHBoardGrid Grid;

	const TOptional<FIntPoint> First = Grid.LocalToCell(FVector2D(-1749.0, -1749.0));
	TestTrue(TEXT("격자 안 첫 칸"), First.IsSet() && First.GetValue() == FIntPoint(0, 0));

	const TOptional<FIntPoint> Last = Grid.LocalToCell(FVector2D(1749.0, 1749.0));
	TestTrue(TEXT("격자 안 마지막 칸"), Last.IsSet() && Last.GetValue() == FIntPoint(13, 13));

	// ★ 내림이 아니라 0 쪽으로 자르면 이 두 줄이 틀린다.
	TestFalse(TEXT("왼쪽 바로 바깥"), Grid.LocalToCell(FVector2D(-1750.1, 0.0)).IsSet());
	TestFalse(TEXT("아래쪽 바로 바깥"), Grid.LocalToCell(FVector2D(0.0, -1750.1)).IsSet());

	TestFalse(TEXT("오른쪽 경계는 밖"), Grid.LocalToCell(FVector2D(1750.0, 0.0)).IsSet());

	const TOptional<FIntPoint> OnEdge = Grid.LocalToCell(FVector2D(-1500.0, -1750.0));
	TestTrue(TEXT("칸 경계는 다음 칸"), OnEdge.IsSet() && OnEdge.GetValue() == FIntPoint(1, 0));

	// 모든 칸에서 칸 → 중심 → 칸 왕복이 같은 칸으로 돌아온다.
	for (int32 Y = 0; Y < Grid.Rows; ++Y)
	{
		for (int32 X = 0; X < Grid.Columns; ++X)
		{
			const FIntPoint Cell(X, Y);
			const TOptional<FIntPoint> Back = Grid.LocalToCell(Grid.CellCenter(Cell));
			TestTrue(FString::Printf(TEXT("왕복 (%d, %d)"), X, Y), Back.IsSet() && Back.GetValue() == Cell);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSHBoardGridCoveringTest, "TEAM5.Board.GridCovering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSHBoardGridCoveringTest::RunTest(const FString& Parameters)
{
	// 학습 아레나 반경이면 기본 격자와 똑같아야 한다 — 기존 적 배치 데이터가 그대로 맞는다.
	const FSHBoardGrid Default;
	const FSHBoardGrid Small = FSHBoardGrid::Covering(1750.0);
	TestEqual(TEXT("1750: 열"), Small.Columns, Default.Columns);
	TestEqual(TEXT("1750: 행"), Small.Rows, Default.Rows);
	TestTrue(TEXT("1750: 원점"), Small.Origin.Equals(Default.Origin));

	// 2450 / 250 = 9.8 → 한쪽 10칸 → 20 x 20, 원점 −2500.
	const FSHBoardGrid Large = FSHBoardGrid::Covering(2450.0);
	TestEqual(TEXT("2450: 열"), Large.Columns, 20);
	TestEqual(TEXT("2450: 행"), Large.Rows, 20);
	TestTrue(TEXT("2450: 원점"), Large.Origin.Equals(FVector2D(-2500.0, -2500.0)));

	// 칸 수가 짝수라 중심(0,0)은 칸 경계 위에 있고, 가운데 두 칸 중심이 ±125 로 대칭이다.
	TestEqual(TEXT("2450: 가운데 서쪽 칸"), Large.CellCenter(FIntPoint(9, 9)).X, -125.0);
	TestEqual(TEXT("2450: 가운데 동쪽 칸"), Large.CellCenter(FIntPoint(10, 10)).X, 125.0);

	// 반경이 칸 크기보다 작아도 최소 2 x 2.
	TestEqual(TEXT("작은 반경"), FSHBoardGrid::Covering(10.0).Columns, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSHPlacementEmptyTest, "TEAM5.Board.PlacementEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSHPlacementEmptyTest::RunTest(const FString& Parameters)
{
	// 유닛이 필요한 규칙(Occupied, Full)은 액터가 있어야 해서 PIE 에서 확인한다.
	FSHPlacement Placement(10);

	TestEqual(TEXT("빈 상태 Num"), Placement.Num(), 0);
	TestFalse(TEXT("빈 상태 IsFull"), Placement.IsFull());
	TestEqual(TEXT("최대 인원"), Placement.GetMaxUnits(), 10);
	TestNull(TEXT("빈 칸의 유닛"), Placement.FindUnit(FIntPoint(0, 0)));
	TestFalse(TEXT("없는 유닛의 칸"), Placement.FindCell(nullptr).IsSet());
	TestTrue(TEXT("없는 유닛 제거"), Placement.Remove(nullptr) == ESHPlaceResult::NotPlaced);
	TestTrue(TEXT("없는 유닛 이동"), Placement.Move(nullptr, FIntPoint(1, 1)) == ESHPlaceResult::NotPlaced);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
