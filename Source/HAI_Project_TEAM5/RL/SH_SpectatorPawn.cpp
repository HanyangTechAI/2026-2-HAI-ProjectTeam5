// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_SpectatorPawn.h"

#include "Components/SphereComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"

ASH_SpectatorPawn::ASH_SpectatorPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// ADefaultPawn 의 레거시 축 바인딩을 끈다. 이 프로젝트에는 대응하는
	// AxisMappings 가 없어서 동작하지 않고, 켜두면 혼란만 준다.
	bAddDefaultMovementBindings = false;

	// 시선이 곧 이동 기준이 되도록 컨트롤 회전을 그대로 따른다.
	bUseControllerRotationPitch = true;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	// 벽과 유닛을 그대로 통과해서 날아다닌다. 관전자가 전투 판정에 끼면 안 된다.
	if (UPrimitiveComponent* Collision = GetCollisionComponent())
	{
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 총알 판정 대상이 되지 않도록 못을 박아 둔다.
	SetActorEnableCollision(false);
	SetCanBeDamaged(false);
}

void ASH_SpectatorPawn::BeginPlay()
{
	Super::BeginPlay();

	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = NormalSpeed;
		// 관성 없이 즉시 서고 즉시 출발 — 에디터 카메라와 같은 느낌.
		Movement->Acceleration = 20000.0f;
		Movement->Deceleration = 20000.0f;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ASH_SpectatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	auto Axis = [PC](const FKey& Positive, const FKey& Negative) -> float
	{
		return (PC->IsInputKeyDown(Positive) ? 1.0f : 0.0f) - (PC->IsInputKeyDown(Negative) ? 1.0f : 0.0f);
	};

	// --- 속도 ---------------------------------------------------------------
	const bool bFast = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
	if (UFloatingPawnMovement* Movement = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = bFast ? FastSpeed : NormalSpeed;
	}

	// --- 시점 ---------------------------------------------------------------
	if (!bRequireRightMouseToLook || PC->IsInputKeyDown(EKeys::RightMouseButton))
	{
		float MouseX = 0.0f, MouseY = 0.0f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		AddControllerYawInput(MouseX * LookSensitivity);
		AddControllerPitchInput(-MouseY * LookSensitivity);
	}

	// --- 이동 ---------------------------------------------------------------
	const FRotator ViewRotation = GetControlRotation();
	const FRotationMatrix ViewMatrix(ViewRotation);

	const float Forward = Axis(EKeys::W, EKeys::S);
	const float Strafe = Axis(EKeys::D, EKeys::A);
	const float Vertical = Axis(EKeys::E, EKeys::Q);

	if (!FMath::IsNearlyZero(Forward))
	{
		AddMovementInput(ViewMatrix.GetUnitAxis(EAxis::X), Forward);
	}
	if (!FMath::IsNearlyZero(Strafe))
	{
		AddMovementInput(ViewMatrix.GetUnitAxis(EAxis::Y), Strafe);
	}
	if (!FMath::IsNearlyZero(Vertical))
	{
		// 상하는 시선과 무관하게 월드 기준 — 에디터 카메라와 같다.
		AddMovementInput(FVector::UpVector, Vertical);
	}
}
