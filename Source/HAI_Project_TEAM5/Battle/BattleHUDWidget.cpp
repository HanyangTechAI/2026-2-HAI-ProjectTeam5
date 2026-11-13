// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/BattleHUDWidget.h"

#include "Battle/SH_BattleBoard.h"
#include "Battle/SH_BattleGameMode.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"

void UBattleHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	StartButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleStartClicked);
	ReplayButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReplayClicked);

	// 범위를 WBP 값이 아니라 게임모드의 상수에서 가져온다 — 규칙이 한 곳에만 있게.
	SpeedSlider->SetMinValue(ASH_BattleGameMode::MinGameSpeed);
	SpeedSlider->SetMaxValue(ASH_BattleGameMode::MaxGameSpeed);
	SpeedSlider->SetStepSize(0.05f);
	SpeedSlider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleSpeedChanged);
	VisionRaysCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleVisionRaysChanged);
	AimRaysCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleAimRaysChanged);

	UWorld* World = GetWorld();
	ASH_BattleGameMode* Mode = World ? World->GetAuthGameMode<ASH_BattleGameMode>() : nullptr;
	if (!Mode)
	{
		UE_LOG(LogTemp, Error, TEXT("[BattleHUDWidget] 게임모드가 ASH_BattleGameMode 가 아니다."));
		return;
	}

	GameMode = Mode;
	PhaseHandle = Mode->OnPhaseChanged.AddUObject(this, &ThisClass::HandlePhaseChanged);

	if (ASH_BattleBoard* BoardPtr = Mode->GetBoard())
	{
		Board = BoardPtr;
		PlacementHandle = BoardPtr->OnPlacementChanged.AddUObject(this, &ThisClass::HandlePlacementChanged);
		HandlePlacementChanged(BoardPtr->GetPlacedCount(), BoardPtr->GetRequiredCount());
		VisionRaysCheckBox->SetIsChecked(BoardPtr->AreVisionRaysVisible());
		AimRaysCheckBox->SetIsChecked(BoardPtr->AreAimRaysVisible());
	}

	// 현재 상태로 한 번 그려 둔다. SetValue 는 OnValueChanged 를 부르지 않는다.
	SpeedSlider->SetValue(Mode->GetGameSpeed());
	RefreshSpeedText(Mode->GetGameSpeed());
	HandlePhaseChanged(Mode->GetPhase());
}

void UBattleHUDWidget::HandleSpeedChanged(const float Value)
{
	if (ASH_BattleGameMode* Mode = GameMode.Get())
	{
		Mode->SetGameSpeed(Value);
		RefreshSpeedText(Mode->GetGameSpeed());   // 잘린 값을 보여 준다
	}
}

void UBattleHUDWidget::HandleVisionRaysChanged(const bool bIsChecked)
{
	if (ASH_BattleBoard* BoardPtr = Board.Get())
	{
		BoardPtr->SetVisionRaysVisible(bIsChecked);
	}
}

void UBattleHUDWidget::HandleAimRaysChanged(const bool bIsChecked)
{
	if (ASH_BattleBoard* BoardPtr = Board.Get())
	{
		BoardPtr->SetAimRaysVisible(bIsChecked);
	}
}

void UBattleHUDWidget::RefreshSpeedText(const float Speed)
{
	SpeedText->SetText(FText::FromString(FString::Printf(TEXT("배속 x%.2f"), Speed)));
}

void UBattleHUDWidget::NativeDestruct()
{
	if (ASH_BattleGameMode* Mode = GameMode.Get())
	{
		Mode->OnPhaseChanged.Remove(PhaseHandle);
	}
	if (ASH_BattleBoard* BoardPtr = Board.Get())
	{
		BoardPtr->OnPlacementChanged.Remove(PlacementHandle);
	}
	Super::NativeDestruct();
}

void UBattleHUDWidget::HandleStartClicked()
{
	if (ASH_BattleGameMode* Mode = GameMode.Get())
	{
		Mode->RequestStartBattle();
	}
}

void UBattleHUDWidget::HandleReplayClicked()
{
	if (ASH_BattleGameMode* Mode = GameMode.Get())
	{
		Mode->RequestReturnToPlacement();
	}
}

void UBattleHUDWidget::HandlePlacementChanged(const int32 Placed, const int32 Required)
{
	PlacedText->SetText(FText::FromString(FString::Printf(TEXT("배치 %d / %d"), Placed, Required)));
	StartButton->SetIsEnabled(Placed == Required);
}

void UBattleHUDWidget::HandlePhaseChanged(const ESHBattlePhase NewPhase)
{
	const bool bPlacement = NewPhase == ESHBattlePhase::Placement;
	const bool bResult = NewPhase == ESHBattlePhase::Result;

	const ESlateVisibility WhenPlacement = bPlacement ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const ESlateVisibility WhenResult = bResult ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

	StartButton->SetVisibility(WhenPlacement);
	PlacedText->SetVisibility(WhenPlacement);
	ReplayButton->SetVisibility(WhenResult);
	ResultText->SetVisibility(WhenResult);

	const ASH_BattleGameMode* Mode = GameMode.Get();
	if (!bResult || !Mode)
	{
		return;
	}

	// 시간 초과 · 동시 전멸은 양쪽 패배다(학습과 같은 규칙).
	const TCHAR* Message = TEXT("양쪽 패배");
	switch (Mode->GetLastResult())
	{
	case ESHRoundEnd::Team0Wins: Message = TEXT("승리"); break;
	case ESHRoundEnd::Team1Wins: Message = TEXT("패배"); break;
	case ESHRoundEnd::Timeout:   Message = TEXT("시간 초과 — 양쪽 패배"); break;
	default: break;
	}
	ResultText->SetText(FText::FromString(Message));
}
