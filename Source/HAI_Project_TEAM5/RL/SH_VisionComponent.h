// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SH_VisionComponent.generated.h"

/** CanSeeActor 가 실패한 이유. 봇이 왜 표적을 못 잡는지 집계하는 데 쓴다. */
UENUM()
enum class ESHVisionResult : uint8
{
	Visible,      /** 보인다. */
	Invalid,      /** 소유자/표적/월드가 없다. */
	OutOfRange,   /** MaxRange 밖. */
	OutsideFOV,   /** 시야각 밖. */
	Occluded,     /** 무언가에 가려졌다 (벽 · 엄폐물 · 아군). */
	NoHit,        /** 표적 캡슐조차 못 맞혔다 — 콜리전이 꺼진 시체/풀에서 빠진 유닛. */
};

/** 부채꼴 레이 하나의 결과. */
USTRUCT(BlueprintType)
struct FSHVisionRay
{
	GENERATED_BODY()

	/** 0 = 눈앞에서 막힘, 1 = MaxRange 까지 아무것도 없음. */
	UPROPERTY(BlueprintReadOnly, Category = "SH|Vision")
	float Depth01 = 1.0f;

	/** 이 레이가 적대 유닛에 닿았는가. */
	UPROPERTY(BlueprintReadOnly, Category = "SH|Vision")
	bool bHostile = false;
};

/**
 *  시야 제한 센서. 조준선을 중심으로 부채꼴을 훑는다.
 *
 *  ★ 핵심 설계: 레이를 **총알과 같은 충돌 채널**(ECC_GameTraceChannel1 = "Bullet")로 쏜다.
 *  그러면 "레이가 적을 본다" 가 "그 방향으로 쏜 총알이 맞는다" 와 같은 뜻이 되고,
 *  관측과 행동이 같은 기하 위에 놓인다. 엄폐물 판정도 따로 만들 필요가 없다 —
 *  엄폐물이 이 채널을 Block 하는 것만으로 성립한다.
 *
 *  ★ RL 유닛과 스크립트 봇이 **같은 컴포넌트를 공유한다.** 한쪽만 시야 제한을 받으면
 *  정책은 싸우는 법 대신 그 비대칭을 학습한다. 다만 쓰는 방식이 다르다:
 *   - RL 유닛: Scan() 의 부채꼴 전체가 관측 벡터가 된다 (16개 레이).
 *   - 스크립트 봇: CanSeeActor() 로 후보를 하나씩 점 조회한다 (레이 1발).
 *
 *  ★ 남는 비대칭 하나: 부채꼴은 이산적이라 레이 사이로 적이 빠질 수 있다.
 *  90도 / 16레이 = 6도 간격인데, 거리 1200 에서 캡슐(반경 42)이 차지하는 각은 약 4도다.
 *  즉 멀리 있는 적은 RL 유닛의 부채꼴에서 누락될 수 있지만 CanSeeActor 는 잡아낸다.
 *  전지적 상태보다는 훨씬 작은 차이이고, "멀리 있는 적은 놓칠 수 있다" 는 오히려
 *  자연스러운 성질이라 그대로 둔다. RayCount 를 올리면 좁혀진다.
 */
