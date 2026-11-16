// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BattleGameMode.h"

#include "Battle/AI/SH_BTBotController.h"
#include "Battle/SH_BattleBoard.h"
#include "Battle/SH_BattleBrain.h"
#include "Battle/SH_PolicyCatalog.h"
#include "Battle/SH_BattlePlayerController.h"
#include "Battle/SH_UnitDefinition.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

ASH_BattleGameMode::ASH_BattleGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ASH_BattlePlayerController::StaticClass();
	BrainClass = ASH_BattleBrain::StaticClass();
	BTControllerClass = ASH_BTBotController::StaticClass();
}

FString ASH_BattleGameMode::GetTeamPolicy(const int32 Team) const
{
	const FString& Chosen = TeamPolicy[Team == 0 ? 0 : 1];
	if (!Chosen.IsEmpty())
	{
		return Chosen;
	}
	const ASH_BattleBoard* BoardPtr = GetBoard();
	const USH_UnitDefinition* Definition = BoardPtr ? BoardPtr->GetAllyDefinition() : nullptr;
	return Definition ? Definition->GetPolicyDirectory() : FString();
}

bool ASH_BattleGameMode::SetTeamPolicy(const int32 Team, const FString& Directory)
{
	if (Phase != ESHBattlePhase::Placement || (Team != 0 && Team != 1))
	{
		return false;
	}
	TeamPolicy[Team] = Directory;
	return true;
}

void ASH_BattleGameMode::FailStart(const FString& Reason)
{
	UE_LOG(LogTemp, Error, TEXT("[SH_BattleGameMode] 전투를 시작하지 못했다 — %s"), *Reason);
	OnStartFailed.Broadcast(Reason);
}

bool ASH_BattleGameMode::SetTeamControl(const int32 Team, const ESHTeamControl Control)
{
	if (Phase != ESHBattlePhase::Placement || (Team != 0 && Team != 1))
	{
		return false;
	}
	if (Control == ESHTeamControl::BehaviorTree && !CombatBehaviorTree)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SH_BattleGameMode] 행동 트리가 지정되지 않았다 — BP_BattleGameMode 의 CombatBehaviorTree 에 BT_SHCombat 을 넣을 것."));
		return false;
	}
	TeamControl[Team] = Control;
	return true;
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

	const bool bUsesTree[2] = { TeamControl[0] == ESHTeamControl::BehaviorTree, TeamControl[1] == ESHTeamControl::BehaviorTree };
	if ((bUsesTree[0] || bUsesTree[1]) && !CombatBehaviorTree)
	{
		FailStart(TEXT("행동 트리가 지정되지 않았다(BP_BattleGameMode → Combat Behavior Tree)."));
		return;
	}

	// 1. 팀마다 컨트롤러를 맞춘다(행동 트리 ↔ RL). 지난 전투와 같으면 아무것도 하지 않는다.
	for (int32 Team = 0; Team < 2; ++Team)
	{
		BoardPtr->ApplyTeamControl(Team, bUsesTree[Team], BTControllerClass);
	}

	// 2. RL 팀 유닛만 정책에 등록한다. 행동 트리 팀은 정책이 건드리지 않는다
	//    (학습 평가의 "RL 대 스크립트 봇" 과 같은 구성 — 정책은 상대가 누구든 같은 관측을 받는다).
	//    같은 모델을 쓰는 팀끼리 묶는다 — 모델이 같으면 실행기 하나(학습 때처럼 한 매니저에서 함께 결정),
	//    다르면 실행기 둘(각자 자기 정책).
	TArray<TPair<FString, TArray<ASH_RLCharacter*>>> UnitsByPolicy;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (bUsesTree[Team])
		{
			continue;
		}
		const FString Directory = GetTeamPolicy(Team);
		if (Directory.IsEmpty())
		{
			FailStart(FString::Printf(TEXT("%s 팀의 RL 모델이 정해지지 않았다."), Team == 0 ? TEXT("아군") : TEXT("적군")));
			return;
		}
		auto* Group = UnitsByPolicy.FindByPredicate([&Directory](const auto& Pair) { return Pair.Key == Directory; });
		if (!Group)
		{
			Group = &UnitsByPolicy.Emplace_GetRef(Directory, TArray<ASH_RLCharacter*>());
		}
		Group->Value.Append(BoardPtr->GetTeamUnits(Team));
	}
	for (const TPair<FString, TArray<ASH_RLCharacter*>>& Group : UnitsByPolicy)
	{
		ASH_BattleBrain* NewBrain = World->SpawnActor<ASH_BattleBrain>(BrainClass);
		FString Error = TEXT("정책 실행기를 만들지 못했다.");
		if (!NewBrain || !NewBrain->Assemble(Group.Value, Group.Key, Error))
		{
			if (NewBrain)
			{
				NewBrain->Destroy();
			}
			DestroyBrains();
			FailStart(FString::Printf(TEXT("%s — %s"), *SHPolicyCatalog::MakeLabel(Group.Key), *Error));
			return;
		}
		Brains.Add(NewBrain);
	}

	if (!BoardPtr->StartBattle())
	{
		DestroyBrains();
		FailStart(TEXT("보드가 전투를 시작하지 못했다(배치가 끝나지 않았다)."));
		return;
	}

	// ★ StartBattle 이 먼저다: 리셋과 배치가 끝난 상태로 첫 관측을 모으고, 트리도 그 상태에서 시작한다.
	for (ASH_BattleBrain* Brain : Brains)
	{
		Brain->StartThinking();
	}
	if ((bUsesTree[0] || bUsesTree[1]) && !BoardPtr->StartBehaviorTrees(CombatBehaviorTree))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleGameMode] 일부 유닛의 행동 트리를 시작하지 못했다 — 위 로그를 볼 것."));
	}
	auto Describe = [this, &bUsesTree](const int32 Team)
	{
		return bUsesTree[Team] ? FString(ToDisplayText(TeamControl[Team]))
		                       : FString::Printf(TEXT("RL(%s)"), *SHPolicyCatalog::MakeLabel(GetTeamPolicy(Team)));
	};
	UE_LOG(LogTemp, Display, TEXT("[SH_BattleGameMode] 전투 시작 — 아군 %s vs 적군 %s (정책 실행기 %d개)"),
		*Describe(0), *Describe(1), Brains.Num());
	SetPhase(ESHBattlePhase::Battle);
}

void ASH_BattleGameMode::HandleBattleEnded(const ESHRoundEnd Result)
{
	for (ASH_BattleBrain* Brain : Brains)
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

	DestroyBrains();
	BoardPtr->ReturnToPlacement();
	SetPhase(ESHBattlePhase::Placement);
}

void ASH_BattleGameMode::DestroyBrains()
{
	for (ASH_BattleBrain* Brain : Brains)
	{
		if (Brain)
		{
			Brain->Destroy();
		}
	}
	Brains.Reset();
}
