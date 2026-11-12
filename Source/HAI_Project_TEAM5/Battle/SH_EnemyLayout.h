// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SH_EnemyLayout.generated.h"

class USH_UnitDefinition;

/** 적 하나의 배치. */
USTRUCT(BlueprintType)
struct FSHEnemySlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Layout")
	TObjectPtr<USH_UnitDefinition> Unit;

	UPROPERTY(EditAnywhere, Category = "Layout")
	FIntPoint Cell = FIntPoint::ZeroValue;
};

/** 레벨을 열 때 미리 서 있는 적 팀의 배치. */
UCLASS(BlueprintType)
class HAI_PROJECT_TEAM5_API USH_EnemyLayout : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Layout")
	TArray<FSHEnemySlot> Slots;
};
