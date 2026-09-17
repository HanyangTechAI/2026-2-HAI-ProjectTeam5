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

protected:
	/** 탐색 반경 안에서 가장 가까운 살아있는 적대 유닛. */
	ABase_Character* FindNearestHostile(const ABase_Character* Self) const;

	TWeakObjectPtr<ABase_Character> CurrentTarget;

	/** 마지막으로 읽은 표적 위치. ReactionDelay 동안 갱신되지 않아 반응 지연을 만든다. */
	FVector LastKnownAimPoint = FVector::ZeroVector;

	float TimeSinceRetarget = 0.0f;
	float TimeSinceStrafeFlip = 0.0f;
	float StrafeSign = 1.0f;
};
