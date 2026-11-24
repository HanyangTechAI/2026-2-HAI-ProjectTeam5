// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/BattleHUDWidget.h"

#include "Battle/SH_BattleBoard.h"
#include "Battle/SH_BattleGameMode.h"
#include "Battle/SH_PolicyCatalog.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
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
		BindBoard(BoardPtr);
		VisionRaysCheckBox->SetIsChecked(BoardPtr->AreVisionRaysVisible());
		AimRaysCheckBox->SetIsChecked(BoardPtr->AreAimRaysVisible());
	}
	BoardChangedHandle = Mode->OnBoardChanged.AddUObject(this, &ThisClass::HandleBoardChanged);
	RefreshMapCombo();
	MapCombo->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleMapChanged);

	SetupControlCombo(*AllyControlCombo, 0);
	SetupControlCombo(*EnemyControlCombo, 1);
	AllyControlCombo->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleAllyControlChanged);
	EnemyControlCombo->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleEnemyControlChanged);

	RefreshModelCombos();
	AllyModelCombo->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleAllyModelChanged);
	EnemyModelCombo->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleEnemyModelChanged);
	StartFailedHandle = Mode->OnStartFailed.AddUObject(this, &ThisClass::HandleStartFailed);

	// 현재 상태로 한 번 그려 둔다. SetValue 는 OnValueChanged 를 부르지 않는다.
	SpeedSlider->SetValue(Mode->GetGameSpeed());
	RefreshSpeedText(Mode->GetGameSpeed());
	HandlePhaseChanged(Mode->GetPhase());
}

void UBattleHUDWidget::BindBoard(ASH_BattleBoard* NewBoard)
{
	if (ASH_BattleBoard* OldBoard = Board.Get())
	{
		OldBoard->OnPlacementChanged.Remove(PlacementHandle);
	}
	Board = NewBoard;
	if (NewBoard)
	{
		PlacementHandle = NewBoard->OnPlacementChanged.AddUObject(this, &ThisClass::HandlePlacementChanged);
		HandlePlacementChanged(NewBoard->GetPlacedCount(), NewBoard->GetRequiredCount());
	}
}

void UBattleHUDWidget::HandleBoardChanged(ASH_BattleBoard* NewBoard)
{
	BindBoard(NewBoard);
	if (NewBoard)
	{
		// 체크박스에 보이는 상태를 새 보드에도 건다(보드마다 따로 기억하지 않는다).
		NewBoard->SetVisionRaysVisible(VisionRaysCheckBox->IsChecked());
		NewBoard->SetAimRaysVisible(AimRaysCheckBox->IsChecked());
	}
	RefreshMapCombo();
}

void UBattleHUDWidget::RefreshMapCombo()
{
	const ASH_BattleGameMode* Mode = GameMode.Get();
	if (!Mode)
	{
		return;
	}
	TGuardValue<bool> Guard(bFillingCombos, true);
	MapCombo->ClearOptions();
	MapBoards.Reset();
	const ASH_BattleBoard* Current = Mode->GetBoard();
	for (ASH_BattleBoard* Each : Mode->GetBoards())
	{
		const FString Label = Each->GetDisplayName().ToString();
		MapCombo->AddOption(Label);
		MapBoards.Add(Each);
		if (Each == Current)
		{
			MapCombo->SetSelectedOption(Label);
		}
	}
}

void UBattleHUDWidget::HandleMapChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	ASH_BattleGameMode* Mode = GameMode.Get();
	const int32 Index = MapCombo->FindOptionIndex(SelectedItem);
	if (bFillingCombos || !Mode || !MapBoards.IsValidIndex(Index))
	{
		return;
	}
	if (!Mode->SelectBoard(MapBoards[Index].Get()))
	{
		RefreshMapCombo();   // 거절됐다(전투 중) — 실제 값으로 되돌린다
	}
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

void UBattleHUDWidget::SetupControlCombo(UComboBoxString& Combo, const int32 Team)
{
	Combo.ClearOptions();
	Combo.AddOption(ToDisplayText(ESHTeamControl::RLModel));
	Combo.AddOption(ToDisplayText(ESHTeamControl::BehaviorTree));
	if (const ASH_BattleGameMode* Mode = GameMode.Get())
	{
		// 구독 전에 부르므로 OnSelectionChanged 가 불리지 않는다.
		Combo.SetSelectedOption(ToDisplayText(Mode->GetTeamControl(Team)));
	}
}

void UBattleHUDWidget::ApplyControlSelection(UComboBoxString& Combo, const int32 Team, const FString& SelectedItem)
{
	ASH_BattleGameMode* Mode = GameMode.Get();
	if (!Mode)
	{
		return;
	}
	const ESHTeamControl Wanted = SelectedItem == ToDisplayText(ESHTeamControl::BehaviorTree)
		? ESHTeamControl::BehaviorTree : ESHTeamControl::RLModel;
	if (Wanted == Mode->GetTeamControl(Team))
	{
		return;   // 되돌리기로 다시 불린 경우 포함
	}
	if (!Mode->SetTeamControl(Team, Wanted))
	{
		// 거절됐다(행동 트리 에셋 없음 · 전투 중). 화면을 실제 값으로 되돌린다 — 이유는 출력 로그에 있다.
		Combo.SetSelectedOption(ToDisplayText(Mode->GetTeamControl(Team)));
	}
}

