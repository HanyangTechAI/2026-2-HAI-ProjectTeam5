// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_RLBotController.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "SH_RLCharacter.h"

ASH_RLBotController::ASH_RLBotController()
{
	// ★ 틱하지 않는다. 정책의 실행은 폰(ASH_RLCharacter::Tick)에서 일어난다.
	//   여기서도 틱하면 320개 컨트롤러가 빈 Tick 을 돈다.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// ★ 헤더의 함정 (1). 끄지 않으면 SetControlRotation 이 조용히 무력화된다.
	bSetControlRotationFromPawnOrientation = false;

	// 조준 방향과 이동 방향이 독립적이어야 한다 (전진하며 옆을 보는 것).
	bAllowStrafe = true;
}

void ASH_RLBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// NOTE: 지역 변수 이름을 Character 로 쓰면 AController::Character 멤버를 가려
	//       C4458 이 뜬다 (UE 는 경고를 에러로 취급한다).
	ASH_RLCharacter* Unit = Cast<ASH_RLCharacter>(InPawn);
	if (!Unit)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLBotController] %s: ASH_RLCharacter 가 아닌 폰(%s)에 빙의했다. ")
			TEXT("BP 유닛의 부모 클래스를 확인할 것."),
			*GetName(), InPawn ? *InPawn->GetName() : TEXT("(없음)"));
		return;
	}

	// 헤더의 함정 (2). 몸통이 컨트롤 회전을 따라가게 한다.
	if (UCharacterMovementComponent* Movement = Unit->GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
		// 시각 전용. 정책의 각속도 상한과 같게 두면 몸통이 조준선을 한 프레임 뒤에서 따라온다.
		Movement->RotationRate = FRotator(0.0f, Unit->MaxTurnRateDeg, 0.0f);
	}

	// 스폰 직후 한 프레임 튀는 것을 막는다 — 아레나가 ApplySpawnSlots 에서
	// 다시 컨트롤 회전을 세팅하지만, 그 사이 프레임의 조준이 엉뚱한 곳을 보지 않게 한다.
	SetControlRotation(Unit->GetActorRotation());
}
