// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/SH_BattleTypes.h"
#include "GameFramework/GameModeBase.h"
#include "RL/SH_Arena.h"
#include "SH_BattleGameMode.generated.h"

class ASH_BattleBoard;
class ASH_BattleBrain;

DECLARE_MULTICAST_DELEGATE_OneParam(FSHOnPhaseChanged, ESHBattlePhase);

/**
 *  게임 흐름. 단계 전이는 SetPhase 한 곳에서만 일어나고, 바뀔 때마다 UI 에 알린다.
 *
 *  ★ 폰을 쓰지 않는다. 입력은 ASH_BattlePlayerController 가 직접 받는다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_BattleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASH_BattleGameMode();

	/** UI 의 시작 버튼. 조건이 안 되면 아무 일도 하지 않는다. */
	void RequestStartBattle();

	/** UI 의 다시 배치 버튼. */
	void RequestReturnToPlacement();

	[[nodiscard]] ESHBattlePhase GetPhase() const { return Phase; }
	[[nodiscard]] ESHRoundEnd GetLastResult() const { return LastResult; }

	/**
	 *  레벨의 보드. 처음 물을 때 찾아서 기억한다.
	 *  ★ BeginPlay 에서 찾지 않는 이유: 컨트롤러와 HUD 의 BeginPlay 가 이 게임모드보다 먼저 돌 수 있다.
	 *  캐시를 채워도 게임모드의 논리적 상태는 그대로이므로 const 함수이고 캐시는 mutable 이다.
	 */
	[[nodiscard]] ASH_BattleBoard* GetBoard() const;

	FSHOnPhaseChanged OnPhaseChanged;

	/** 폰이 없는 게임모드다. 기본 구현은 스폰 실패로 컨트롤러를 비활성 상태로 보낸다. */
	virtual void RestartPlayer(AController* NewPlayer) override {}

protected:
	virtual void BeginPlay() override;

private:
	void SetPhase(ESHBattlePhase NewPhase);
	void HandleBattleEnded(ESHRoundEnd Result);
	void DestroyBrain();

	UPROPERTY(EditDefaultsOnly, Category = "SH|Battle")
	TSubclassOf<ASH_BattleBrain> BrainClass;

	UPROPERTY()
	TObjectPtr<ASH_BattleBrain> Brain;

	mutable TWeakObjectPtr<ASH_BattleBoard> CachedBoard;

	ESHBattlePhase Phase = ESHBattlePhase::Placement;
	ESHRoundEnd LastResult = ESHRoundEnd::None;
};
