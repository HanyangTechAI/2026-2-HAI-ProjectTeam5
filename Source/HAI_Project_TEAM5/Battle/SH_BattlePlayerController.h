// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SH_BattlePlayerController.generated.h"

class ACameraActor;
class ASH_BattleBoard;
class UBattleHUDWidget;
class UInputAction;
class UInputMappingContext;

/**
 *  마우스 커서 → 칸 → 보드. 폰 없이 컨트롤러가 직접 입력을 받는다.
 *  HUD 위젯도 여기서 만든다.
 *
 *  시점: 레벨의 "BattleCamera" 태그 CameraActor 를 시작 위치로 삼아 컨트롤러가 직접 움직인다.
 *    W A S D      화면 기준 평행 이동 (Shift 가속)
 *    Q / E        좌우 회전
 *    가운데 버튼   누른 채 끌어서 회전
 *    휠           확대 · 축소
 *
 *  ★ ASH_SpectatorPawn 을 쓰지 않는 이유: 그 폰은 커서를 숨기고 우클릭으로 시점을 돌린다.
 *  이 게임은 커서로 칸을 고르고 우클릭으로 유닛을 뺀다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASH_BattlePlayerController();

	virtual void PlayerTick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void HandleSelect();
	void HandleRemove();
	[[nodiscard]] TOptional<FIntPoint> CellUnderCursor() const;

	/** 카메라의 지금 자세에서 바라보는 바닥 점 · 거리 · 각도를 읽어 온다. */
	void InitializeCameraRig(const ACameraActor& Camera);
	/** GameDeltaTime 은 시간 배율이 곱해진 값이다. 안에서 실제 시간으로 되돌려 쓴다. */
	void UpdateCameraInput(float GameDeltaTime);
	void ApplyCameraRig() const;

	UPROPERTY(EditDefaultsOnly, Category = "SH|Input")
	TObjectPtr<UInputMappingContext> BattleMapping;

	UPROPERTY(EditDefaultsOnly, Category = "SH|Input")
	TObjectPtr<UInputAction> SelectAction;

	UPROPERTY(EditDefaultsOnly, Category = "SH|Input")
	TObjectPtr<UInputAction> RemoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "SH|UI")
	TSubclassOf<UBattleHUDWidget> BattleHUDClass;

	/** 평행 이동 속도(cm/s). 기본 거리 3,000 cm 기준이고, 멀어질수록 비례해서 빨라진다. */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "1.0"))
	float CameraPanSpeed = 2000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "1.0"))
	float CameraFastMultiplier = 2.5f;

	/** Q / E 회전 속도(도/초). */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "1.0"))
	float CameraRotateSpeed = 90.0f;

	/** 가운데 버튼으로 끌 때 회전 감도(도/픽셀). */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "0.01"))
	float CameraDragSensitivity = 0.25f;

	/** 휠 한 칸에 거리가 바뀌는 비율. */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float CameraZoomStep = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "100.0"))
	float CameraMinDistance = 800.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "100.0"))
	float CameraMaxDistance = 8000.0f;

	/** 내려다보는 각도 범위(도). 음수가 아래쪽이다. 0 에 가까우면 바닥이 보이지 않아 칸을 고를 수 없다. */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera")
	float CameraMinPitch = -85.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera")
	float CameraMaxPitch = -20.0f;

	/** 바라보는 점이 보드 중심에서 벗어날 수 있는 반경(cm). */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Camera", meta = (ClampMin = "0.0"))
	float CameraMaxFocusRadius = 2500.0f;

	TWeakObjectPtr<ACameraActor> ViewCamera;
	FVector CameraFocus = FVector::ZeroVector;
	float CameraYaw = 0.0f;
	float CameraPitch = -48.0f;
	float CameraDistance = 3000.0f;
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	bool bDraggingCamera = false;

	/** 게임모드가 찾아 준다. 레벨의 액터라 이 컨트롤러가 소유하지 않는다. */
	TWeakObjectPtr<ASH_BattleBoard> Board;
};
