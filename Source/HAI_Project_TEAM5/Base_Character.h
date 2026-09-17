// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAI_Project_TEAM5Character.h"
#include "Gun.h"
#include "Engine/World.h"
#include "LearningAgentsEntityInterface.h"
#include "Base_Character.generated.h"


class UInputAction;
class UCombatStatsComponent;
class ALearningAgentsGymBase;

UCLASS()
class HAI_PROJECT_TEAM5_API ABase_Character : public AHAI_Project_TEAM5Character,
                                              public ILearningAgentsEntityInterface
{
	GENERATED_BODY()
public:
	/** Constructor */
	ABase_Character();

	/** 체력 · 팀 · RL 보상 누산기. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UCombatStatsComponent> CombatStats;

	/** 총. 에피소드 리셋에서 쿨다운을 되돌리려면 밖에서도 보여야 한다. */
	UPROPERTY(BlueprintReadOnly, Category="Combat")
	TObjectPtr<AGun> gun;

	/** 죽었을 때 — 이동 정지, 캡슐 콜리전 해제. 부활은 리셋이 담당한다. */
	UFUNCTION()
	void HandleDeath(AActor* Victim, AActor* Causer);

	/** 에피소드 리셋. HP 복구 · 누산기 클리어 · 콜리전 재활성화 · 쿨다운 리셋. */
	UFUNCTION(BlueprintCallable, Category="Combat")
	void ResetForNewMatch();

	//~ ILearningAgentsEntityInterface ----------------------------------------
	//
	//  이 인터페이스가 없으면 ULearningAgentsEntitiesManagerComponent 의
	//  CheckEntityClasses() 가 Error 를 찍고 **유닛을 하나도 스폰하지 않은 채** 진행한다
	//  (LearningAgentsEntitiesManagerComponent.cpp:266). 크래시도 중단도 없다.
	//
	//  전부 BlueprintNativeEvent 이므로 반드시 _Implementation 접미사로 오버라이드한다.

	/** 풀에 처음 만들어질 때 1회. 소속 Gym 을 기억한다. */
	virtual void InitializeEntity_Implementation(ALearningAgentsGymBase* InGym) override;

	/** 에피소드마다. 엔진이 랜덤 트랜스폼을 적용한 직후에 불린다. */
	virtual void ResetEntity_Implementation(ALearningAgentsGymBase* InGym) override;

	/** 소속 Gym. ALearningAgentsGymBase::IsMemberOfGym 이 이 값을 쓴다 —
	 *  총알이 옆 아레나로 넘어갔는지 판정하는 근거다. */
	virtual ALearningAgentsGymBase* GetGym_Implementation() const override;

	/** 전투에 참여시킨다. */
	virtual void EnableEntity_Implementation() override;

	/** 전투에서 빼지만 **파괴하지는 않는다**. 풀이 다음 에피소드에 재사용한다. */
	virtual void DisableEntity_Implementation() override;

	virtual bool IsEntityEnabled_Implementation() const override { return bEntityEnabled; }

	/** 소속 아레나. InitializeEntity 가 채운다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<ALearningAgentsGymBase> CachedGym;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	bool bEntityEnabled = true;

protected:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	void Shoot();
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* ShootAction;
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* FocusAction;

	/** 조준 중인가. AnimBP가 읽는다. */
	UPROPERTY(BlueprintReadOnly, Category="Combat")
	bool bIsFocusing = false;
	
	void StartFocus();
	void StopFocus();
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<AGun> GunClass;
};
