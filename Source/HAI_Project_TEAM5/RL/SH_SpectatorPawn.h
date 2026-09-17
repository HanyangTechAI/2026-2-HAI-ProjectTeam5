// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "SH_SpectatorPawn.generated.h"

/**
 *  학습 장면을 날아다니며 관전하는 폰. 전투에 일절 참여하지 않는다.
 *
 *  조작은 에디터 뷰포트와 같게 맞췄다:
 *      우클릭 유지 + 마우스   시점 회전
 *      W A S D               전후좌우 (시선 기준)
 *      E / Q                 상승 / 하강
 *      Shift                 가속
 *
 *  ★ Enhanced Input 을 쓰지 않는다.
 *  이 프로젝트의 DefaultInput.ini 에는 레거시 AxisMappings 가 없어서 ADefaultPawn 의
 *  기본 바인딩이 동작하지 않고, 그렇다고 Enhanced Input 을 쓰려면 IA/IMC 에셋을 새로
 *  만들어야 하며 BP_PlayerController 가 추가하는 IMC_Character 와 키가 겹칠 수 있다.
 *  APlayerController::IsInputKeyDown 으로 직접 폴링하면 에셋이 0개이고 충돌도 없다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_SpectatorPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	ASH_SpectatorPawn();

	/** 기본 이동 속도(cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Spectator", meta = (ClampMin = "10.0"))
	float NormalSpeed = 2000.0f;

	/** Shift 를 누르고 있을 때의 속도(cm/s). 아레나 격자가 185m 라 빠르게 이동할 필요가 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Spectator", meta = (ClampMin = "10.0"))
	float FastSpeed = 8000.0f;

	/** 마우스 감도(도/픽셀). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Spectator", meta = (ClampMin = "0.01"))
	float LookSensitivity = 1.0f;

	/** 우클릭을 누르고 있을 때만 시점을 돌린다. false 면 항상 마우스로 돌아간다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Spectator")
	bool bRequireRightMouseToLook = true;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
};
