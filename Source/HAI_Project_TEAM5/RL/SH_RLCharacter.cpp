// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_RLCharacter.h"

#include "Camera/CameraComponent.h"
#include "Combat/CombatStatsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "SH_VisionComponent.h"

ASH_RLCharacter::ASH_RLCharacter()
{
	Vision = CreateDefaultSubobject<USH_VisionComponent>(TEXT("Vision"));

	// ★ Pending 입력을 매 프레임 다시 적용해야 하므로 폰이 틱해야 한다.
	//   컨트롤러(ASH_RLBotController)는 반대로 틱하지 않는다 — 정책은 여기서 실행된다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ASH_RLCharacter::BeginPlay()
{
	// base 가 gun 을 스폰하고 OnDeath 에 자기 핸들러를 먼저 등록한다.
	Super::BeginPlay();

	if (CombatStats)
	{
		// 멀티캐스트라 base 의 HandleDeath 와 둘 다 불린다. base 가 먼저다.
		CombatStats->OnDeath.AddDynamic(this, &ASH_RLCharacter::HandleRLDeath);
	}

	// 시야 센서와 총의 기하가 어긋나면 "보이는데 안 맞는" 구간이 생기고
	// 정책이 그 불일치를 학습한다. 조용히 넘어가면 원인을 영영 못 찾는다.
	if (Vision && gun)
	{
		if (!FMath::IsNearlyEqual(Vision->MaxRange, gun->Range))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_RLCharacter] %s: Vision->MaxRange(%.0f) != Gun->Range(%.0f). ")
				TEXT("시야와 탄도가 어긋난다 — 두 값을 맞출 것."),
				*GetName(), Vision->MaxRange, gun->Range);
		}
		if (!FMath::IsNearlyEqual(Vision->EyeHeight, gun->MuzzleHeight))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_RLCharacter] %s: Vision->EyeHeight(%.0f) != Gun->MuzzleHeight(%.0f). ")
				TEXT("시야와 탄도의 높이가 어긋난다 — 두 값을 맞출 것."),
				*GetName(), Vision->EyeHeight, gun->MuzzleHeight);
		}
	}

	if (bDisableAnimation)
	{
		// NOTE: 지역 변수 이름을 Mesh 로 쓰면 ACharacter::Mesh 멤버를 가려 C4458 이 뜬다
		//       (UE 는 경고를 에러로 취급한다). ScriptBotController 의 Character/Bot 와 같은 함정.
		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			// base 생성자가 걸어 둔 AlwaysTickPoseAndRefreshBones 를 되돌린다.
			// base 를 고치지 않는다 — 런타임 속성이라 여기서 덮어쓰면 된다.
			SkelMesh->VisibilityBasedAnimTickOption =
				EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			SkelMesh->SetComponentTickEnabled(false);
		}
	}

	if (bTrainingMode)
	{
		// 스프링암의 틱마다 도는 스윕 트레이스를 없앤다.
		if (USpringArmComponent* Boom = GetCameraBoom())
		{
			Boom->bDoCollisionTest = false;
			Boom->SetComponentTickEnabled(false);
		}
		if (UCameraComponent* Camera = GetFollowCamera())
		{
			Camera->SetComponentTickEnabled(false);
		}
	}
}

void ASH_RLCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 풀에서 빠졌거나 죽은 유닛은 움직이지 않는다.
	// ★ 여기서 나가기 전에 Pending 을 비우는 것은 HandleRLDeath / ResetForNewMatch 가 한다.
	//   Tick 에서 비우면 "죽은 프레임" 과 "리셋 프레임" 의 순서에 따라 결과가 달라진다.
	if (!bEntityEnabled || !CombatStats || !CombatStats->IsAlive())
	{
		return;
	}

	// --- 이동: 다음 결정이 올 때까지 매 프레임 다시 넣는다 ---------------------
	// CharacterMovementComponent 가 프레임마다 ConsumeInputVector() 로 비우기 때문이다.
	if (!PendingMoveInput.IsNearlyZero())
	{
		AddMovementInput(PendingMoveInput, 1.0f);
	}

	// --- 조준: 각속도 x DeltaTime. 프레임률과 무관해진다 ----------------------
	// ★ 정책이 내는 것은 절대 각도가 아니라 각속도다. 절대 각도를 내게 하면
	//   각속도 제약이 학습에서 사라지고, "제자리에서 훑어보기" 도 표현하기 어려워진다.
	if (!FMath::IsNearlyZero(PendingTurnRateDeg))
	{
		if (AController* OwnerController = GetController())
		{
			FRotator ControlRotation = OwnerController->GetControlRotation();
			ControlRotation.Yaw += PendingTurnRateDeg * DeltaSeconds;
			OwnerController->SetControlRotation(ControlRotation);
		}
	}
}

void ASH_RLCharacter::ClearPendingInput()
{
	PendingMoveInput = FVector::ZeroVector;
	PendingTurnRateDeg = 0.0f;
}

void ASH_RLCharacter::ResetForNewMatch()
{
	// HP 복구 · 누산기 클리어 · 콜리전 재활성화 · 쿨다운 리셋.
	Super::ResetForNewMatch();

	ClearPendingInput();

	// ★ 빠뜨리면 지난 판의 "마지막으로 본 방향" 이 다음 판으로 새어 들어간다.
	if (Vision)
	{
		Vision->ResetMemory();
	}
}

void ASH_RLCharacter::HandleRLDeath(AActor* /*Victim*/, AActor* /*Causer*/)
{
	// base 가 이미 이동을 멈추고 콜리전을 껐다. 의도까지 지워서
	// 부활 전에 Tick 이 한 프레임이라도 밀어내지 않게 한다.
	ClearPendingInput();
}
