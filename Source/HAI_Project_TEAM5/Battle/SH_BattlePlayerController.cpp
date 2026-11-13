// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BattlePlayerController.h"

#include "Battle/BattleHUDWidget.h"
#include "Battle/SH_BattleBoard.h"
#include "Battle/SH_BattleGameMode.h"
#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputAction.h"
#include "InputMappingContext.h"

namespace
{
	const FName BattleCameraTag(TEXT("BattleCamera"));
}

ASH_BattlePlayerController::ASH_BattlePlayerController()
{
	// 폰이 없으니 엔진이 카메라를 폰으로 되돌리지 않게 한다. 카메라는 BeginPlay 에서 직접 정한다.
	bAutoManageActiveCameraTarget = false;
}

void ASH_BattlePlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (BattleMapping)
		{
			Subsystem->AddMappingContext(BattleMapping, /*Priority=*/0);
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (const ASH_BattleGameMode* Mode = World->GetAuthGameMode<ASH_BattleGameMode>())
	{
		Board = Mode->GetBoard();
	}

	// 카메라 초기값이 보드 높이를 쓰므로 보드를 먼저 찾는다.
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(BattleCameraTag))
		{
			ViewCamera = *It;
			SetViewTarget(*It);
			InitializeCameraRig(**It);
			break;
		}
	}
	if (!ViewCamera.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SH_BattlePlayerController] 태그가 BattleCamera 인 CameraActor 가 레벨에 없다."));
	}

	if (BattleHUDClass)
	{
		if (UBattleHUDWidget* Widget = CreateWidget<UBattleHUDWidget>(this, BattleHUDClass))
		{
			Widget->AddToViewport();
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SH_BattlePlayerController] BattleHUDClass 가 비어 있다. 시작 버튼이 없다."));
	}
}

void ASH_BattlePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattlePlayerController] InputComponent 가 Enhanced Input 이 아니다."));
		return;
	}

	// &ThisClass::HandleSelect 는 "이 클래스의 HandleSelect" 라는 값이다.
	// this 와 함께 저장되었다가 클릭 때 this->HandleSelect() 로 불린다.
	if (SelectAction)
	{
		EnhancedInput->BindAction(SelectAction, ETriggerEvent::Started, this, &ThisClass::HandleSelect);
	}
	if (RemoveAction)
	{
		EnhancedInput->BindAction(RemoveAction, ETriggerEvent::Started, this, &ThisClass::HandleRemove);
	}
}

void ASH_BattlePlayerController::PlayerTick(const float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UpdateCameraInput(DeltaTime);

	if (ASH_BattleBoard* BoardPtr = Board.Get())
	{
		BoardPtr->SetHoveredCell(CellUnderCursor());
	}
}

TOptional<FIntPoint> ASH_BattlePlayerController::CellUnderCursor() const
{
	FHitResult Hit;
	if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), /*bTraceComplex=*/false, Hit))
	{
		return {};
	}
	const ASH_BattleBoard* BoardPtr = Board.Get();
	return BoardPtr ? BoardPtr->WorldToCell(Hit.ImpactPoint) : TOptional<FIntPoint>{};
}

void ASH_BattlePlayerController::HandleSelect()
{
	ASH_BattleBoard* BoardPtr = Board.Get();
	const TOptional<FIntPoint> Cell = CellUnderCursor();
	if (BoardPtr && Cell.IsSet())
	{
		BoardPtr->HandleCellClicked(Cell.GetValue());
	}
}

void ASH_BattlePlayerController::HandleRemove()
{
	ASH_BattleBoard* BoardPtr = Board.Get();
	const TOptional<FIntPoint> Cell = CellUnderCursor();
	if (BoardPtr && Cell.IsSet())
	{
		BoardPtr->HandleCellRightClicked(Cell.GetValue());
	}
}

//~ 시점 ----------------------------------------------------------------------

void ASH_BattlePlayerController::InitializeCameraRig(const ACameraActor& Camera)
{
	const FRotator Rotation = Camera.GetActorRotation();
	CameraYaw = static_cast<float>(Rotation.Yaw);
	CameraPitch = FMath::Clamp(static_cast<float>(Rotation.Pitch), CameraMinPitch, CameraMaxPitch);

	// 카메라가 바라보는 선이 보드 바닥과 만나는 점을 회전 중심으로 삼는다.
	const ASH_BattleBoard* BoardPtr = Board.Get();
	const double FloorZ = BoardPtr ? BoardPtr->GetActorLocation().Z : 0.0;
	const FVector Location = Camera.GetActorLocation();
	const FVector Forward = FRotator(CameraPitch, CameraYaw, 0.0f).Vector();

	// 피치를 음수로 제한했으므로 Forward.Z 는 항상 음수다.
	const double RayLength = FMath::Max(0.0, (FloorZ - Location.Z) / Forward.Z);
	CameraFocus = Location + Forward * RayLength;
	CameraFocus.Z = FloorZ;
	CameraDistance = FMath::Clamp(static_cast<float>(RayLength), CameraMinDistance, CameraMaxDistance);

	ApplyCameraRig();
}

