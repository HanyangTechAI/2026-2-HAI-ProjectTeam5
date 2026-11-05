// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "Templates/SubclassOf.h"
#include "SH_UnitDefinition.generated.h"

class ASH_RLCharacter;
class UTexture2D;

/**
 *  유닛 종류 하나의 정의. 코드가 아니라 에셋이다 — 종류를 늘릴 때 에셋만 추가한다.
 *  유닛이 이 클래스를 상속하지 않고 참조한다(합성): 정의는 공유되는 읽기 전용 데이터다.
 *
 *  ★ 팀마다 클래스가 따로인 이유: 팀은 BP 의 CombatStats.TeamId 와 팀 색이 정한다.
 */
UCLASS(BlueprintType)
class HAI_PROJECT_TEAM5_API USH_UnitDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	[[nodiscard]] TSubclassOf<ASH_RLCharacter> GetUnitClass(int32 Team) const
	{
		return Team == 0 ? UnitClassTeam0 : UnitClassTeam1;
	}

	/** 반환된 참조는 이 에셋이 살아 있는 동안만 유효하다. */
	[[nodiscard]] const FText& GetDisplayName() const { return DisplayName; }

	/** 절대 경로 또는 프로젝트 폴더 기준 상대 경로. */
	[[nodiscard]] const FString& GetPolicyDirectory() const { return PolicySnapshotDirectory.Path; }

private:
	UPROPERTY(EditDefaultsOnly, Category = "Unit")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, Category = "Unit")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, Category = "Unit")
	TSubclassOf<ASH_RLCharacter> UnitClassTeam0;

	UPROPERTY(EditDefaultsOnly, Category = "Unit")
	TSubclassOf<ASH_RLCharacter> UnitClassTeam1;

	/** 이 유닛을 조종하는 정책의 스냅샷 폴더. 예: Snapshots/2026-10-25/cum016h00m */
	UPROPERTY(EditDefaultsOnly, Category = "Unit")
	FDirectoryPath PolicySnapshotDirectory;
};
