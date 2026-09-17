// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gun.generated.h"

class AController;
class USkeletalMeshComponent;
class USceneComponent;

/** PullTrigger 한 번의 결과. 보상 계산이 이 값을 읽는다. */
USTRUCT(BlueprintType)
struct FGunFireResult
{
	GENERATED_BODY()

	/** 쿨다운을 통과해 실제로 발사됐는가. false 면 나머지 필드는 의미 없다. */
	UPROPERTY(BlueprintReadOnly, Category = "Gun")
	bool bFired = false;

	/** 무언가에 맞았는가 (벽 포함). */
	UPROPERTY(BlueprintReadOnly, Category = "Gun")
	bool bHitSomething = false;

	/** 적대 대상에 맞았는가. 보상의 hit/miss 항이 이 값을 쓴다. */
	UPROPERTY(BlueprintReadOnly, Category = "Gun")
	bool bHitHostile = false;

	/** 실제로 깎인 HP. 오버킬은 제외된다. */
	UPROPERTY(BlueprintReadOnly, Category = "Gun")
	float DamageApplied = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Gun")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Gun")
	TObjectPtr<AActor> HitActor = nullptr;
};

UCLASS()
class HAI_PROJECT_TEAM5_API AGun : public AActor
{
	GENERATED_BODY()

public:
	AGun();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	//~ 스탯 ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun", meta = (ClampMin = "0.0"))
	float Damage = 10.0f;

	/** 사거리(cm). 아레나 최대 현(3500)보다 크게 두어 사거리가 제약이 되지 않게 한다 —
	 *  조준 학습 단계에서 "못 맞힌 이유" 가 조준 실패인지 사거리 밖인지 섞이면 안 된다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun", meta = (ClampMin = "0.0"))
	float Range = 6000.0f;

	/** 발사 간격(초). 0.15 = 초당 약 6.7발. 행동 마스킹이 이 쿨다운을 읽는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun", meta = (ClampMin = "0.0"))
	float FireInterval = 0.15f;

	/** 탄퍼짐 반각(도). ★ 학습 중에는 0 을 권한다 — FMath::VRandCone 은 전역 RNG 를
	 *  쓰므로 시드를 고정해도 재현되지 않아 결정론이 깨진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun", meta = (ClampMin = "0.0"))
	float SpreadDegrees = 0.0f;

	/** 트레이스 원점을 소유 폰의 발밑에서 얼마나 올릴지(cm).
	 *
	 *  ★ 총구 소켓(MuzzleFlashSocket)에서 쏘지 않는 이유: 소켓 위치는 애니메이션 포즈에
	 *  의존하는데, 포즈는 LOD / VisibilityBasedAnimTickOption / -nullrhi 에 따라 달라진다.
	 *  같은 시드로 같은 궤적이 나오지 않으면 디버깅도 보고서도 불가능해진다.
	 *  캡슐 기준 고정 오프셋을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gun")
	float MuzzleHeight = 60.0f;

	UPROPERTY(EditAnywhere, Category = "Gun|Debug")
	bool bDrawDebugTrace = false;

	UPROPERTY(EditAnywhere, Category = "Gun|Debug")
	float DebugTraceDuration = 1.0f;

	/** 다른 아레나의 유닛을 맞힌 횟수. **학습이 정상이면 끝까지 0 이어야 한다.**
	 *  0 이 아니면 벽 콜리전이나 GymsSpacing 이 잘못된 것이다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gun|Debug")
	int32 CrossGymHitCount = 0;

	//~ API -------------------------------------------------------------------

	/** 쿨다운이 끝났는가. 행동 마스킹(MakeBoolActionModifier)이 이 값을 읽는다. */
	UFUNCTION(BlueprintPure, Category = "Gun")
	bool CanFire() const;

	/** 남은 쿨다운(초). 관측에 들어간다. */
	UFUNCTION(BlueprintPure, Category = "Gun")
	float GetCooldownRemaining() const;

	/** 발사. 쿨다운 중이면 아무 일도 하지 않고 bFired=false 를 반환한다. */
	UFUNCTION(BlueprintCallable, Category = "Gun")
	FGunFireResult PullTrigger();

	/** 에피소드 리셋 — 쿨다운을 비운다. 빠뜨리면 부활 직후 몇 초간 발사 불가. */
	UFUNCTION(BlueprintCallable, Category = "Gun")
	void ResetForNewMatch();

	/** 조준 원점과 방향. 관측(조준 오차) 계산도 같은 함수를 써야 실제 탄도와 일치한다. */
	UFUNCTION(BlueprintPure, Category = "Gun")
	bool GetAimOriginAndDirection(FVector& OutOrigin, FVector& OutDirection) const;

protected:
	/** 소유 폰의 컨트롤러를 매번 조회한다.
	 *
	 *  ★ BeginPlay 에서 캐시하지 않는 이유: AutoPossessAI 로 스폰되는 폰은 BeginPlay
	 *  시점에 아직 possess 되지 않았을 수 있고, 풀링된 유닛은 unpossess/repossess 로
	 *  컨트롤러가 바뀐다. 캐시된 포인터는 조용히 낡는다. */
	AController* ResolveOwnerController() const;

	/**
	 *  맞은 대상이 소유자와 같은 아레나에 속하는가.
	 *
	 *  ★ 총알 누출을 막는 2차 방어선이다. 1차는 벽이 Bullet 채널을 Block 하는 것.
	 *  사거리 6000 이 아레나 간 피치 5000 보다 크기 때문에, 벽에 틈이 있거나 유닛이
	 *  넉백으로 벽을 넘어가면 옆 아레나 유닛을 맞힐 수 있다. 그러면 맞은 쪽은
	 *  자기 관측에 없는 원인으로 체력을 잃고, 학습 신호가 조용히 오염된다.
	 *
	 *  둘 중 하나라도 풀에 속하지 않은 액터(벽, 수동 배치 테스트 유닛)면 true 를 돌려
	 *  기존 동작을 유지한다.
	 */
	bool IsSameGymAsOwner(AActor* Other) const;

	/** 마지막 발사 시각(월드 시간). ResetForNewMatch 가 되돌린다. */
	float LastFireTime = -1000.0f;
};
