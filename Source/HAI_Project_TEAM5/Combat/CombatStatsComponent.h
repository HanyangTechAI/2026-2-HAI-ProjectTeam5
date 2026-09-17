// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatStatsComponent.generated.h"

class UCombatStatsComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCombatDeath, AActor*, Victim, AActor*, Causer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCombatDamaged, AActor*, Victim, float, Amount, AActor*, Causer);

/**
 *  유닛의 체력 · 팀 · RL 보상 누산기.
 *
 *  인터페이스가 아니라 컴포넌트인 이유: 기존 클래스를 리팩터링하는 게 아니라
 *  HP 자체가 없는 상태에서 처음부터 붙이는 것이라 위험이 없고,
 *  히트 판정 쪽에서 FindComponentByClass 한 번으로 끝나기 때문이다.
 *
 *  ★ 누산기 규약: 게임플레이가 값을 더하고, GatherAgentReward 가 읽은 뒤
 *  ConsumeRewardAccumulators() 로 0으로 비운다. 비우지 않으면 같은 데미지가
 *  여러 결정 스텝에 걸쳐 중복 계산되어 보상이 부풀고, 크리틱이 그 부푼 값을
 *  학습한다. 에러는 나지 않는다.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class HAI_PROJECT_TEAM5_API UCombatStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatStatsComponent();

	//~ 스탯 ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "1.0"))
	float MaxHP = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	float CurrentHP = 100.0f;

	/** 팀 식별자. 같은 값이면 아군, 다르면 적. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	uint8 TeamId = 0;

	/** 꺼두면 같은 팀끼리도 데미지가 들어간다. 학습에서는 기본값(끔)을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	bool bAllowFriendlyFire = false;

	//~ RL 보상 누산기 ---------------------------------------------------------

	/** 이 유닛이 가한 데미지 (실제로 깎인 양 — 오버킬 제외). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|RL")
	float DamageDealtAccum = 0.0f;

	/** 이 유닛이 받은 데미지. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|RL")
	float DamageTakenAccum = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|RL")
	int32 ShotsFiredAccum = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|RL")
	int32 ShotsHitAccum = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|RL")
	int32 KillsAccum = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|RL")
	bool bDiedThisInterval = false;

	//~ 이벤트 ----------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FOnCombatDeath OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FOnCombatDamaged OnDamaged;

	//~ API -------------------------------------------------------------------

	/**
	 *  데미지를 적용한다.
	 *  @return 실제로 깎인 양. 남은 HP 로 클램프되므로 오버킬은 보상에 반영되지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	float ApplyDamage(float Amount, AActor* Causer);

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsAlive() const { return CurrentHP > 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetHPRatio() const { return (MaxHP > 0.0f) ? FMath::Clamp(CurrentHP / MaxHP, 0.0f, 1.0f) : 0.0f; }

	/** 사격 1발을 누산기에 기록한다. 명중 여부와 무관하게 호출한다. */
	UFUNCTION(BlueprintCallable, Category = "Combat|RL")
	void RecordShotFired(bool bHitHostile);

	/** 에피소드 리셋 — HP 복구 + 누산기 전체 클리어. */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void ResetForNewMatch();

	/** ★ GatherAgentReward 가 읽은 직후 반드시 호출한다. */
	UFUNCTION(BlueprintCallable, Category = "Combat|RL")
	void ConsumeRewardAccumulators();

	//~ 정적 헬퍼 -------------------------------------------------------------

	/** 액터에서 스탯 컴포넌트를 찾는다. 없으면 nullptr. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	static UCombatStatsComponent* FindOn(AActor* Actor);

	/** 서로 다른 팀인가. 어느 한쪽이라도 스탯이 없으면 false. */
	UFUNCTION(BlueprintPure, Category = "Combat")
	static bool AreHostile(AActor* A, AActor* B);

protected:
	virtual void BeginPlay() override;
};