void ASH_BattlePlayerController::UpdateCameraInput(const float GameDeltaTime)
{
	if (!ViewCamera.IsValid())
	{
		return;
	}

	// ★ PlayerTick 의 DeltaTime 은 전역 시간 배율이 곱해진 게임 시간이다. 배속 0.1 에서 그대로 쓰면
	//   카메라도 10배 느려진다. 배율로 나눠 실제 시간으로 되돌린다(관전 중에도 카메라는 평소 속도).
	const float DeltaTime = GameDeltaTime / FMath::Max(GetActorTimeDilation(), UE_KINDA_SMALL_NUMBER);

	// IsInputKeyDown 으로 직접 읽는다 — 입력 에셋이 필요 없고 배치용 매핑과 겹치지 않는다(ASH_SpectatorPawn 과 같은 방식).
	auto Axis = [this](const FKey& Positive, const FKey& Negative) -> float
	{
		return (IsInputKeyDown(Positive) ? 1.0f : 0.0f) - (IsInputKeyDown(Negative) ? 1.0f : 0.0f);
	};

	// --- 회전 ---------------------------------------------------------------
	CameraYaw += Axis(EKeys::E, EKeys::Q) * CameraRotateSpeed * DeltaTime;

	// ★ GetInputMouseDelta 를 쓰지 않는다. 커서가 보이는 입력 모드에서는 마우스를 잡고 있을 때만 값이 온다.
	//   커서의 화면 좌표 차이는 입력 모드와 상관없이 같다.
	FVector2D MousePosition = LastMousePosition;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (GetMousePosition(MouseX, MouseY))
	{
		MousePosition = FVector2D(MouseX, MouseY);
	}

	const bool bDragging = IsInputKeyDown(EKeys::MiddleMouseButton);
	if (bDragging && bDraggingCamera)   // 누른 첫 프레임은 건너뛴다 — 이전 좌표와의 차이가 튄다
	{
		const FVector2D MouseDelta = MousePosition - LastMousePosition;
		CameraYaw += static_cast<float>(MouseDelta.X) * CameraDragSensitivity;
		CameraPitch -= static_cast<float>(MouseDelta.Y) * CameraDragSensitivity;
	}
	bDraggingCamera = bDragging;
	LastMousePosition = MousePosition;
	CameraPitch = FMath::Clamp(CameraPitch, CameraMinPitch, CameraMaxPitch);

	// --- 확대 · 축소 ---------------------------------------------------------
	if (WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		CameraDistance *= 1.0f - CameraZoomStep;
	}
	if (WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		CameraDistance *= 1.0f + CameraZoomStep;
	}
	CameraDistance = FMath::Clamp(CameraDistance, CameraMinDistance, CameraMaxDistance);

	// --- 평행 이동 -----------------------------------------------------------
	// 피치를 빼고 요만 쓴다 — W 가 바닥을 파고들지 않고 화면 위쪽으로 간다.
	const FRotationMatrix YawMatrix(FRotator(0.0f, CameraYaw, 0.0f));
	const FVector Pan = YawMatrix.GetUnitAxis(EAxis::X) * Axis(EKeys::W, EKeys::S)
		+ YawMatrix.GetUnitAxis(EAxis::Y) * Axis(EKeys::D, EKeys::A);

	const bool bFast = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	const float Speed = CameraPanSpeed * (bFast ? CameraFastMultiplier : 1.0f) * (CameraDistance / 3000.0f);
	CameraFocus += Pan.GetClampedToMaxSize(1.0) * Speed * DeltaTime;

	const ASH_BattleBoard* BoardPtr = Board.Get();
	const FVector Center = BoardPtr ? BoardPtr->GetActorLocation() : FVector::ZeroVector;
	const FVector FromCenter = (CameraFocus - Center).GetClampedToMaxSize(CameraMaxFocusRadius);
	CameraFocus = Center + FVector(FromCenter.X, FromCenter.Y, 0.0);

	ApplyCameraRig();
}

void ASH_BattlePlayerController::ApplyCameraRig() const
{
	if (ACameraActor* Camera = ViewCamera.Get())
	{
		const FRotator Rotation(CameraPitch, CameraYaw, 0.0f);
		Camera->SetActorLocationAndRotation(CameraFocus - Rotation.Vector() * CameraDistance, Rotation);
	}
}
