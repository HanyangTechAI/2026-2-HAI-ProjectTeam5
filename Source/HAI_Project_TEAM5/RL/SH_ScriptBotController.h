// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SH_ScriptBotController.generated.h"

class ABase_Character;

/**
 *  학습하지 않는 스크립트 상대역. 커리큘럼 1단계의 대전 상대이자,
 *  RL 없이 전투 시스템(명중·데미지·사망·팀)을 검증하는 수단이다.
 *
 *  ★ 고정 스크립트 상대가 왜 중요한가.
 *  학습하지 않는 상대는 정상(stationary) MDP 를 만든다 — PPO 가 가장 잘하는 경우다.
 *  곧바로 self-play 로 가면 양쪽이 동시에 변해서 "보상 곡선이 영원히 진동"하는
 *  고전적 실패가 난다.
 *
 *  ★ 왜 폰이 아니라 컨트롤러인가.
 *  조준은 SetControlRotation, 이동은 Pawn->AddMovementInput 이다. 컨트롤 회전을
 *  소유한 쪽이 컨트롤러이고, 나중에 RL 정책도 정확히 같은 두 경로로 폰을 움직인다.
 *  스크립트를 컨트롤러에 두면 폰은 양쪽에서 그대로 재사용된다.
 *
 *  난이도 손잡이는 셋이다 — MaxTurnRateDeg / ReactionDelay / AccuracyConeDeg.
 *  이걸 단계적으로 조여서 커리큘럼 사다리로 쓴다.
 *
 *  ★ 시야 제한(bRequireLineOfSight)이 켜져 있으면 봇도 RL 유닛과 같은
 *  USH_VisionComponent 를 통해서만 적을 본다. 봇만 전지적이면 정책은 싸우는 법 대신
 *  그 비대칭을 학습한다. 대신 봇이 약해지므로 **커리큘럼 승률 기준을 다시 잡아야 한다.**
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_ScriptBotController : public AAIController
{
	GENERATED_BODY()

public:
	ASH_ScriptBotController();

	//~ 난이도 손잡이 ----------------------------------------------------------

	/** 최대 조준 각속도(도/초). 낮을수록 약하다. 1단계 하: 120, 상: 300. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "1.0"))
	float MaxTurnRateDeg = 180.0f;

	/** 목표 위치를 다시 읽기까지의 지연(초). 클수록 움직이는 표적을 놓친다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0"))
	float ReactionDelay = 0.25f;

	/** 이 각도(도) 안에 들어와야 발사한다. 클수록 막 쏜다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0"))
	float AccuracyConeDeg = 5.0f;

	//~ 시야 제한 --------------------------------------------------------------

	/**
	 *  표적을 시야 안에 있는 것으로만 제한한다. 폰의 USH_VisionComponent 를 쓴다.
	 *
	 *  ★ 기본값 true. 끄면 예전의 전지적 동작으로 돌아간다 — 벽·엄폐물 뒤 적까지
	 *  정확히 겨눈다. 엄폐물을 넣은 뒤로는 끄면 안 된다.
	 *
	 *  ★ RL 유닛은 16개 레이의 부채꼴로 보고 봇은 점 조회(CanSeeActor)로 본다.
	 *  봇 쪽이 약간 더 정확하지만(레이 사이로 빠지지 않는다) 전지적 상태보다는
	 *  훨씬 작은 차이다. 자세한 내용은 SH_VisionComponent.h 주석 참조.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot")
	bool bRequireLineOfSight = true;

	/** 표적을 잃은 뒤 이 시간까지는 마지막으로 본 지점을 계속 겨눈다(초).
	 *  적이 엄폐물 뒤로 들어간 직후 다시 나오는 것을 노린다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0"))
	float SearchSweepDelay = 1.0f;

	/** 그 뒤로는 선회로 훑는다. 그 기준 각속도(도/초).
	 *  ★ 탐색을 안 하면 시야 밖 적을 영영 못 찾고 봇이 얼어붙는다 —
	 *  그러면 정책이 "봇의 시야 밖에 머무른다" 를 학습해 커리큘럼이 무의미해진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "1.0"))
	float SearchSweepRateDeg = 120.0f;

	/**
	 *  선회 속도를 유닛마다 이 비율만큼 어긋나게 한다 (0.3 = ±30%).
	 *
	 *  ★★ 실측으로 확인된 필수 항목이다. 모든 봇이 **같은 속도로 같은 방향** 으로 돌면
	 *  두 봇 사이의 상대 방위가 고정되어 **영원히 서로를 보지 못한다.**
	 *  96명이 동시에 90도 콘을 벗어난 채 40초를 버틴 실측이 그 결과였다
	 *  (우연이라면 0.75^96, 사실상 0).
	 *
	 *  배율은 폰의 고유 id 에서 뽑는다 — 전역 RNG 를 쓰지 않아 같은 시드면 재현된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float SearchSweepDesync = 0.35f;

	/**
	 *  탐색 중 전진할지.
	 *
	 *  ★ 실측으로 확인된 필수 항목이다. 제자리 회전만 시키면
	 *  엄폐물로 시야가 막힌 짝이 **영원히 교착한다** —
	 *  320유닛 / 엄폐물 7개 / 시야 360도에서 96명만 죽고 224명이 7초 후 완전 정지했다
	 *  (시야 90도에서는 55~71명). 봇이 얼어붙으면 에피소드가 보상 신호를 만들지 못한다.
	 *
	 *  ★★ 목표를 **아레나 중심으로 두면 안 된다.** 실측에서 전 유닛이 반경 700 부근에
	 *  모였고 그게 바로 엄폐물이 깔린 고리여서, 적을 정면에 둬도 항상 엄폐물이 막혔다
	 *  (표적 보유 0/256, 가려짐 판정 100%). 그래서 목표는 **아레나 반대편**이다 —
	 *  매 프레임 자기 위치의 대척점을 다시 계산하므로 계속 가로지르고,
	 *  엄폐물 사이에 주차하지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot")
	bool bAdvanceWhileSearching = true;

	/** 표적을 잃은 뒤 이 시간까지는 마지막으로 본 지점으로 이동한다(초).
	 *  그 뒤로는 포기하고 아레나를 가로지른다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0"))
	float SearchCommitSeconds = 3.0f;

	//~ 전술 ------------------------------------------------------------------

	/** 유지하려는 교전 거리(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0"))
	float DesiredRange = 900.0f;

	/** 이 폭 안이면 접근/후퇴 대신 좌우로 선회한다(cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0"))
	float RangeTolerance = 200.0f;

	/** 표적 탐색 반경(cm).
	 *  ★ 아레나 피치(5000)보다 작게 둬야 옆 아레나 유닛을 표적으로 잡지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.0"))
	float TargetSearchRadius = 4000.0f;

	/** 교전 거리에서 좌우 선회를 할지. 끄면 제자리에 선다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot")
	bool bStrafe = true;

	/** 선회 방향을 뒤집는 주기(초). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot", meta = (ClampMin = "0.1"))
	float StrafeFlipInterval = 2.0f;

	//~ 디버그 ----------------------------------------------------------------

	/** 표적 선, 조준 콘, 이동 의도를 그린다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Bot|Debug")
	bool bDrawDebug = false;

	//~ AAIController ---------------------------------------------------------

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaTime) override;

	/** 현재 표적. 없으면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "SH|Bot")
	ABase_Character* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** 표적을 잃은 뒤 흐른 시간(초). 진단용 — 선회 모드에 들어갔는지 본다. */
	UFUNCTION(BlueprintPure, Category = "SH|Bot")
	float GetTimeSinceTargetLost() const { return TimeSinceTargetLost; }

	/** 선회 탐색 중인가 (표적 없음 + SearchSweepDelay 경과). */
	UFUNCTION(BlueprintPure, Category = "SH|Bot")
	bool IsSweeping() const
	{
		return !CurrentTarget.IsValid()
			&& (TimeSinceTargetLost >= SearchSweepDelay || !bEverAcquiredTarget);
	}

