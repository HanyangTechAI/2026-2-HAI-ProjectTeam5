// Fill out your copyright notice in the Description page of Project Settings.

#include "Misc/AutomationTest.h"
#include "RL/SH_ShooterTrainingEnv.h"

#if WITH_DEV_AUTOMATION_TESTS

// 실행: Tools -> Session Frontend -> Automation 에서 TEAM5 검색.
//
// 이 파일이 지키는 것은 하나다: **멀어지는 쪽이 이득이 되면 안 된다.**
// 영상(2026-11-12)에서 양 팀이 라운드 시작부터 반대편 벽에 붙어 20초를 보냈고,
// 학습기 로그의 "최근접 적" 이 1545~2202 cm (아레나 지름 3500) 였다.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSHApproachRewardTest, "TEAM5.Reward.Approach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSHApproachRewardTest::RunTest(const FString& Parameters)
{
	constexpr float W = 1.0f;
	constexpr float R = 3500.0f;     // 아레나 지름
	constexpr float GUARD = 100.0f;  // ApproachMaxStepCm 기본값

	// 전속 접근 한 결정(500 cm/s x 0.05 초 = 25 cm).
	const float Step = SHReward::Approach(2400.0f, 2375.0f, W, R, GUARD);
	TestTrue(TEXT("접근은 양수"), Step > 0.0f);
	TestTrue(TEXT("접근 한 결정의 크기"), FMath::IsNearlyEqual(Step, 25.0f / R, 1e-6f));

	// 같은 거리를 물러나면 정확히 같은 크기의 음수.
	const float Back = SHReward::Approach(2375.0f, 2400.0f, W, R, GUARD);
	TestTrue(TEXT("후퇴는 음수"), Back < 0.0f);
	TestTrue(TEXT("접근과 후퇴가 상쇄된다"), FMath::IsNearlyEqual(Step + Back, 0.0f, 1e-6f));

	// ★ 농사 방지. 붙었다 떨어졌다를 100번 반복해도 합이 0 이다.
	float Farmed = 0.0f;
	for (int32 i = 0; i < 100; ++i)
	{
		Farmed += SHReward::Approach(2400.0f, 1200.0f, W, R, 0.0f);
		Farmed += SHReward::Approach(1200.0f, 2400.0f, W, R, 0.0f);
	}
	TestTrue(TEXT("왕복 농사 불가"), FMath::IsNearlyEqual(Farmed, 0.0f, 1e-4f));

	// 정지는 정확히 0 — 멀리 가만히 서 있는 것에 보상이 새면 안 된다.
	TestTrue(TEXT("정지는 0"),
		FMath::IsNearlyEqual(SHReward::Approach(2200.0f, 2200.0f, W, R, GUARD), 0.0f, 1e-6f));

	// 라운드 전체 합은 (시작 − 끝) 하나로 접힌다. 경로와 무관하다.
	const float Direct = SHReward::Approach(2400.0f, 600.0f, W, R, 0.0f);
	float Piecewise = 0.0f;
	float Prev = 2400.0f;
	const float Path[] = { 2100.0f, 2300.0f, 1500.0f, 900.0f, 1100.0f, 600.0f };
	for (const float D : Path)
	{
		Piecewise += SHReward::Approach(Prev, D, W, R, 0.0f);
		Prev = D;
	}
	TestTrue(TEXT("경로 무관 — 합이 접힌다"), FMath::IsNearlyEqual(Direct, Piecewise, 1e-5f));

	// ★ 텔레포트 방호. 걸어서 닿을 수 없는 변화(스폰 재배치 · 표적 교체)는 0 이어야 한다.
	//   방호가 없으면 아래 한 줄이 결정 하나에 Weight 의 0.51 배를 꽂는다.
	TestTrue(TEXT("텔레포트는 0"),
		FMath::IsNearlyEqual(SHReward::Approach(2400.0f, 600.0f, W, R, GUARD), 0.0f, 1e-6f));
	TestTrue(TEXT("멀어지는 텔레포트도 0"),
		FMath::IsNearlyEqual(SHReward::Approach(600.0f, 2400.0f, W, R, GUARD), 0.0f, 1e-6f));
	// 경계: 상한 이하는 통과한다 (양쪽이 마주 달리는 최대 50 cm 를 포함해야 한다).
	TestTrue(TEXT("상한 이하는 통과"), SHReward::Approach(2400.0f, 2350.0f, W, R, GUARD) > 0.0f);
	TestTrue(TEXT("방호 0 이면 끈다"),
		FMath::IsNearlyEqual(SHReward::Approach(2400.0f, 600.0f, W, R, 0.0f), 1800.0f / R, 1e-6f));

	// 0 으로 나누기 방어.
	TestTrue(TEXT("RangeCm 0 이면 0"),
		FMath::IsNearlyEqual(SHReward::Approach(2400.0f, 0.0f, W, 0.0f, GUARD), 0.0f, 1e-6f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSHProximityShapingTest, "TEAM5.Reward.Proximity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSHProximityShapingTest::RunTest(const FString& Parameters)
{
	constexpr float R = 3500.0f;

	TestTrue(TEXT("붙으면 1"), FMath::IsNearlyEqual(SHReward::Proximity(0.0f, R), 1.0f, 1e-6f));
	TestTrue(TEXT("지름이면 0"), FMath::IsNearlyEqual(SHReward::Proximity(R, R), 0.0f, 1e-6f));
	TestTrue(TEXT("사거리(6000)는 0 으로 클램프"),
		FMath::IsNearlyEqual(SHReward::Proximity(6000.0f, R), 0.0f, 1e-6f));
	TestTrue(TEXT("음수 거리도 1 로 클램프"),
		FMath::IsNearlyEqual(SHReward::Proximity(-10.0f, R), 1.0f, 1e-6f));

	// 단조 감소.
	float Prev = SHReward::Proximity(0.0f, R);
	for (float D = 100.0f; D <= R; D += 100.0f)
	{
		const float Now = SHReward::Proximity(D, R);
		TestTrue(TEXT("거리가 늘면 가중이 준다"), Now < Prev);
		Prev = Now;
	}

	// ★ 회귀 방지의 핵심. 영상에서 실제로 서 있던 거리(2202 cm)의 셰이핑이
	//   교전 거리(600 cm)의 절반보다 작아야 "벽에 붙어 수확" 이 손해가 된다.
	const float AtWall  = SHReward::Proximity(2202.0f, R);
	const float AtClose = SHReward::Proximity(600.0f, R);
	TestTrue(TEXT("벽 거리 셰이핑 < 교전 거리의 절반"), AtWall < 0.5f * AtClose);

	// 수정 전에는 둘 다 1.0 이었다 — 거리를 전혀 보지 않았다.
	TestTrue(TEXT("벽 거리에서 전액 수확 불가"), AtWall < 0.5f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
