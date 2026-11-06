// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/SH_BattleTypes.h"
#include "Blueprint/UserWidget.h"
#include "BattleHUDWidget.generated.h"

class ASH_BattleBoard;
class ASH_BattleGameMode;
class UButton;
class UTextBlock;

/**
 *  전투 HUD. 레이아웃은 위젯 블루프린트(WBP_BattleHUD)에 있고 동작은 여기에 있다.
 *
 *  ★ BindWidget 멤버의 이름은 WBP 안 위젯 이름과 같아야 한다. 다르면 WBP 컴파일 에러로 알려 준다.
 */
UCLASS(Abstract)
class HAI_PROJECT_TEAM5_API UBattleHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// 버튼의 OnClicked 는 동적 델리게이트다 — UFUNCTION 이어야 이름으로 찾을 수 있다.
	UFUNCTION()
	void HandleStartClicked();

	UFUNCTION()
	void HandleReplayClicked();

	void HandlePhaseChanged(ESHBattlePhase NewPhase);
	void HandlePlacementChanged(int32 Placed, int32 Required);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ReplayButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PlacedText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ResultText;

	TWeakObjectPtr<ASH_BattleGameMode> GameMode;
	TWeakObjectPtr<ASH_BattleBoard> Board;
	FDelegateHandle PhaseHandle;
	FDelegateHandle PlacementHandle;
};
