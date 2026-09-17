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
#include "Combat/CombatStatsComponent.h"

ABase_Character::ABase_Character()
{
	CombatStats = CreateDefaultSubobject<UCombatStatsComponent>(TEXT("CombatStats"));

	// --- 총알 판정은 캡슐만 맞는다 -------------------------------------------
	// "Bullet" 채널(ECC_GameTraceChannel1)은 DefaultResponse=Block 이라 아무것도
	// 안 하면 스켈레탈 메시도 총알을 막는다. 본 단위 판정은 애니메이션 포즈에
	// 의존해서 결정론이 깨지므로, 메시는 무시하고 캡슐만 막게 한다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);

	// 오프스크린 / -nullrhi 에서 포즈 틱이 멈추면 소켓 위치가 얼어붙는다.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
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
	}

	if (CombatStats)
	{
		CombatStats->OnDeath.AddDynamic(this, &ABase_Character::HandleDeath);
	}
}

void ABase_Character::HandleDeath(AActor* Victim, AActor* Causer)
{
	UE_LOG(LogTemp, Display, TEXT("[%s] 사망 — 가해자 %s"),
		*GetName(), Causer ? *Causer->GetName() : TEXT("(없음)"));

	// 총알이 시체를 통과하도록. 리셋이 되돌린다.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
}

void ABase_Character::ResetForNewMatch()
{
	if (CombatStats)
	{
		CombatStats->ResetForNewMatch();
	}
	if (gun)
	{
		gun->ResetForNewMatch();
	}

	// ★ 캡슐 콜리전 재활성화. 빠뜨리면 부활한 유닛을 총알이 통과한다 —
	//   화면으로는 멀쩡해 보여서 눈으로는 절대 못 찾는다.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	GetCharacterMovement()->StopMovementImmediately();

	// ★ 여기서 StopFocus() 를 부르면 안 된다.
	//   StopFocus 는 bOrientRotationToMovement=true / bUseControllerDesiredRotation=false 로
	//   되돌리는데, AI 유닛은 ASH_ScriptBotController::OnPossess 가 정반대로 설정해 둔 상태다.
	//   리셋할 때마다 조준 회전 모드가 꺼져서 봇이 표적을 영영 못 겨눈다.
	//   회전 모드의 소유권은 컨트롤러에 있다 — 여기서는 플래그만 내린다.
	bIsFocusing = false;

	SetActorHiddenInGame(false);
}

//~ ILearningAgentsEntityInterface --------------------------------------------

void ABase_Character::InitializeEntity_Implementation(ALearningAgentsGymBase* InGym)
{
	CachedGym = InGym;
	bEntityEnabled = true;
}

void ABase_Character::ResetEntity_Implementation(ALearningAgentsGymBase* InGym)
{
	CachedGym = InGym;
	ResetForNewMatch();
}

ALearningAgentsGymBase* ABase_Character::GetGym_Implementation() const
{
	return CachedGym;
}

void ABase_Character::EnableEntity_Implementation()
{
	bEntityEnabled = true;

	SetActorHiddenInGame(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetActorTickEnabled(true);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// 컨트롤러는 폰과 별개로 틱한다. 같이 켜주지 않으면 몸만 살아나고 행동은 멈춘 채로 남는다.
	if (AController* C = GetController())
	{
		C->SetActorTickEnabled(true);
	}
}

void ABase_Character::DisableEntity_Implementation()
{
	bEntityEnabled = false;

	// ★ Destroy() 를 부르지 않는다. 풀이 이 액터를 다음 에피소드에 그대로 재사용하고,
	//   Manager 가 들고 있는 에이전트 포인터도 이 액터를 가리킨 채로 남아 있다.
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	if (AController* C = GetController())
	{
		C->SetActorTickEnabled(false);
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
	if (!gun)
	{
		return;
	}

	const FGunFireResult Result = gun->PullTrigger();
	if (!Result.bFired)
	{
		return;   // 쿨다운 중
	}

	UE_LOG(LogTemp, Display, TEXT("[%s] 발사 — 명중:%s 적대:%s 데미지:%.0f 대상:%s"),
		*GetName(),
		Result.bHitSomething ? TEXT("O") : TEXT("X"),
		Result.bHitHostile ? TEXT("O") : TEXT("X"),
		Result.DamageApplied,
		Result.HitActor ? *Result.HitActor->GetName() : TEXT("(없음)"));
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
