// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_SpectatorGameMode.h"

#include "GameFramework/PlayerController.h"
#include "SH_SpectatorPawn.h"

ASH_SpectatorGameMode::ASH_SpectatorGameMode()
{
	DefaultPawnClass = ASH_SpectatorPawn::StaticClass();

	// ★ BP_PlayerController 가 아니라 기본 PlayerController 를 쓴다.
	//   BP_PlayerController 는 IMC_Character 를 추가하는데, 관전자는 Enhanced Input 을
	//   쓰지 않으므로 불필요하고, IA_Shoot / IA_Focus 가 갈 곳 없이 떠 있게 된다.
	PlayerControllerClass = APlayerController::StaticClass();

	HUDClass = nullptr;
	bStartPlayersAsSpectators = false;
}

APawn* ASH_SpectatorGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	// StartSpot 은 의도적으로 무시한다. 학습 맵에 PlayerStart 가 없으면 엔진은
	// 원점에 스폰하는데, 원점은 아레나 0 의 한가운데다.
	const FTransform SpawnTransform(SpectatorStartRotation, SpectatorStartLocation, FVector::OneVector);

	APawn* Pawn = SpawnDefaultPawnAtTransform(NewPlayer, SpawnTransform);

	// 폰 회전만 맞춰서는 카메라가 따라오지 않는다 — 컨트롤 회전이 시점을 정한다.
	if (NewPlayer)
	{
		NewPlayer->SetControlRotation(SpectatorStartRotation);
	}

	return Pawn;
}
