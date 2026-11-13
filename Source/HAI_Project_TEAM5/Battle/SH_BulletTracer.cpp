// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BulletTracer.h"

#include "Components/StaticMeshComponent.h"

ASH_BulletTracer::ASH_BulletTracer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	// ★ 그림일 뿐이다. 콜리전이 있으면 시야 레이 · 다른 총알 트레이스 · 이동을 막아 판정이 바뀐다.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	Mesh->SetMobility(EComponentMobility::Movable);

	// 엔진이 지우는 안전장치. Launch 를 못 받았거나 끝점이 이상해도 남지 않는다(배속 0.1 에서도 충분한 값).
	InitialLifeSpan = 10.0f;
}

void ASH_BulletTracer::Launch(const FVector& Start, const FVector& End, const float Speed)
{
	From = Start;
	const FVector Delta = End - Start;
	TotalDistance = Delta.Size();
	Direction = TotalDistance > UE_KINDA_SMALL_NUMBER ? Delta / TotalDistance : FVector::ForwardVector;
	Travelled = 0.0;
	SpeedCmPerSec = FMath::Max(Speed, 1.0f);

	SetActorLocationAndRotation(Start, Direction.Rotation());
}

void ASH_BulletTracer::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Travelled += static_cast<double>(SpeedCmPerSec) * DeltaSeconds;
	if (Travelled >= TotalDistance)
	{
		Destroy();
		return;
	}
	SetActorLocation(From + Direction * Travelled);
}
