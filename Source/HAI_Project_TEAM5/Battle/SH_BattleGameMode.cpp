// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BattleGameMode.h"

#include "Battle/SH_BattleBoard.h"
#include "Battle/SH_BattleBrain.h"
#include "Battle/SH_BattlePlayerController.h"
#include "Battle/SH_UnitDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

ASH_BattleGameMode::ASH_BattleGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ASH_BattlePlayerController::StaticClass();
	BrainClass = ASH_BattleBrain::StaticClass();
}

ASH_BattleBoard* ASH_BattleGameMode::GetBoard() const
{
	if (!CachedBoard.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			// 켜진 보드(bActiveBoard)를 쓴다. 여러 개가 켜져 있으면 처음 것을 쓰고 알린다.
			int32 ActiveCount = 0;
			for (TActorIterator<ASH_BattleBoard> It(World); It; ++It)
			{
				if (!It->IsActiveBoard())
				{
					continue;
				}
				if (++ActiveCount == 1)
				{
					CachedBoard = *It;
				}
			}
			if (ActiveCount > 1)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[SH_BattleGameMode] 켜진 보드가 %d 개다. '%s' 를 쓴다. 나머지는 bActiveBoard 를 끌 것."),
					ActiveCount, *CachedBoard->GetName());
			}
		}
	}
	return CachedBoard.Get();
}

void ASH_BattleGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (ASH_BattleBoard* BoardPtr = GetBoard())
	{
		BoardPtr->OnBattleEnded.AddUObject(this, &ThisClass::HandleBattleEnded);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleGameMode] 레벨에 켜진(bActiveBoard) ASH_BattleBoard 가 없다."));
	}
}

void ASH_BattleGameMode::SetGameSpeed(const float Speed)
{
	GameSpeed = FMath::Clamp(Speed, MinGameSpeed, MaxGameSpeed);
	UGameplayStatics::SetGlobalTimeDilation(this, GameSpeed);
}

void ASH_BattleGameMode::SetPhase(const ESHBattlePhase NewPhase)
{
	// 전이 규칙을 어기는 호출은 프로그래밍 실수다 — 개발 빌드에서 바로 멈춰 알린다.
	check(IsAllowedTransition(Phase, NewPhase));
	Phase = NewPhase;
	OnPhaseChanged.Broadcast(Phase);
}

void ASH_BattleGameMode::RequestStartBattle()
{
	ASH_BattleBoard* BoardPtr = GetBoard();
	UWorld* World = GetWorld();
	if (Phase != ESHBattlePhase::Placement || !BoardPtr || !World || !BoardPtr->IsPlacementComplete())
	{
		return;
	}

	const USH_UnitDefinition* Definition = BoardPtr->GetAllyDefinition();
	if (!Definition)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleGameMode] 보드의 AllyDefinition 이 비어 있다."));
		return;
	}

	Brain = World->SpawnActor<ASH_BattleBrain>(BrainClass);
	if (!Brain || !Brain->Assemble(BoardPtr->GetAllUnits(), Definition->GetPolicyDirectory()))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleGameMode] 정책을 준비하지 못했다 — 전투를 시작하지 않는다."));
		DestroyBrain();
		return;
	}

	if (!BoardPtr->StartBattle())
	{
		DestroyBrain();
		return;
	}

	// ★ StartBattle 이 먼저다: 리셋과 배치가 끝난 상태로 첫 관측을 모은다.
	Brain->StartThinking();
	SetPhase(ESHBattlePhase::Battle);
}

void ASH_BattleGameMode::HandleBattleEnded(const ESHRoundEnd Result)
{
	if (Brain)
	{
		Brain->StopThinking();
	}
	LastResult = Result;
	SetPhase(ESHBattlePhase::Result);
}

void ASH_BattleGameMode::RequestReturnToPlacement()
{
	ASH_BattleBoard* BoardPtr = GetBoard();
	if (Phase != ESHBattlePhase::Result || !BoardPtr)
	{
		return;
	}

	DestroyBrain();
	BoardPtr->ReturnToPlacement();
	SetPhase(ESHBattlePhase::Placement);
}

void ASH_BattleGameMode::DestroyBrain()
{
	if (Brain)
	{
		Brain->Destroy();
		Brain = nullptr;
	}
}
