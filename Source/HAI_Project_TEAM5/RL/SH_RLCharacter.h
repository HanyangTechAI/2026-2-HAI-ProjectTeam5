// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Base_Character.h"
#include "SH_RLCharacter.generated.h"

class USH_VisionComponent;

/**
 *  학습용 유닛. ABase_Character 를 상속해 RL 에 필요한 것만 얹는다.
 *
 *  ★ 왜 ABase_Character 를 고치지 않고 상속하는가.
 *  base 는 플레이어가 조종하는 캐릭터로도 쓰인다. 학습용 최적화(애니메이션 정지 등)와
 *  결정 주기 입력 래칭이 거기 섞이면 게임 쪽에 영향이 간다. 전투 · 팀 · 엔티티 인터페이스 ·
 *  에피소드 리셋은 이미 base 에 다 있고 그대로 상속된다.
 *
 *  ★ 사망 훅은 base 를 건드리지 않고 붙인다.
 *  ABase_Character::HandleDeath 는 virtual 이 아니지만, FOnCombatDeath 가 멀티캐스트라
 *  (CombatStatsComponent.h:11) 자식이 OnDeath 에 핸들러를 하나 더 등록하면 된다.
 *  등록 순서대로 실행되므로 base 의 정리(콜리전 해제 · 이동 정지)가 먼저 끝난다.
 *
 *  ★ 두 팀 BP 가 모두 이 클래스를 상속해도 된다. 엔진 풀의 제약은
 *  "두 팀이 서로 다른 클래스" 일 뿐이고 상속 관계에는 제약이 없다 (SH_Arena.h:84).
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_RLCharacter : public ABase_Character
{
	GENERATED_BODY()

public:
	ASH_RLCharacter();

	//~ 센서 ------------------------------------------------------------------

	/** 시야 제한 센서. 관측 · 보상 · 스크립트 봇의 표적 조건이 모두 이걸 본다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SH|RL")
	TObjectPtr<USH_VisionComponent> Vision;

	//~ 정책이 쓰는 값 ---------------------------------------------------------

	/**
	 *  정책이 낸 이동 의도. 월드 공간 단위벡터. PerformAgentAction 이 쓰고 Tick 이 소비한다.
	 *
	 *  ★ 왜 여기에 들고 있는가 — 이 프로젝트에서 가장 조용히 망가지는 부분이다.
	 *  AddMovementInput 이 쌓은 벡터는 CharacterMovementComponent::PerformMovement 가
	 *  **매 프레임** ConsumeInputVector() 로 비운다. PerformAgentAction 은 결정 주기마다
	 *  한 번(20Hz)만 불리므로, 거기서 직접 AddMovementInput 을 부르면 60fps 에서
	 *  3프레임 중 1프레임만 입력이 있는 상태가 된다 — 실효 속도가 1/3 로 떨어지고
	 *  프레임률에 따라 흔들린다. 에러도 경고도 나지 않는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SH|RL")
	FVector PendingMoveInput = FVector::ZeroVector;

	/** 정책이 낸 조준 각속도(도/초). 부호 있는 값. Tick 이 DeltaTime 을 곱해 적용한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SH|RL")
	float PendingTurnRateDeg = 0.0f;

	/** 조준 각속도 상한(도/초). 정책의 Turn 행동이 이 값에 곱해진다.
	 *  ASH_RLBotController 가 몸통 추종 속도(RotationRate.Yaw)도 여기서 가져간다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|RL", meta = (ClampMin = "1.0"))
	float MaxTurnRateDeg = 180.0f;

	/** Pending 값을 0으로. 결정 사이에 지난 의도가 이어지지 않게 한다. */
	UFUNCTION(BlueprintCallable, Category = "SH|RL")
	void ClearPendingInput();

	//~ 학습 처리량 ------------------------------------------------------------

	/**
	 *  카메라 · 스프링암 틱을 끈다. 시각에도 영향이 없다 — 아무도 이 카메라를 보지 않는다.
	 *
	 *  USpringArmComponent 는 bDoCollisionTest 가 기본 true 라 **틱마다 스윕 트레이스**를
	 *  한다. 학습 중 유닛 320개면 프레임당 스윕 320회다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|RL|Performance")
	bool bTrainingMode = true;

	/**
	 *  스켈레탈 메시 애니메이션을 정지한다.
	 *
	 *  base 생성자가 VisibilityBasedAnimTickOption 을 AlwaysTickPoseAndRefreshBones 로
	 *  두는데(Base_Character.cpp:31), 그 주석의 근거("총구 소켓 위치가 얼어붙는다")는
	 *  이미 사라졌다 — AGun 은 소켓을 쓰지 않고 GetActorLocation() + MuzzleHeight 와
	 *  컨트롤 회전만 쓴다(Gun.cpp:101-106). 오프스크린 메시 320개의 포즈를 매 프레임
	 *  강제 평가할 이유가 없다.
	 *
	 *  ★ 켜면 관전 시 T-포즈로 보인다. 그래서 bTrainingMode 와 분리해 뒀다 —
	 *  관전하며 디버깅할 때는 이것만 끈다. 탄도 · 판정 · 콜리전에는 영향이 없다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|RL|Performance")
	bool bDisableAnimation = true;

	//~ AActor / ABase_Character ----------------------------------------------

	virtual void Tick(float DeltaSeconds) override;

	/** ★ Super:: 를 반드시 부른다. base 가 HP · 쿨다운 · 콜리전을 되돌린다. */
	virtual void ResetForNewMatch() override;

protected:
	virtual void BeginPlay() override;

	/** base 의 HandleDeath 와 **별개로** OnDeath 에 추가 등록된다. */
	UFUNCTION()
	void HandleRLDeath(AActor* Victim, AActor* Causer);
};