UCLASS(ClassGroup = (SH), meta = (BlueprintSpawnableComponent))
class HAI_PROJECT_TEAM5_API USH_VisionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USH_VisionComponent();

	//~ 시야 ------------------------------------------------------------------

	/** 시야각 전체 폭(도). 커리큘럼 손잡이 — 360 → 180 → 120 → 90 으로 조인다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Vision", meta = (ClampMin = "1.0", ClampMax = "360.0"))
	float FOVDegrees = 90.0f;

	/** 부채꼴 레이 개수. ★ 관측 스키마의 StaticArray 크기와 반드시 같아야 한다 —
	 *  다르면 관측 벡터 크기가 어긋나 학습이 시작되지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Vision", meta = (ClampMin = "1"))
	int32 RayCount = 16;

	/** 시야 거리(cm). ★ AGun::Range 와 반드시 같은 값이어야 한다.
	 *  더 짧으면 "안 보이는데 맞는" 구간, 더 길면 "보이는데 안 맞는" 구간이 생기고
	 *  정책이 그 불일치를 학습한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Vision", meta = (ClampMin = "1.0"))
	float MaxRange = 6000.0f;

	/** 눈높이(cm, 액터 원점 기준). ★ AGun::MuzzleHeight 와 반드시 같은 값. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Vision")
	float EyeHeight = 60.0f;

	/** 기억 지평(초). GetTimeSinceSeen 의 상한이자 관측 정규화 Scale 이다.
	 *  ★ 관측 스키마의 Float 정규화 Scale 과 같은 값을 써야 0~1 로 떨어진다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Vision", meta = (ClampMin = "0.1"))
	float MemoryHorizonSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|Debug")
	bool bDrawDebug = false;

	//~ API -------------------------------------------------------------------

	/**
	 *  부채꼴을 훑고 결과를 캐시한다.
	 *
	 *  ★ 같은 프레임에 두 번 불러도 트레이스는 한 번만 돈다. RunTraining 이 한 프레임에
	 *  GatherRewards 와 GatherObservations 를 연달아 부르는데, 둘이 **같은 스캔 결과를
	 *  봐야** 보상과 관측이 일치한다. bForce 로 강제할 수 있다.
	 */
	UFUNCTION(BlueprintCallable, Category = "SH|Vision")
	void Scan(bool bForce = false);

	const TArray<FSHVisionRay>& GetRays() const { return Rays; }

	/**
	 *  이 액터가 시야각 안에 있고 총알이 닿는가. 레이 **1발**.
	 *
	 *  부채꼴과 같은 기하(같은 채널 · 같은 눈높이 · 같은 사거리 · 같은 시야각)를 쓰므로
	 *  "보인다" 의 정의가 한 곳에만 있다. 스크립트 봇의 표적 조건이 이걸 쓴다.
	 */
	UFUNCTION(BlueprintPure, Category = "SH|Vision")
	bool CanSeeActor(const AActor* Target) const;

	/**
	 *  CanSeeActor 와 같은 판정을 하되 **실패 이유**를 돌려준다.
	 *  CanSeeActor 가 이걸 호출하므로 판정 로직은 한 곳에만 있다.
	 *
	 *  @param OutBlocker Occluded 일 때 가린 액터 이름. 그 외에는 NAME_None.
	 */
	ESHVisionResult ClassifyVisibility(const AActor* Target, FName& OutBlocker) const;

	UFUNCTION(BlueprintPure, Category = "SH|Vision")
	bool IsHostileVisible() const { return bHostileVisible; }

	/** 조준선 기준 부호 있는 각(도). 왼쪽이 음수. 보일 때만 의미가 있다. */
	UFUNCTION(BlueprintPure, Category = "SH|Vision")
	float GetHostileAngleDeg() const { return HostileAngleDeg; }

	UFUNCTION(BlueprintPure, Category = "SH|Vision")
	float GetHostileDepth01() const { return HostileDepth01; }

	/** 마지막으로 적을 본 방향(조준선 기준 도). 안 보일 때 정책이 추종할 단서다. */
	UFUNCTION(BlueprintPure, Category = "SH|Vision")
	float GetLastSeenAngleDeg() const { return LastSeenAngleDeg; }

	/** 마지막으로 본 뒤 흐른 시간(초). MemoryHorizonSeconds 로 클램프된다. */
	UFUNCTION(BlueprintPure, Category = "SH|Vision")
	float GetTimeSinceSeen() const;

	/** 조준 원점과 방향. AGun::GetAimOriginAndDirection 과 같은 기준이어야 한다. */
	bool GetEyeAndAim(FVector& OutEye, FRotator& OutAim) const;

	/**
	 *  ★ 에피소드 리셋에서 반드시 호출한다.
	 *  빠뜨리면 지난 판의 "마지막으로 본 방향" 이 다음 판으로 새어 들어가고,
	 *  정책이 존재하지 않는 적을 향해 출발한다. 에러는 나지 않는다.
	 */
	UFUNCTION(BlueprintCallable, Category = "SH|Vision")
	void ResetMemory();

private:
	TArray<FSHVisionRay> Rays;

	bool  bHostileVisible  = false;
	float HostileAngleDeg  = 0.0f;
	float HostileDepth01   = 1.0f;
	float LastSeenAngleDeg = 0.0f;
	float LastSeenTime     = -1000.0f;

	/** 같은 프레임 중복 스캔을 막는다. */
	uint64 LastScanFrame = 0;
};
