// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SH_SpectatorGameMode.generated.h"

/**
 *  학습 맵 전용 게임모드. 관전자만 스폰하고 전투 유닛은 만들지 않는다.
 *
 *  ★ 왜 필요한가.
 *  GlobalDefaultGameMode 가 BP_GameMode 이고 그 DefaultPawnClass 가 BP_BaseCharacter 라,
 *  학습 맵에서 Play 를 누르면 아레나 소속이 아닌 전투 유닛이 하나 더 스폰된다.
 *  그 유닛은 어느 아레나에도 등록되지 않은 채 총알 판정 대상이 되고, 팀 0 이므로
 *  팀 1 유닛에게 맞으면서 학습 통계를 오염시킨다.
 *
 *  ★ 왜 프로젝트 기본값을 바꾸지 않는가.
 *  GlobalDefaultGameMode 는 출고 빌드와 공유하는 설정이다. 이 게임모드는
 *  World Settings -> GameMode Override 로 학습 맵에만 건다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_SpectatorGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASH_SpectatorGameMode();

	/** 관전 시작 위치. 기본값은 4x4 격자(0,0 ~ 15000,15000) 전체가 보이는 바깥 모서리 위. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Spectator")
	FVector SpectatorStartLocation = FVector(-6000.0f, -6000.0f, 8000.0f);

	/** 격자 중심을 내려다보는 각도. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Spectator")
	FRotator SpectatorStartRotation = FRotator(-23.0f, 45.0f, 0.0f);

protected:
	/** PlayerStart 를 무시하고 위 트랜스폼에서 스폰한다.
	 *  학습 맵에는 PlayerStart 를 두지 않으므로(두면 전투 유닛이 거기 스폰될 위험이 있다)
	 *  기본 구현은 원점 — 즉 아레나 0 한가운데 — 에 떨어뜨린다. */
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;
};
