// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_RLDirector.h"

#include "Base_Character.h"
#include "Combat/CombatStatsComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Gun.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/UnrealType.h"
#include "LearningAgentsGym.h"
#include "LearningAgentsGymsManager.h"
#include "SH_Arena.h"
#include "SH_RLCharacter.h"
#include "SH_ScriptBotController.h"
#include "SH_VisionComponent.h"

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

	FString ArenaOverride;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHArena="), ArenaOverride) && !ArenaOverride.IsEmpty())
	{
		if (!ApplyArenaOverride(ArenaOverride))
		{
			// 다른 맵으로 몇 시간 학습하는 것보다 바로 멈추는 편이 낫다. ★ 정상 종료 경로라 엔진이 종료 코드를 0 으로 덮어쓴다 —
			// 실행기(train.py)는 아래 로그 문구("를 적용하지 못해 종료한다")로 알아챈다. 문구를 바꾸면 train.py 의 ARENA_FAILURE_TEXT 도 바꿀 것.
			UE_LOG(LogTemp, Error, TEXT("[SH_RLDirector] -SHArena=%s 를 적용하지 못해 종료한다."), *ArenaOverride);
			FPlatformMisc::RequestExitWithStatus(/*bForce=*/false, /*ReturnCode=*/3);
			return;
		}
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

bool ASH_RLDirector::ApplyArenaOverride(const FString& Requested)
{
	// 이름만 오면 /Game/RL/ 아래로, 클래스 접미사(.X_C)가 없으면 붙인다.
	FString Path = Requested.TrimStartAndEnd().TrimQuotes();
	if (!Path.StartsWith(TEXT("/")))
	{
		Path = FString(TEXT("/Game/RL/")) + Path;
	}
	if (!Path.Contains(TEXT(".")))
	{
		Path += FString(TEXT(".")) + FPackageName::GetShortName(Path) + TEXT("_C");
	}

	UClass* ArenaClass = LoadClass<ASH_Arena>(nullptr, *Path);
	if (!ArenaClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_RLDirector] -SHArena: '%s' 에서 ASH_Arena 클래스를 찾지 못했다."), *Path);
		return false;
	}

	FArrayProperty* TemplatesProperty =
		FindFProperty<FArrayProperty>(ALearningAgentsGymsManager::StaticClass(), TEXT("GymTemplates"));
	const FStructProperty* InnerProperty = TemplatesProperty ? CastField<FStructProperty>(TemplatesProperty->Inner) : nullptr;
	// ★ FSpawnGymInfo 는 모듈 밖으로 내보내지 않은(API 매크로 없는) USTRUCT 라 StaticStruct() 를 부르면 링크가 안 된다.
	//   멤버도 메모리 배치에 기대지 않고 리플렉션으로 찾는다.
	const FClassProperty* GymClassProperty = InnerProperty
		? FindFProperty<FClassProperty>(InnerProperty->Struct, TEXT("GymClass")) : nullptr;
	if (!GymClassProperty)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] -SHArena: ALearningAgentsGymsManager::GymTemplates 를 찾지 못했다(엔진 버전이 바뀌었나?)."));
		return false;
	}

	FScriptArrayHelper Templates(TemplatesProperty, TemplatesProperty->ContainerPtrToValuePtr<void>(GymsManager.Get()));
	if (Templates.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_RLDirector] -SHArena: GymTemplates 가 비어 있다."));
		return false;
	}

	for (int32 Index = 0; Index < Templates.Num(); ++Index)
	{
		void* Element = GymClassProperty->ContainerPtrToValuePtr<void>(Templates.GetRawPtr(Index));
		const UObject* Previous = GymClassProperty->GetObjectPropertyValue(Element);
		UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] -SHArena: 템플릿 %d  %s → %s"), Index,
			Previous ? *Previous->GetName() : TEXT("(없음)"), *ArenaClass->GetName());
		GymClassProperty->SetObjectPropertyValue(Element, ArenaClass);
	}

	if (const ASH_Arena* Defaults = GetDefault<ASH_Arena>(ArenaClass))
	{
		FString Shape;
		switch (Defaults->BoundaryShape)
		{
		case ESHArenaShape::Rectangle:
			Shape = FString::Printf(TEXT("직사각형 %.0f x %.0f cm"), Defaults->RectHalfExtents.X * 2.0, Defaults->RectHalfExtents.Y * 2.0);
			break;
		case ESHArenaShape::LShape:
			Shape = FString::Printf(TEXT("ㄱ자 다리 %.0f cm, 폭 %.0f cm"), Defaults->LegLength, Defaults->LegHalfWidth * 2.0f);
			break;
		default:
			Shape = FString::Printf(TEXT("팔각형 반경 %.0f cm"), Defaults->ArenaRadius);
			break;
		}
		UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] -SHArena: %s — 팀당 %d명, 경계 %s"),
			*ArenaClass->GetName(), Defaults->UnitsPerTeam, *Shape);
	}
	return true;
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

	if (bReportCombat)
	{
		ReportCombat();
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

void ASH_RLDirector::ReportCombat() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ★ 이 누산기는 GatherAgentReward 가 ConsumeRewardAccumulators() 로 비운다.
	//   학습 없이 돌리면 아무도 비우지 않으므로 여기 값은 **실행 시작부터의 누적**이다.
	//   학습을 붙이면 구간 값이 되므로 해석이 달라진다 — 로그에 그걸 명시한다.
	int32 Fired = 0;
	int32 HitCount = 0;

	int32 AliveBots = 0;
	int32 BotsWithTarget = 0;

	float FovDegrees = -1.0f;
	bool  bLineOfSight = false;
	bool  bFoundBot = false;

	for (TActorIterator<ABase_Character> It(World); It; ++It)
	{
		ABase_Character* Unit = *It;
		if (!Unit || !Unit->CombatStats)
		{
			continue;
		}

		Fired    += Unit->CombatStats->ShotsFiredAccum;
		HitCount += Unit->CombatStats->ShotsHitAccum;

		if (!Unit->bEntityEnabled || !Unit->CombatStats->IsAlive())
		{
			continue;
		}

		if (const ASH_RLCharacter* RLUnit = Cast<ASH_RLCharacter>(Unit))
		{
			if (RLUnit->Vision)
			{
				FovDegrees = RLUnit->Vision->FOVDegrees;
			}
		}

		// 표적 보유 여부는 스크립트 봇만 알 수 있다 — 정책은 표적 개념이 없다.
		if (const ASH_ScriptBotController* Bot = Cast<ASH_ScriptBotController>(Unit->GetController()))
		{
			++AliveBots;
			bFoundBot = true;
			bLineOfSight = Bot->bRequireLineOfSight;
			if (Bot->GetCurrentTarget())
			{
				++BotsWithTarget;
			}
		}
	}

	const float HitRate = (Fired > 0) ? (100.0f * static_cast<float>(HitCount) / static_cast<float>(Fired)) : 0.0f;
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]  누적 사격/명중    : %d / %d  (명중률 %.1f%%)"),
		Fired, HitCount, HitRate);

	if (bFoundBot)
	{
		const float TargetRate = (AliveBots > 0)
			? (100.0f * static_cast<float>(BotsWithTarget) / static_cast<float>(AliveBots)) : 0.0f;
		UE_LOG(LogTemp, Display,
			TEXT("[SH_RLDirector]  표적 보유 봇      : %d / %d  (%.1f%%)   시야 %.0f도 / LoS=%s"),
			BotsWithTarget, AliveBots, TargetRate,
			FovDegrees, bLineOfSight ? TEXT("켬") : TEXT("끔"));

		// 표적이 없는 봇이 많으면 **왜** 없는지가 유일하게 중요한 정보다.
		if (BotsWithTarget < AliveBots)
		{
			ReportVisionFailures();
		}
	}
}