protected:
	/** 탐색 반경 안에서 가장 가까운 살아있는 **보이는** 적대 유닛. */
	ABase_Character* FindNearestHostile(const ABase_Character* Self) const;

	/** 표적이 없을 때의 행동 — 마지막으로 본 지점 유지, 그 뒤 선회 + 전진. */
	void UpdateSearchBehavior(ABase_Character* Self, const FVector& Origin, float DeltaTime);

	TWeakObjectPtr<ABase_Character> CurrentTarget;

	/** 마지막으로 읽은 표적 위치. ReactionDelay 동안 갱신되지 않아 반응 지연을 만든다. */
	FVector LastKnownAimPoint = FVector::ZeroVector;

	float TimeSinceRetarget = 0.0f;
	float TimeSinceStrafeFlip = 0.0f;
	float StrafeSign = 1.0f;

	/** 표적을 잃은 뒤 흐른 시간. 표적이 있으면 0 으로 유지된다. */
	float TimeSinceTargetLost = 0.0f;

	/** 탐색 선회 방향. 표적을 잃을 때마다 뒤집어 두 봇이 같은 방향으로만 돌지 않게 한다. */
	float SearchSweepSign = 1.0f;

	/** 한 번이라도 표적을 봤는가. false 면 LastKnownAimPoint 가 자기 위치라 쓸 수 없다. */
	bool bEverAcquiredTarget = false;

	/** 이 봇의 선회 속도 배율. OnPossess 에서 폰 id 로 정한다. */
	float SweepRateScale = 1.0f;
};
