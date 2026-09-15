// Fill out your copyright notice in the Description page of Project Settings.


#include "Gun.h"

#include "DrawDebugHelpers.h"
#include "Components/SkeletalMeshComponent.h"

// Sets default values
AGun::AGun()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	SetRootComponent(SceneRoot);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SceneRoot);
	
}

// Called when the game starts or when spawned
void AGun::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AGun::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}
void AGun::PullTrigger()
{
	UE_LOG(LogTemp, Display, TEXT("PullTrigger"));
	if (Controller)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		//DrawDebugCamera(GetWorld(), ViewLocation, ViewRotation,90.0f,2.0f,FColor::Red,true);
		FHitResult Hit;
		FVector EndLocation = ViewLocation + ViewRotation.Vector()*Range;
		FCollisionQueryParams CollisionParams;
		CollisionParams.AddIgnoredActor(this);
		CollisionParams.AddIgnoredActor(GetOwner());
		bool isHit = GetWorld()->LineTraceSingleByChannel(Hit,ViewLocation,EndLocation,ECC_GameTraceChannel1,CollisionParams);
		if (isHit)
		{
			DrawDebugSphere(GetWorld(),Hit.ImpactPoint,5.0f,16,FColor::Red,true);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("Line Trace Failed"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("No Controller"));
	}
}


