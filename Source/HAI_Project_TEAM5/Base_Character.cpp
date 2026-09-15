// Fill out your copyright notice in the Description page of Project Settings.


#include "Base_Character.h"
#include "HAI_Project_TEAM5Character.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "HAI_Project_TEAM5Character.h"
#include "Components/SkeletalMeshComponent.h"

ABase_Character::ABase_Character()
{
	
}
void ABase_Character::BeginPlay()
{
	Super::BeginPlay();

	GetMesh()->HideBoneByName("weapon_r",EPhysBodyOp::PBO_None);
	gun = GetWorld()->SpawnActor<AGun>(GunClass);
	if (gun)
	{
		gun->SetOwner(this);
		gun->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,TEXT("WeaponSocket"));
		gun->Controller = GetController();
	}
}
void ABase_Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		EnhancedInputComponent->BindAction(ShootAction, ETriggerEvent::Started, this, &ABase_Character::Shoot);
		EnhancedInputComponent->BindAction(FocusAction, ETriggerEvent::Started,   this, &ABase_Character::StartFocus);
		EnhancedInputComponent->BindAction(FocusAction, ETriggerEvent::Completed,   this, &ABase_Character::StopFocus);
	}
}
void ABase_Character::Shoot()
{
	if (gun)
		gun->PullTrigger();
}
void ABase_Character::StartFocus()
{
	bIsFocusing = true;
	GetCharacterMovement()->bOrientRotationToMovement    = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = true;   // ← RotationRate로 추종
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	UE_LOG(LogTemp, Warning, TEXT("Starting Focus"));
}
void ABase_Character::StopFocus()
{
	bIsFocusing = false;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement    = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);
	UE_LOG(LogTemp, Warning, TEXT("Stopping Focus"));
}