void ASH_RLDirector::ReportVisionFailures() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 살아있는 유닛 전체를 한 번 모아 둔다 (아레나별 최근접 적을 찾기 위해).
	TArray<ABase_Character*> Live;
	for (TActorIterator<ABase_Character> It(World); It; ++It)
	{
		ABase_Character* Unit = *It;
		if (Unit && Unit->bEntityEnabled && Unit->CombatStats && Unit->CombatStats->IsAlive())
		{
			Live.Add(Unit);
		}
	}

	int32 Counts[6] = { 0, 0, 0, 0, 0, 0 };   // ESHVisionResult 순서
	int32 NoCandidate = 0;
	TMap<FName, int32> Blockers;
	float RadiusSum = 0.0f;
	int32 RadiusNum = 0;

	int32 Sweeping = 0;
	int32 DetailShown = 0;
	float LostSum = 0.0f;
	// 표본 3개의 조준 Yaw. 리포트 두 번을 비교하면 선회가 실제로 도는지 알 수 있다.
	float SampleYaw[3] = { 0.0f, 0.0f, 0.0f };
	int32 SampleNum = 0;

	for (ABase_Character* Unit : Live)
	{
		const ASH_ScriptBotController* Bot = Cast<ASH_ScriptBotController>(Unit->GetController());
		if (!Bot || Bot->GetCurrentTarget())
		{
			continue;   // 표적이 있는 봇은 진단 대상이 아니다
		}

		const USH_VisionComponent* Vision = Unit->FindComponentByClass<USH_VisionComponent>();
		if (!Vision)
		{
			continue;
		}

		LostSum += Bot->GetTimeSinceTargetLost();
		if (Bot->IsSweeping())
		{
			++Sweeping;
		}
		if (SampleNum < 3)
		{
			SampleYaw[SampleNum] = Bot->GetControlRotation().Yaw;
			++SampleNum;
		}

		// 소속 아레나 중심으로부터의 거리 — 전진이 실제로 일어났는지 본다.
		if (const ALearningAgentsGymBase* Gym = Unit->CachedGym)
		{
			RadiusSum += FVector::Dist2D(Unit->GetActorLocation(), Gym->GetActorLocation());
			++RadiusNum;
		}

		// 가시성을 **무시하고** 같은 아레나의 최근접 적을 찾는다.
		ABase_Character* Nearest = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		for (ABase_Character* Other : Live)
		{
			if (Other == Unit || Other->CachedGym != Unit->CachedGym)
			{
				continue;
			}
			if (!UCombatStatsComponent::AreHostile(Unit, Other))
			{
				continue;
			}
			const float DistSq = FVector::DistSquared(Unit->GetActorLocation(), Other->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Nearest = Other;
			}
		}

		if (!Nearest)
		{
			++NoCandidate;   // 이 아레나는 한쪽 팀이 전멸했다
			continue;
		}

		FName Blocker;
		const ESHVisionResult Result = Vision->ClassifyVisibility(Nearest, Blocker);
		Counts[static_cast<int32>(Result)]++;
		if (Result == ESHVisionResult::Occluded && Blocker != NAME_None)
		{
			Blockers.FindOrAdd(Blocker)++;
		}

		// 표본 몇 개는 기하를 그대로 찍는다 — 각도가 ±45 안에 들어오는데도
		// OutsideFOV 가 나오면 검사가 틀린 것이고, 각도가 늘 크면 배치 문제다.
		if (SampleNum <= 3 && DetailShown < 3)
		{
			++DetailShown;
			const FVector ToEnemy = Nearest->GetActorLocation() - Unit->GetActorLocation();
			const float EnemyYaw = ToEnemy.Rotation().Yaw;
			const float AimYaw = Bot->GetControlRotation().Yaw;
			UE_LOG(LogTemp, Display,
				TEXT("[SH_RLDirector]    표본 %s: 거리 %.0f / 적방향 %.1f도 / 조준 %.1f도 / ")
				TEXT("차이 %.1f도 / 판정 %d / 가린것 %s"),
				*Unit->GetName(), ToEnemy.Size2D(), EnemyYaw, FRotator::NormalizeAxis(AimYaw),
				FRotator::NormalizeAxis(EnemyYaw - AimYaw),
				static_cast<int32>(Result), *Blocker.ToString());
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[SH_RLDirector]  표적 없는 이유    : 보임 %d / 시야각밖 %d / 가려짐 %d / ")
		TEXT("사거리밖 %d / 미명중 %d / 무효 %d / 적없음 %d"),
		Counts[(int32)ESHVisionResult::Visible],
		Counts[(int32)ESHVisionResult::OutsideFOV],
		Counts[(int32)ESHVisionResult::Occluded],
		Counts[(int32)ESHVisionResult::OutOfRange],
		Counts[(int32)ESHVisionResult::NoHit],
		Counts[(int32)ESHVisionResult::Invalid],
		NoCandidate);

	if (RadiusNum > 0)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SH_RLDirector]  아레나 중심 거리  : 평균 %.0f cm (스폰 반경 1200 — 전진했으면 줄어든다)"),
			RadiusSum / RadiusNum);

		UE_LOG(LogTemp, Display,
			TEXT("[SH_RLDirector]  선회 탐색 중      : %d명 | 표적 상실 경과 평균 %.1f초 | ")
			TEXT("표본 Yaw %.1f / %.1f / %.1f (리포트 간 변하지 않으면 선회가 안 도는 것)"),
			Sweeping, LostSum / RadiusNum,
			SampleYaw[0], SampleYaw[1], SampleYaw[2]);
	}

	// 가린 것이 무엇인지. 엄폐물인지 아군인지에 따라 대응이 완전히 다르다.
	Blockers.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 Shown = 0;
	for (const TPair<FName, int32>& Pair : Blockers)
	{
		UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector]    가린 것: %-28s %d회"),
			*Pair.Key.ToString(), Pair.Value);
		if (++Shown >= 5)
		{
			break;
		}
	}
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
	int32 OverlappingPairs = 0;

	// ★ 예전에는 "중심 사이 거리 > 2 x ArenaRadius" 로 봤다. 원형 아레나에서만 맞는 검사라
	//   길쭉한 복도(Rectangle)는 짧은 축으로 촘촘히 놓여도 정상인데 겹친다고 나온다.
	//   그래서 실제 콜리전 바운드(GetGymExtents) 두 상자가 XY 에서 겹치는지 본다.
	TArray<FBox2D> Bounds;
	Bounds.Reserve(Arenas.Num());
	for (const ASH_Arena* Arena : Arenas)
	{
		FVector Min, Max;
		Arena->GetGymExtents(Min, Max);
		const FVector L = Arena->GetActorLocation();
		Bounds.Add(FBox2D(FVector2D(L.X + Min.X, L.Y + Min.Y), FVector2D(L.X + Max.X, L.Y + Max.Y)));
	}

	for (int32 i = 0; i < Arenas.Num(); ++i)
	{
		if (Arenas[i]->GetActorLocation().SizeSquared() < 1.0f)
		{
			++AtOrigin;
		}
		for (int32 j = i + 1; j < Arenas.Num(); ++j)
		{
			// 맞닿기만 한 것은 겹침이 아니다 — 1 cm 줄여서 본다.
			if (Bounds[i].ExpandBy(-1.0).Intersect(Bounds[j].ExpandBy(-1.0)))
			{
				++OverlappingPairs;
			}
		}
	}

	const FVector2D Size = Bounds[0].GetSize();

	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] --- 아레나 배치 검증 ---"));
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 스폰된 아레나       : %d개"), Arenas.Num());
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 아레나 클래스       : %s (바운드 %.0f x %.0f cm, 팀당 %d명)"),
		*Arenas[0]->GetClass()->GetName(), Size.X, Size.Y, Arenas[0]->UnitsPerTeam);
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 첫 아레나 위치      : %s"), *Arenas[0]->GetActorLocation().ToString());
	UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 마지막 아레나 위치  : %s"), *Arenas.Last()->GetActorLocation().ToString());

	if (AtOrigin > 1)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] ★ 아레나 %d개가 월드 원점에 겹쳐 있다. 좌표 생성이 (0,0,0) 을 반환하고 있다."),
			AtOrigin);
	}

	if (OverlappingPairs > 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_RLDirector] ★ 아레나 %d쌍이 겹친다. GymsManager 의 GymsSpacing 을 올릴 것 ")
			TEXT("(피치 = GymsSpacing + 바닥폭)."), OverlappingPairs);
	}
	else if (Arenas.Num() > 1)
	{
		UE_LOG(LogTemp, Display, TEXT("[SH_RLDirector] 배치 정상 — 겹침 없음."));
	}
}
