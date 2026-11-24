// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Battle/SH_BattleTypes.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"   // ESelectInfo — UFUNCTION 시그니처에 쓰려면 정의가 보여야 한다
#include "BattleHUDWidget.generated.h"

class ASH_BattleBoard;
class ASH_BattleGameMode;
class UButton;
class UCheckBox;
class UComboBoxString;
class USlider;
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

	// 슬라이더 · 체크박스의 이벤트도 동적 델리게이트다.
	UFUNCTION()
	void HandleSpeedChanged(float Value);

	UFUNCTION()
	void HandleVisionRaysChanged(bool bIsChecked);

	UFUNCTION()
	void HandleAimRaysChanged(bool bIsChecked);

	// 콤보박스의 OnSelectionChanged 도 동적 델리게이트다. 시그니처: (FString 고른 항목, 고른 방법)
	UFUNCTION()
	void HandleAllyControlChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleEnemyControlChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleAllyModelChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleEnemyModelChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleMapChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	/** 레벨의 보드 목록으로 맵 콤보박스를 채우고 지금 보드를 고른다. */
	void RefreshMapCombo();

	/** 게임모드가 보드를 바꿨다 — 배치 수 구독을 옮기고 레이 체크박스 상태를 새 보드에 건다. */
	void HandleBoardChanged(ASH_BattleBoard* NewBoard);

	/** 보드의 배치 수 방송을 구독한다(이전 보드 구독은 푼다). */
	void BindBoard(ASH_BattleBoard* NewBoard);

	/** 콤보박스에 항목을 넣고 게임모드의 현재 값을 보여 준다. */
	void SetupControlCombo(UComboBoxString& Combo, int32 Team);

	/** 스냅샷 목록을 다시 훑어 두 모델 콤보박스를 채운다. 배치 단계로 돌아올 때마다(학습이 새 스냅샷을 만들었을 수 있다). */
	void RefreshModelCombos();

	/** 고른 표시 이름 → 스냅샷 폴더로 바꿔 게임모드에 알린다. */
	void ApplyModelSelection(int32 Team, const FString& SelectedLabel);

	/** 모델 콤보박스는 배치 단계이고 그 팀이 RL 일 때만 쓸 수 있다. */
	void UpdateModelComboStates();

	/** 결과 화면에 쓰는 팀 설명: "RL(START_11_13 · 017h06m)" / "행동 트리". */
	[[nodiscard]] FString DescribeTeam(int32 Team) const;

	void HandleStartFailed(const FString& Reason);

	/** 고른 항목을 게임모드에 알린다. 거절되면(행동 트리 에셋 없음) 콤보박스를 원래 값으로 되돌린다. */
	void ApplyControlSelection(UComboBoxString& Combo, int32 Team, const FString& SelectedItem);

	void HandlePhaseChanged(ESHBattlePhase NewPhase);
	void HandlePlacementChanged(int32 Placed, int32 Required);
	void RefreshSpeedText(float Speed);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ReplayButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PlacedText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ResultText;

	/** 좌측 상단 배속 슬라이더. 범위는 코드가 게임모드의 Min/MaxGameSpeed 로 맞춘다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> SpeedSlider;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SpeedText;

	/** 시야 레이 표시. 기본 꺼짐. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> VisionRaysCheckBox;

	/** 조준 레이 표시. 기본 꺼짐. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> AimRaysCheckBox;

	/** 팀별 조종 방식(RL 모델 / 행동 트리). 배치 단계에서만 바꿀 수 있다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> AllyControlCombo;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> EnemyControlCombo;

	/** 팀별 RL 모델(스냅샷). Training/<이름>/cum… 과 Snapshots/<날짜>/cum… 에서 고른다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> AllyModelCombo;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> EnemyModelCombo;

	/** 맵(보드) 선택. 레벨에 놓인 보드 전부가 나온다(ASH_BattleBoard::DisplayName). 배치 단계에서만 바꿀 수 있다. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> MapCombo;

	/** 맵 콤보박스 항목 ↔ 보드. 같은 순서. */
	TArray<TWeakObjectPtr<ASH_BattleBoard>> MapBoards;

	/** 콤보박스 항목(표시 이름) ↔ 스냅샷 폴더. 같은 순서. */
	TArray<FString> ModelLabels;
	TArray<FString> ModelDirectories;

	/** 코드가 콤보박스를 채우는 동안에는 선택 이벤트를 무시한다(SetSelectedOption 도 OnSelectionChanged 를 부른다). */
	bool bFillingCombos = false;

	TWeakObjectPtr<ASH_BattleGameMode> GameMode;
	TWeakObjectPtr<ASH_BattleBoard> Board;
	FDelegateHandle PhaseHandle;
	FDelegateHandle PlacementHandle;
	FDelegateHandle StartFailedHandle;
	FDelegateHandle BoardChangedHandle;
};
