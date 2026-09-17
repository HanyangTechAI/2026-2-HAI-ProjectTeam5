// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_RLDirector.h"

#include "Base_Character.h"
#include "Combat/CombatStatsComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Gun.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LearningAgentsGymsManager.h"
#include "SH_Arena.h"

ASH_RLDirector::ASH_RLDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 계획 Part 7 의 목록. 고정 타임스텝에서는 렌더 프레임 하나당 시뮬이 1/60초씩
	// 전진하므로, 렌더링 비용을 줄이는 것이 그대로 학습 속도가 된다.
	TrainingRenderCommands = {
		TEXT("r.Lumen.DiffuseIndirect.Allow 0"),
		TEXT("r.Lumen.Reflections.Allow 0"),
		TEXT("r.Shadow.Virtual.Enable 0"),
		TEXT("r.RayTracing.ForceAllRayTracingEffects 0"),
		TEXT("r.ScreenPercentage 25"),
		TEXT("r.SkeletalMeshLODBias 3"),
		TEXT("r.VolumetricFog 0"),
		TEXT("sg.ShadowQuality 0"),
		TEXT("sg.EffectsQuality 0"),
		TEXT("sg.PostProcessQuality 0"),
	};
}

void ASH_RLDirector::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 지정되지 않았으면 레벨에서 찾는다.
	if (!GymsManager)
	{
		for (TActorIterator<ALearningAgentsGymsManager> It(World); It; ++It)
		{
			GymsManager = *It;
			break;
		}
	}

	if (!GymsManager)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] 레벨에 ALearningAgentsGymsManager 가 없다. 아레나가 하나도 스폰되지 않는다."));
		return;
	}

	const int32 ExpectedCount = GymsManager->GetGymsCount();
	if (ExpectedCount <= 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] GymsManager 의 GymTemplates 가 비어 있다 (예상 아레나 수 0). ")
			TEXT("디테일 패널에서 GymClass = SH_Arena, SpawnCount = 16 을 지정할 것."));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] GymsManager->Start() 호출 — 예상 아레나 %d개"), ExpectedCount);
	GymsManager->Start();

	if (bVerifyArenaLayout)
	{
		VerifyArenaLayout();
	}

	if (bApplyTrainingRenderSettings)
	{
		ApplyTrainingRenderSettings();
	}

	LastReportRealTime = FPlatformTime::Seconds();
	LastReportSimTime = World->GetTimeSeconds();
	FramesSinceReport = 0;
}

void ASH_RLDirector::ApplyTrainingRenderSettings()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (const FString& Command : TrainingRenderCommands)
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);
	}

	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 학습용 렌더 설정 %d개 적용."),
		TrainingRenderCommands.Num());
}

void ASH_RLDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bReportThroughput)
	{
		return;
	}

	++FramesSinceReport;

	const double RealNow = FPlatformTime::Seconds();
	const double RealElapsed = RealNow - LastReportRealTime;
	if (RealElapsed < ThroughputReportInterval)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float SimNow = World ? World->GetTimeSeconds() : LastReportSimTime;

	ReportThroughput(RealElapsed, SimNow - LastReportSimTime, FramesSinceReport);

	LastReportRealTime = RealNow;
	LastReportSimTime = SimNow;
	FramesSinceReport = 0;
}

