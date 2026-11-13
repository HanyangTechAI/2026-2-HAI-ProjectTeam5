// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SH_BulletTracer.generated.h"

class UStaticMeshComponent;

/**
 *  날아가는 총알 그림. **판정은 하지 않는다** — 명중 · 피해는 AGun 의 히트스캔이 쏘는 순간 이미 끝냈다.
 *  이 액터는 그 결과(시작점 → 맞은 곳)를 따라 날아가는 모습만 보여 주고, 도착하면 사라진다.
 *
 *  ★ 판정을 날아가는 총알로 옮기지 않은 이유: 정책은 히트스캔으로 학습됐다. 도착해야 맞는 규칙이면
 *  학습 때 본 적 없는 상황(쏘고 나서 표적이 비켜남)이 생겨 명중률과 승패가 달라진다.
 *
 *  메시 · 머티리얼은 BP(BP_BulletTracer)에서 정한다. 메시의 +X 가 날아가는 방향이다.
 *  Tick 의 DeltaSeconds 는 전역 시간 배율이 적용된 값이라 배속을 낮추면 총알도 같이 느려진다.
 */
UCLASS(Blueprintable)
class HAI_PROJECT_TEAM5_API ASH_BulletTracer : public AActor
{
	GENERATED_BODY()

public:
	ASH_BulletTracer();

	/** 시작점에서 끝점까지 Speed(cm/s)로 날아간다. 스폰 직후 한 번 부른다. */
	void Launch(const FVector& Start, const FVector& End, float Speed);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "SH|Tracer")
	TObjectPtr<UStaticMeshComponent> Mesh;

	FVector From = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	double TotalDistance = 0.0;
	double Travelled = 0.0;
	float SpeedCmPerSec = 15000.0f;
};
