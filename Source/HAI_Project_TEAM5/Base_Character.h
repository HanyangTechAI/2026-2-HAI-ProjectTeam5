// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAI_Project_TEAM5Character.h"
#include "Gun.h"
#include "Engine/World.h"
#include "Base_Character.generated.h"


class UInputAction;

UCLASS()
class HAI_PROJECT_TEAM5_API ABase_Character : public AHAI_Project_TEAM5Character
{
	GENERATED_BODY()
public:
	/** Constructor */
	ABase_Character();	
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

	AGun* gun;
	
};