void UBattleHUDWidget::HandleAllyControlChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	ApplyControlSelection(*AllyControlCombo, 0, SelectedItem);
	UpdateModelComboStates();
}

void UBattleHUDWidget::HandleEnemyControlChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	ApplyControlSelection(*EnemyControlCombo, 1, SelectedItem);
	UpdateModelComboStates();
}

void UBattleHUDWidget::RefreshModelCombos()
{
	const ASH_BattleGameMode* Mode = GameMode.Get();
	if (!Mode)
	{
		return;
	}

	ModelLabels.Reset();
	ModelDirectories.Reset();
	for (const SHPolicyCatalog::FEntry& Entry : SHPolicyCatalog::Scan())
	{
		ModelLabels.Add(Entry.Label);
		ModelDirectories.Add(Entry.Directory);
	}
	// 지금 고른 모델(유닛 정의의 기본값 포함)이 목록에 없으면 맨 앞에 넣는다 — 화면이 실제 값과 어긋나지 않게.
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const FString Current = Mode->GetTeamPolicy(Team);
		if (!Current.IsEmpty() && !ModelDirectories.Contains(Current))
		{
			ModelLabels.Insert(SHPolicyCatalog::MakeLabel(Current), 0);
			ModelDirectories.Insert(Current, 0);
		}
	}

	TGuardValue<bool> Guard(bFillingCombos, true);
	for (int32 Team = 0; Team < 2; ++Team)
	{
		UComboBoxString& Combo = Team == 0 ? *AllyModelCombo : *EnemyModelCombo;
		Combo.ClearOptions();
		for (const FString& Label : ModelLabels)
		{
			Combo.AddOption(Label);
		}
		const int32 Index = ModelDirectories.IndexOfByKey(Mode->GetTeamPolicy(Team));
		if (ModelLabels.IsValidIndex(Index))
		{
			Combo.SetSelectedOption(ModelLabels[Index]);
		}
	}
}

void UBattleHUDWidget::ApplyModelSelection(const int32 Team, const FString& SelectedLabel)
{
	ASH_BattleGameMode* Mode = GameMode.Get();
	const int32 Index = ModelLabels.IndexOfByKey(SelectedLabel);
	if (bFillingCombos || !Mode || !ModelDirectories.IsValidIndex(Index))
	{
		return;
	}
	if (!Mode->SetTeamPolicy(Team, ModelDirectories[Index]))
	{
		RefreshModelCombos();   // 거절됐다(전투 중) — 실제 값으로 되돌린다
	}
}

void UBattleHUDWidget::HandleAllyModelChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	ApplyModelSelection(0, SelectedItem);
}

void UBattleHUDWidget::HandleEnemyModelChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	ApplyModelSelection(1, SelectedItem);
}

void UBattleHUDWidget::UpdateModelComboStates()
{
	const ASH_BattleGameMode* Mode = GameMode.Get();
	const bool bPlacement = Mode && Mode->GetPhase() == ESHBattlePhase::Placement;
	AllyModelCombo->SetIsEnabled(bPlacement && Mode->GetTeamControl(0) == ESHTeamControl::RLModel);
	EnemyModelCombo->SetIsEnabled(bPlacement && Mode->GetTeamControl(1) == ESHTeamControl::RLModel);
}

FString UBattleHUDWidget::DescribeTeam(const int32 Team) const
{
	const ASH_BattleGameMode* Mode = GameMode.Get();
	if (!Mode || Mode->GetTeamControl(Team) == ESHTeamControl::BehaviorTree)
	{
		return ToDisplayText(ESHTeamControl::BehaviorTree);
	}
	return FString::Printf(TEXT("RL(%s)"), *SHPolicyCatalog::MakeLabel(Mode->GetTeamPolicy(Team)));
}

void UBattleHUDWidget::HandleStartFailed(const FString& Reason)
{
	// 배치 수 글자 자리에 이유를 보여 준다. 유닛을 놓거나 빼면 다시 "배치 N / 10" 으로 돌아간다.
	PlacedText->SetText(FText::FromString(FString::Printf(TEXT("시작 실패: %s"), *Reason)));
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
		Mode->OnStartFailed.Remove(StartFailedHandle);
		Mode->OnBoardChanged.Remove(BoardChangedHandle);
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

	// 조종 방식은 배치 중에만 바꾼다. 전투 중에 바꾸면 이미 돌고 있는 정책 · 트리와 어긋난다.
	AllyControlCombo->SetIsEnabled(bPlacement);
	EnemyControlCombo->SetIsEnabled(bPlacement);
	MapCombo->SetIsEnabled(bPlacement);
	if (bPlacement)
	{
		RefreshModelCombos();   // 학습이 그 사이 새 스냅샷을 만들었을 수 있다
	}
	UpdateModelComboStates();

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
	// 어떤 조합의 결과인지 같이 보여 준다(비교 실험에서 헷갈리지 않게).
	ResultText->SetText(FText::FromString(FString::Printf(TEXT("%s\n아군 %s vs 적군 %s"), Message,
		*DescribeTeam(0), *DescribeTeam(1))));
}