void ASH_RLDirector::ReportThroughput(double RealElapsed, float SimElapsed, int32 Frames) const
{
	UWorld* World = GetWorld();
	if (!World || RealElapsed <= 0.0)
	{
		return;
	}

	int32 ArenaCount = 0;
	for (TActorIterator<ASH_Arena> It(World); It; ++It)
	{
		++ArenaCount;
	}

	int32 UnitTotal = 0, UnitEnabled = 0, UnitAlive = 0;
	for (TActorIterator<ABase_Character> It(World); It; ++It)
	{
		++UnitTotal;
		if (It->bEntityEnabled)
		{
			++UnitEnabled;
		}
		if (It->CombatStats && It->CombatStats->IsAlive())
		{
			++UnitAlive;
		}
	}

	// ★ 총알 누출 누적. 32개 총을 일일이 열어보지 않아도 되도록 여기서 합산한다.
	int32 CrossGymHits = 0;
	for (TActorIterator<AGun> It(World); It; ++It)
	{
		CrossGymHits += It->CrossGymHitCount;
	}

	const float Fps = static_cast<float>(Frames) / static_cast<float>(RealElapsed);

	// 시뮬 시간이 실제 시간보다 빠르게 흐르는 배수. 고정 타임스텝 + 고속 렌더의 효과.
	const float Compression = static_cast<float>(SimElapsed / RealElapsed);

	// 수집 속도는 "살아있는 에이전트 수 x 결정 주기 x 시간 압축" 이다.
	const float StepsPerSecond = static_cast<float>(UnitEnabled) * DecisionRateHz * Compression;
	const float SecondsPerIteration =
		(StepsPerSecond > KINDA_SMALL_NUMBER) ? (static_cast<float>(StepsPerIteration) / StepsPerSecond) : 0.0f;

	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] ===== 처리량 (%.1f초) ====="), RealElapsed);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  fps               : %.1f"), Fps);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  시뮬 압축 배수    : %.2fx"), Compression);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  아레나            : %d"), ArenaCount);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  유닛 (활성/생존/전체) : %d / %d / %d"),
		UnitEnabled, UnitAlive, UnitTotal);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  수집 속도         : %.0f step/s  (%d x %.0fHz x %.2f)"),
		StepsPerSecond, UnitEnabled, DecisionRateHz, Compression);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  %d스텝 소요       : %.1f초"),
		StepsPerIteration, SecondsPerIteration);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  렌더 오버라이드   : %s"),
		bApplyTrainingRenderSettings ? TEXT("적용") : TEXT("미적용 — 이 수치는 학습 조건이 아니다"));

	// --- Gate 2 자동 판정 ----------------------------------------------------

	if (CrossGymHits > 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector]  ★ CROSS-GYM 누적 : %d — 총알이 옆 아레나로 넘어갔다. ")
			TEXT("벽의 Bullet 채널 응답과 GymsSpacing 을 확인할 것."), CrossGymHits);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  CROSS-GYM 누적    : 0"));
	}

	if (PreviousUnitActorCount >= 0 && UnitTotal > PreviousUnitActorCount)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector]  ★ 유닛 액터 수가 %d -> %d 로 늘었다. 풀링이 깨졌다 — ")
			TEXT("리셋이 액터를 재사용하지 않고 새로 스폰하고 있다. ")
			TEXT("EpisodeEntitySpawnCountMin != Max 인지 확인할 것."),
			PreviousUnitActorCount, UnitTotal);
	}
	PreviousUnitActorCount = UnitTotal;
}

void ASH_RLDirector::VerifyArenaLayout() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<const ASH_Arena*> Arenas;
	for (TActorIterator<ASH_Arena> It(World); It; ++It)
	{
		Arenas.Add(*It);
	}

	if (Arenas.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] 스폰된 ASH_Arena 가 0개다. GymTemplates 의 GymClass 를 확인할 것."));
		return;
	}

	// 월드 원점에 겹쳐 있는지 — ProjectPointToGym 의 NavMesh 경로에 걸리면
	// 모든 좌표가 (0,0,0) 이 되므로 가장 먼저 확인해야 할 증상이다.
	int32 AtOrigin = 0;
	float MinPairDistance = TNumericLimits<float>::Max();
	float ArenaRadius = 0.0f;

	for (int32 i = 0; i < Arenas.Num(); ++i)
	{
		const FVector Li = Arenas[i]->GetActorLocation();
		ArenaRadius = Arenas[i]->ArenaRadius;

		if (Li.SizeSquared() < 1.0f)
		{
			++AtOrigin;
		}

		for (int32 j = i + 1; j < Arenas.Num(); ++j)
		{
			MinPairDistance = FMath::Min(MinPairDistance, FVector::Dist2D(Li, Arenas[j]->GetActorLocation()));
		}
	}

	const float RequiredPitch = 2.0f * ArenaRadius;

	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] --- 아레나 배치 검증 ---"));
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 스폰된 아레나       : %d개"), Arenas.Num());
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 최소 중심간 거리    : %.0f cm (겹치지 않으려면 > %.0f)"),
		MinPairDistance, RequiredPitch);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 첫 아레나 위치      : %s"), *Arenas[0]->GetActorLocation().ToString());
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 마지막 아레나 위치  : %s"), *Arenas.Last()->GetActorLocation().ToString());

	if (AtOrigin > 1)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] ★ 아레나 %d개가 월드 원점에 겹쳐 있다. 좌표 생성이 (0,0,0) 을 반환하고 있다."),
			AtOrigin);
	}

	if (Arenas.Num() > 1 && MinPairDistance < RequiredPitch)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] ★ 아레나가 겹친다. GymsManager 의 GymsSpacing 을 올릴 것 ")
			TEXT("(피치 = GymsSpacing + 바닥폭)."));
	}
	else if (Arenas.Num() > 1)
	{
		UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 배치 정상 — 겹침 없음."));
	}
}
