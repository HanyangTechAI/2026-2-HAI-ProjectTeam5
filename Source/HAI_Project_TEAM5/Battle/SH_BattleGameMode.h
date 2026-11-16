// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/SH_BattleTypes.h"
#include "GameFramework/GameModeBase.h"
#include "RL/SH_Arena.h"
#include "SH_BattleGameMode.generated.h"

class ASH_BattleBoard;
class ASH_BattleBrain;
class ASH_BTBotController;
class UBehaviorTree;

DECLARE_MULTICAST_DELEGATE_OneParam(FSHOnPhaseChanged, ESHBattlePhase);
DECLARE_MULTICAST_DELEGATE_OneParam(FSHOnStartFailed, const FString& /*Reason*/);

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

	/**
	 *  게임 배속. [MinGameSpeed, MaxGameSpeed] 로 잘라 전역 시간 배율로 건다.
	 *  유닛 이동 · 애니메이션 · 정책 결정(20 Hz 는 게임 시간 기준) · 총알 그림이 모두 같이 느려진다.
	 *  ★ 카메라는 실제 시간으로 움직인다(ASH_BattlePlayerController::UpdateCameraInput).
	 */
	void SetGameSpeed(float Speed);
	[[nodiscard]] float GetGameSpeed() const { return GameSpeed; }

	static constexpr float MinGameSpeed = 0.1f;
	static constexpr float MaxGameSpeed = 1.0f;

	/**
	 *  팀(0 = 아군, 1 = 적)을 무엇이 조종할지. 배치 단계에서만 바뀐다.
	 *  행동 트리를 고르려면 CombatBehaviorTree 가 지정돼 있어야 한다 — 아니면 false 를 돌려주고 바꾸지 않는다.
	 */
	bool SetTeamControl(int32 Team, ESHTeamControl Control);
	[[nodiscard]] ESHTeamControl GetTeamControl(int32 Team) const { return TeamControl[Team == 0 ? 0 : 1]; }
	[[nodiscard]] bool HasBehaviorTree() const { return CombatBehaviorTree != nullptr; }

	/**
	 *  RL 팀이 쓰는 정책 스냅샷 폴더(프로젝트 폴더 기준 상대 경로, 예: Training/START_11_13/cum017h06m).
	 *  고르지 않았으면 보드 유닛 정의(DA_Unit_Shooter)의 기본값. 배치 단계에서만 바뀐다.
	 *  ★ 양 팀이 RL 이고 폴더가 다르면 팀마다 정책 실행기(ASH_BattleBrain)를 따로 띄운다 — 모델끼리 비교할 수 있다.
	 */
	[[nodiscard]] FString GetTeamPolicy(int32 Team) const;
	bool SetTeamPolicy(int32 Team, const FString& Directory);

	/** 시작 버튼을 눌렀는데 전투를 시작하지 못했을 때. HUD 가 이유를 보여 준다. */
	FSHOnStartFailed OnStartFailed;

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
	void DestroyBrains();
	void FailStart(const FString& Reason);

	UPROPERTY(EditDefaultsOnly, Category = "SH|Battle")
	TSubclassOf<ASH_BattleBrain> BrainClass;

	/** 이번 전투의 정책 실행기. RL 팀이 없으면 0개, 양 팀이 같은 모델이면 1개, 다른 모델이면 2개. */
	UPROPERTY()
	TArray<TObjectPtr<ASH_BattleBrain>> Brains;

	/** 비어 있으면 유닛 정의의 기본값. */
	FString TeamPolicy[2];

	/** "행동 트리" 팀이 쓰는 트리(BT_SHCombat). BP_BattleGameMode 에서 지정한다(Docs/BehaviorTreeGuide.md). */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Battle|AI")
	TObjectPtr<UBehaviorTree> CombatBehaviorTree;

	/** "행동 트리" 팀 유닛에 끼울 컨트롤러. */
	UPROPERTY(EditDefaultsOnly, Category = "SH|Battle|AI")
	TSubclassOf<ASH_BTBotController> BTControllerClass;

	ESHTeamControl TeamControl[2] = { ESHTeamControl::RLModel, ESHTeamControl::RLModel };

	mutable TWeakObjectPtr<ASH_BattleBoard> CachedBoard;

	float GameSpeed = 1.0f;

	ESHBattlePhase Phase = ESHBattlePhase::Placement;
	ESHRoundEnd LastResult = ESHRoundEnd::None;
};
