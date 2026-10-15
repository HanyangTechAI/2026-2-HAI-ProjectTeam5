// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SH_RLBotController.generated.h"

/**
 *  정책이 조종하는 유닛의 컨트롤러. **행동을 짜지 않는다.**
 *
 *  ★ 왜 필요한가 — 두 가지를 세팅해야 하는데 둘 다 컨트롤러의 소유다.
 *
 *  1) bSetControlRotationFromPawnOrientation = false
 *     이걸 끄지 않으면 AAIController 가 매 프레임 컨트롤 회전을 폰의 방향으로 덮어쓴다.
 *     그러면 SetControlRotation 이 **아무 효과도 없으면서 에러도 나지 않는다.**
 *     조준은 컨트롤 회전이고(Gun.cpp:105) 시야도 컨트롤 회전이므로
 *     (SH_VisionComponent::GetEyeAndAim), 이게 막히면 학습 전체가 무의미해진다.
 *
 *  2) bUseControllerDesiredRotation = true / bOrientRotationToMovement = false
 *     몸통이 컨트롤 회전을 따라가게 한다. 순전히 시각적이다 — 탄도와 시야는 컨트롤
 *     회전을 직접 쓰므로 몸통이 뒤처져도 판정에는 영향이 없다.
 *     ★ 두 플래그를 동시에 켜면 안 된다. CharacterMovementComponent::PhysicsRotation 이
 *     나중에 돌면서 덮어쓴다.
 *
 *  ★ ASH_ScriptBotController 를 재사용하지 않는 이유: 그쪽 Tick 의 스크립트 로직이
 *  정책과 같은 채널(SetControlRotation / AddMovementInput)을 건드려 서로 싸운다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_RLBotController : public AAIController
{
	GENERATED_BODY()

public:
	ASH_RLBotController();

	virtual void OnPossess(APawn* InPawn) override;
};
