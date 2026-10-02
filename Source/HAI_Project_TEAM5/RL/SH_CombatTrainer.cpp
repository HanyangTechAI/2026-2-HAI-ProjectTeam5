// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_CombatTrainer.h"

#include "Combat/CombatStatsComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SH_Arena.h"
#include "SH_RLBotController.h"
#include "SH_RLCharacter.h"
#include "SH_ShooterInteractor.h"
#include "SH_ShooterTrainingEnv.h"
#include "SH_VisionComponent.h"

ASH_CombatTrainer::ASH_CombatTrainer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("LearningAgentsManager"));

	// ★ 기본값이 learning_core.train_behavior_cloning 이다. 반드시 바꾼다.
	ProcessSettings.TrainerFileName = TEXT("learning_core.train_ppo");
	ProcessSettings.TaskName        = TEXT("SHCombat");

	// 기본 10초는 파이썬 첫 기동에 모자라다.
	SharedMemorySettings.Timeout = 60.0f;

	// 에피소드 256스텝 = 20Hz 에서 12.8초. 아레나를 가로지르고(7초) 교전할 시간이 필요하다.
	TrainerSettings.MaxEpisodeStepNum = 256;

	// ★ 리플레이 버퍼 상한. 에이전트 수 x 게더당 결정 수 보다 커야 한다 —
	//   160 에이전트면 게더당 약 102 결정(5.1초 시뮬)에 해당한다.
	TrainerSettings.MaximumRecordedStepsPerIteration    = 16384;
	TrainerSettings.MaximumRecordedEpisodesPerIteration = 4000;

	// 기본 0.0 은 나쁜 기본값이다 — 엔트로피가 초반에 붕괴하면 탐색이 멈춘다.
	TrainingSettings.ActionEntropyWeight = 0.005f;

	// ★ 기본값 false. 안 켜면 학습된 정책이 디스크에 남지 않는다 —
	//   2026-10-01 까지의 1~5회차 결과가 전부 이렇게 사라졌다.
	//   IterationsPerSnapshot(기본 1000) 마다 Intermediate/LearningCore/<Task>/ 아래에 기록된다.
	TrainingSettings.bSaveSnapshots = true;

	// ★ 그래디언트 노름 클리핑. 엔진 기본값은 꺼짐이다.
	//   라운드 시스템(에피소드 약 200스텝, 결과 ±2.0, 시계 비용)으로 반환값의 크기와 분산이 커진 뒤
	//   "MaxIdx != INDEX_NONE" 크래시(로짓이 NaN/−∞)가 3~13분 간격으로 났다. 관측·메모리·인코딩은
	//   직전 스텝까지 유한했으므로(ScanForNonFinite) 신경망 출력 자체를 의심한다.
	//   PPO 구현 대부분이 쓰는 0.5 로 묶는다. -SHGradClip=0 으로 끌 수 있다(대조 실험용).
	TrainingSettings.bUseGradNormMaxClipping = true;
	TrainingSettings.GradNormMax = 0.5f;
	TrainingSettings.bUseTensorboard     = true;
	TrainingSettings.Device              = ELearningAgentsTrainingDevice::GPU;
}

void ASH_CombatTrainer::BeginPlay()
{
	Super::BeginPlay();

	StartRealTime = FPlatformTime::Seconds();
	LastReportRealTime = StartRealTime;

	// --- 커맨드라인 오버라이드 ------------------------------------------------
	// ★ 실행 길이를 바꿀 때마다 레벨 에셋을 커멘드릿으로 다시 저장하면
	//   (1) 에디터를 닫아야 하고 (2) .umap 이 계속 더러워진다.
	//   헤드리스 실행에서 길이만 바꾸는 일이 잦으므로 커맨드라인으로 받는다.
	//
	//   UnrealEditor-Cmd.exe <uproject> /Game/RL/Lvl_SHTrain -game -SHTrainSeconds=5400
	float OverrideSeconds = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHTrainSeconds="), OverrideSeconds)
		&& OverrideSeconds > 0.0f)
	{
		MaxWallClockSeconds = OverrideSeconds;
	}

	float OverrideReportInterval = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHReportInterval="), OverrideReportInterval)
		&& OverrideReportInterval >= 1.0f)
	{
		ReportInterval = OverrideReportInterval;
	}

	// 대치 실험용 스위치. 0 = 시간 초과는 무승부 + 축소 구역 끔(대조군), 1 = 둘 다 켬.
	int32 OverrideRoundRules = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHRoundRules="), OverrideRoundRules))
	{
		RoundRulesOverride = OverrideRoundRules;
	}
	float OverrideRoundTime = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHRoundTime="), OverrideRoundTime) && OverrideRoundTime >= 1.0f)
	{
		RoundTimeOverride = OverrideRoundTime;
	}
	float OverrideTeamSpirit = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHTeamSpirit="), OverrideTeamSpirit) && OverrideTeamSpirit >= 0.0f)
	{
		TeamSpiritOverride = FMath::Clamp(OverrideTeamSpirit, 0.0f, 1.0f);
	}
	float OverrideOutcome = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHOutcome="), OverrideOutcome) && OverrideOutcome >= 0.0f)
	{
		OutcomeOverride = OverrideOutcome;
	}
	int32 OverrideRoundRamp = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHRoundRamp="), OverrideRoundRamp) && OverrideRoundRamp >= 0)
	{
		RoundRampOverride = OverrideRoundRamp;
	}
	int32 OverrideSelfPlay = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHSelfPlay="), OverrideSelfPlay) && (OverrideSelfPlay == 0 || OverrideSelfPlay == 1))
	{
		bSelfPlay = (OverrideSelfPlay == 1);
	}
	int32 OverrideBuffer = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHBuffer="), OverrideBuffer) && OverrideBuffer >= 1024)
	{
		BufferOverride = OverrideBuffer;
	}
	int32 OverrideGradClip = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHGradClip="), OverrideGradClip) && (OverrideGradClip == 0 || OverrideGradClip == 1))
	{
		TrainingSettings.bUseGradNormMaxClipping = (OverrideGradClip == 1);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 실행 한도 %.0f초, 리포트 주기 %.0f초."),
		MaxWallClockSeconds, ReportInterval);

	// ★ 여기서 조립하지 않는다. 아레나가 아직 없을 수 있다 — 헤더 주석 참조.
	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] BeginPlay. 조립은 첫 Tick 에서 한다 (아레나 스폰 이후)."));
}

void ASH_CombatTrainer::CollectAgentUnits(TArray<ASH_RLCharacter*>& OutUnits) const
{
	OutUnits.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ASH_RLCharacter> It(World); It; ++It)
	{
		ASH_RLCharacter* Unit = *It;
		if (!Unit)
		{
			continue;
		}

		// ★ 판별 기준은 팀이 아니라 컨트롤러다. 커리큘럼을 BP 설정만으로 바꿀 수 있다.
		if (!Cast<ASH_RLBotController>(Unit->GetController()))
		{
			continue;
		}

		OutUnits.Add(Unit);
	}
}

bool ASH_CombatTrainer::ValidateUnitSchema(const TArray<ASH_RLCharacter*>& Units) const
{
	bool bOk = true;

	for (const ASH_RLCharacter* Unit : Units)
	{
		const USH_VisionComponent* Vision = Unit ? Unit->Vision.Get() : nullptr;
		if (!Vision)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_CombatTrainer] %s 에 USH_VisionComponent 가 없다."),
				Unit ? *Unit->GetName() : TEXT("(null)"));
			bOk = false;
			continue;
		}

		// ★ 관측 스키마의 StaticArray 크기는 SHRL::RayCount 로 고정이다.
		//   유닛의 RayCount 가 다르면 부채꼴 길이가 어긋나 벡터 크기가 깨진다.
		if (Vision->RayCount != SHRL::RayCount)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_CombatTrainer] %s: Vision->RayCount(%d) != 스키마 RayCount(%d). ")
				TEXT("BP 유닛의 Vision 설정을 고칠 것 — 스키마는 코드 상수다."),
				*Unit->GetName(), Vision->RayCount, SHRL::RayCount);
			bOk = false;
		}

		// ★ 피격 기억 지평은 Units[0] 것만 인터랙터로 복사된다 (Assemble 참조).
		//   유닛마다 다르면 그 유닛의 클램프 상한과 정규화 Scale 이 어긋나서
		//   TimeSinceHit 이 0~1 을 벗어난다. 에러는 나지 않는다.
		if (!FMath::IsNearlyEqual(Unit->HitMemoryHorizonSeconds, Units[0]->HitMemoryHorizonSeconds))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_CombatTrainer] %s: HitMemoryHorizonSeconds(%.2f) 가 다른 유닛(%.2f)과 다르다. ")
				TEXT("정규화 Scale 은 하나뿐이므로 모든 유닛이 같아야 한다."),
				*Unit->GetName(), Unit->HitMemoryHorizonSeconds, Units[0]->HitMemoryHorizonSeconds);
			bOk = false;
		}
	}

	return bOk;
}

void ASH_CombatTrainer::Assemble()
{
	bAssembleAttempted = true;

	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] Manager 가 없다."));
		return;
	}

	// --- 0a. self-play: 아레나 유닛의 컨트롤러를 RL 컨트롤러로 바꿔 끼운다 --------
	if (bSelfPlay)
	{
		int32 Swapped = 0;
		for (TActorIterator<ASH_RLCharacter> It(GetWorld()); It; ++It)
		{
			ASH_RLCharacter* Unit = *It;
			if (!Unit || !Unit->CachedGym)
			{
				continue;   // 아레나 소속 유닛만
			}

			AController* OldController = Unit->GetController();
			if (OldController && OldController->IsA<ASH_RLBotController>())
			{
				continue;   // 이미 RL
			}
			if (OldController)
			{
				OldController->UnPossess();
				OldController->Destroy();
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ASH_RLBotController* NewController = GetWorld()->SpawnActor<ASH_RLBotController>(
				ASH_RLBotController::StaticClass(), Unit->GetActorTransform(), SpawnParams);
			if (NewController)
			{
				// OnPossess 가 회전 모드를 설정한다. 컨트롤 회전은 다음 아레나 리셋의
				// ApplySpawnSlots 가 슬롯 방향으로 맞춘다(bResetAgentsOnBegin = true).
				NewController->Possess(Unit);
				++Swapped;
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] self-play — 유닛 %d개의 컨트롤러를 SH_RLBotController 로 교체했다."), Swapped);
	}

	// 게더당 기록 스텝. self-play 는 에이전트가 두 배라 버퍼도 키운다.
	if (BufferOverride >= 1024)
	{
		TrainerSettings.MaximumRecordedStepsPerIteration = BufferOverride;
	}
	else if (bSelfPlay)
	{
		TrainerSettings.MaximumRecordedStepsPerIteration = SelfPlayRecordedSteps;
	}

	// --- 0. 에이전트가 될 유닛을 먼저 찾는다 -----------------------------------
	TArray<ASH_RLCharacter*> Units;
	CollectAgentUnits(Units);

	if (Units.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_CombatTrainer] 정책이 조종할 유닛이 0개다. 확인할 것: ")
			TEXT("(1) 레벨에 ASH_RLDirector 가 있고 GymsManager->Start() 가 불렸는가, ")
			TEXT("(2) BP_SHArena 의 GymTemplates 가 설정됐는가, ")
			TEXT("(3) 유닛 BP 의 AIControllerClass 가 SH_RLBotController 인가."));
		return;
	}

	if (!ValidateUnitSchema(Units))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] 유닛 스키마 검증 실패 — 조립을 중단한다."));
		return;
	}

	if (Units.Num() > MaxAgentNum)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_CombatTrainer] 유닛 %d개가 MaxAgentNum(%d) 을 넘는다. ")
			TEXT("넘치는 유닛은 등록되지 않고 정책이 영원히 무시한다 — MaxAgentNum 을 올릴 것."),
			Units.Num(), MaxAgentNum);
	}

	// --- 1. 리스너를 만들기 전에 최대 에이전트 수를 정한다 ---------------------
	Manager->SetMaxAgentNum(FMath::Max(MaxAgentNum, Units.Num()));

	ULearningAgentsManager* ManagerPtr = Manager;

	// --- 2. Interactor -------------------------------------------------------
	ULearningAgentsInteractor* InteractorPtr = ULearningAgentsInteractor::MakeInteractor(
		ManagerPtr, USH_ShooterInteractor::StaticClass(), TEXT("ShooterInteractor"));

	Interactor = Cast<USH_ShooterInteractor>(InteractorPtr);
	if (!Interactor)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] MakeInteractor 실패."));
		return;
	}

	// 관측 정규화 Scale 을 유닛의 기억 지평과 맞춘다. 어긋나면 TimeSince 가 0~1 을 벗어난다.
	if (const USH_VisionComponent* Vision = Units[0]->Vision.Get())
	{
		Interactor->MemoryHorizonSeconds = Vision->MemoryHorizonSeconds;
	}

	// 피격 기억도 같은 이유로 맞춘다. 이쪽은 컴포넌트가 아니라 폰이 들고 있다.
	Interactor->HitMemoryHorizonSeconds = Units[0]->HitMemoryHorizonSeconds;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 스키마 크기 — 관측 %d (인코딩 %d) / 행동 %d (분포 %d, 모디파이어 %d)"),
		Interactor->GetObservationVectorSize(),
		Interactor->GetObservationEncodedVectorSize(),
		Interactor->GetActionVectorSize(),
		Interactor->GetActionDistributionVectorSize(),
		Interactor->GetActionModifierVectorSize());

	// --- 3. Policy / 4. Critic ----------------------------------------------
	ULearningAgentsPolicy* PolicyPtr = ULearningAgentsPolicy::MakePolicy(
		ManagerPtr, InteractorPtr, ULearningAgentsPolicy::StaticClass(), TEXT("ShooterPolicy"),
		/*Encoder*/ nullptr, /*Policy*/ nullptr, /*Decoder*/ nullptr,
		/*bReinitEncoder*/ true, /*bReinitPolicy*/ true, /*bReinitDecoder*/ true,
		FLearningAgentsPolicySettings(), Seed);
	Policy = PolicyPtr;

	ULearningAgentsCritic* CriticPtr = ULearningAgentsCritic::MakeCritic(
		ManagerPtr, InteractorPtr, PolicyPtr, ULearningAgentsCritic::StaticClass(), TEXT("ShooterCritic"),
		/*Network*/ nullptr, /*bReinit*/ true, FLearningAgentsCriticSettings(), Seed);
	Critic = CriticPtr;

	// --- 5. TrainingEnvironment ---------------------------------------------
	ULearningAgentsTrainingEnvironment* EnvPtr = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(
		ManagerPtr, USH_ShooterTrainingEnv::StaticClass(), TEXT("ShooterTrainingEnv"));

	TrainingEnvironment = Cast<USH_ShooterTrainingEnv>(EnvPtr);
	if (!TrainingEnvironment)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] MakeTrainingEnvironment 실패."));
		return;
	}

	if (TeamSpiritOverride >= 0.0f)
	{
		TrainingEnvironment->TeamSpirit = TeamSpiritOverride;
	}
	if (OutcomeOverride >= 0.0f)
	{
		TrainingEnvironment->OutcomeReward = OutcomeOverride;
	}
	if (RoundRampOverride >= 0)
	{
		TrainingEnvironment->RoundRewardRampSteps = RoundRampOverride;
	}

	// ★ 시계 비용의 상쇄는 학습에 실제로 쓰는 γ 와 같을 때만 정확하다.
	TrainingEnvironment->ClockDiscount = TrainingSettings.DiscountFactor;

	// --- 아레나: 라운드 지표 수집 대상 + 커맨드라인 오버라이드 ----------------
	Arenas.Reset();
	AgentTeamMask = 0;
	for (const ASH_RLCharacter* Unit : Units)
	{
		if (Unit && Unit->CombatStats)
		{
			AgentTeamMask |= 1 << static_cast<int32>(Unit->CombatStats->TeamId);
		}
	}
	for (TActorIterator<ASH_Arena> It(GetWorld()); It; ++It)
	{
		ASH_Arena* Arena = *It;
		if (!Arena || Arena->GetTeamSize(0) + Arena->GetTeamSize(1) == 0)
		{
			continue;   // 레벨에 편집용으로 놓은 빈 아레나
		}
		if (RoundRulesOverride == 0 || RoundRulesOverride == 1)
		{
			Arena->bTimeoutIsLossForBoth = (RoundRulesOverride == 1);
			Arena->bShrinkZone = (RoundRulesOverride == 1);
		}
		if (RoundTimeOverride >= 1.0f)
		{
			Arena->RoundTimeLimitSeconds = RoundTimeOverride;
		}
		Arenas.Add(Arena);
	}

	// 대조군(-SHRoundRules=0)에는 대치 방지 장치가 하나도 없어야 한다 — 시계 비용도 끈다.
	if (RoundRulesOverride == 0 || RoundRulesOverride == 1)
	{
		TrainingEnvironment->bRoundClockCost = (RoundRulesOverride == 1);
	}

	if (const ASH_Arena* First = Arenas.Num() > 0 ? Arenas[0].Get() : nullptr)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] 라운드 규칙 — 아레나 %d개, 상한 %.0f초, 시간 초과 = %s, 축소 구역 %s ")
			TEXT("(%.0f초부터 %.0f초 동안 %.0f → %.0f cm, 초당 %.0f), 스폰 지터 ±%.0f° / ±%.0f cm"),
			Arenas.Num(), First->RoundTimeLimitSeconds,
			First->bTimeoutIsLossForBoth ? TEXT("양쪽 패배") : TEXT("무승부"),
			First->bShrinkZone ? TEXT("켬") : TEXT("끔"),
			First->ZoneStartSeconds, First->ZoneShrinkSeconds, First->ArenaRadius, First->ZoneFinalRadius,
			First->ZoneDamagePerSecond, First->SpawnAngleJitterDegrees, First->SpawnRadiusJitter);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 보상 — 준/받은 피해 %.2f/%.2f, 킬/사망 %.2f/%.2f, 빗맞힘 %.3f, 결과 ±%.1f, 팀 스피릿 %.2f ")
		TEXT("(램프 %d), 시계 비용 %s (γ %.3f → 최대 %.4f/스텝), 갱신 시 리셋 %s, 스냅샷 저장 %s, 그래디언트 클리핑 %s"),
		TrainingEnvironment->DamageDealtWeight, TrainingEnvironment->DamageTakenWeight,
		TrainingEnvironment->KillBonus, TrainingEnvironment->DeathPenalty, TrainingEnvironment->MissWeight,
		TrainingEnvironment->OutcomeReward, TrainingEnvironment->TeamSpirit,
		TrainingEnvironment->RoundRewardRampSteps,
		TrainingEnvironment->bRoundClockCost ? TEXT("켬") : TEXT("끔"),
		TrainingEnvironment->ClockDiscount,
		TrainingEnvironment->bRoundClockCost
			? TrainingEnvironment->OutcomeReward * (1.0f - TrainingEnvironment->ClockDiscount) : 0.0f,
		bResetAgentsOnUpdate ? TEXT("켬") : TEXT("끔"),
		TrainingSettings.bSaveSnapshots ? TEXT("켬") : TEXT("끔"),
		TrainingSettings.bUseGradNormMaxClipping ? TEXT("켬") : TEXT("끔"));

	// --- 6. 파이썬 트레이너 프로세스 -----------------------------------------
	UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] 파이썬 트레이너 기동 — 모듈 '%s', 타임아웃 %.0f초"),
		*ProcessSettings.TrainerFileName, SharedMemorySettings.Timeout);

	Communicator = ULearningAgentsCommunicatorLibrary::MakeSharedMemoryTrainingProcess(
		ProcessSettings, SharedMemorySettings);

	if (!Communicator.Trainer.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_CombatTrainer] 트레이너 프로세스 기동 실패. Intermediate/PipInstall 이 ")
			TEXT("설치됐는지, 경로에 한글/공백이 없는지 확인할 것."));
		return;
	}

	// --- 7. PPOTrainer -------------------------------------------------------
	Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(
		ManagerPtr, InteractorPtr, EnvPtr, PolicyPtr, CriticPtr,
		Communicator, ULearningAgentsPPOTrainer::StaticClass(), TEXT("ShooterPPOTrainer"),
		TrainerSettings);

	if (!Trainer)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] MakePPOTrainer 실패."));
		return;
	}

	// --- 8. ★ 에이전트 등록은 맨 마지막 --------------------------------------
	// 리스너가 생기기 전에 추가된 에이전트는 그 리스너에게 영원히 보이지 않고, 에러도 없다.
	Agents.Reset(Units.Num());
	for (ASH_RLCharacter* Unit : Units)
	{
		// ★ 폰을 등록한다. 풀이 재사용하는 것이 폰이고(DisableEntity 가 Destroy 하지 않는다),
		//   컨트롤러는 원칙적으로 교체될 수 있다.
		Manager->AddAgent(Unit);
		Agents.Add(Unit);
	}

	bAssembled = true;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 조립 완료 — 에이전트 %d, 결정 %.0fHz, 에피소드 %d스텝(%.1f초), ")
		TEXT("게더당 %d스텝, 디바이스 %s"),
		Agents.Num(), DecisionRateHz,
		TrainerSettings.MaxEpisodeStepNum,
		TrainerSettings.MaxEpisodeStepNum / FMath::Max(1.0f, DecisionRateHz),
		TrainerSettings.MaximumRecordedStepsPerIteration,
		TrainingSettings.Device == ELearningAgentsTrainingDevice::GPU ? TEXT("GPU") : TEXT("CPU"));
}

void ASH_CombatTrainer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 첫 Tick 에서 한 번만 조립한다. 실패했으면 매 프레임 재시도하지 않는다 —
	// 같은 에러가 로그를 도배하면 진짜 원인이 묻힌다.
	if (!bAssembled)
	{
		if (!bAssembleAttempted)
		{
			Assemble();
		}
		return;
	}

	if (!Trainer)
	{
		return;
	}

	const double RealNow = FPlatformTime::Seconds();

	// ★★ IsTraining() 으로 **먼저** 걸러서는 안 된다.
	//    RunTraining 이 첫 호출에서 BeginTraining 을 하므로, 시작 전에는 IsTraining() 이
	//    false 다. 앞에서 걸러 버리면 학습이 영원히 시작되지 않고 에러도 나지 않는다 —
	//    조립 로그까지는 정상으로 보이고 그 뒤로 아무 일도 일어나지 않는다.
	//    (SH_HelloWorldTrainer.cpp:292 도 무조건 부르고, IsTraining 은 종료 판정에만 쓴다.)
	if (bTrainingStarted && !Trainer->IsTraining())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SH_CombatTrainer] 학습이 종료됐다 (파이썬 프로세스 종료 또는 EndTraining). ")
			TEXT("결정 %d회 수행."), DecisionCount);
		bAssembled = false;
		return;
	}

	// --- 벽시계 상한 --------------------------------------------------------
	if (MaxWallClockSeconds > 0.0f && (RealNow - StartRealTime) >= MaxWallClockSeconds)
	{
		Report(RealNow - LastReportRealTime);
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] MaxWallClockSeconds(%.0f초) 도달 — 학습을 종료한다."),
			MaxWallClockSeconds);

		Trainer->EndTraining();
		bAssembled = false;

		if (!GIsEditor)
		{
			FPlatformMisc::RequestExit(false);
		}
		return;
	}

	// --- 결정 주기 ----------------------------------------------------------
	const float DecisionPeriod = 1.0f / FMath::Max(1.0f, DecisionRateHz);
	DecisionAccumulator += DeltaSeconds;
	if (DecisionAccumulator < DecisionPeriod)
	{
		return;
	}

	// ★ 0 으로 리셋하지 않고 주기만큼 뺀다. 프레임 지터가 있어도 장기 평균 주기가 유지된다.
	DecisionAccumulator -= DecisionPeriod;
	++DecisionCount;

	// --- 지표 표본 (RunTraining 보다 먼저) -----------------------------------
	// ★ 순서가 중요하다. RunTraining 안의 GatherAgentReward 가
	//   ConsumeRewardAccumulators() 로 사격 누산기를 비우므로, 그 전에 읽어야 한다.
	{
		int32 Alive = 0;
		int32 Visible = 0;
		for (const ASH_RLCharacter* Unit : Agents)
		{
			const UCombatStatsComponent* Stats = Unit ? Unit->CombatStats.Get() : nullptr;
			if (!Stats)
			{
				continue;
			}

			ShotsFiredTotal += Stats->ShotsFiredAccum;
			ShotsHitTotal   += Stats->ShotsHitAccum;

			if (Stats->IsAlive() && Unit->bEntityEnabled)
			{
				++Alive;
				if (const USH_VisionComponent* Vision = Unit->Vision.Get())
				{
					if (Vision->IsHostileVisible())
					{
						++Visible;
					}
				}

				// 최근접 적 거리 — 아레나의 유닛 캐시만 훑는다(적 10명).
				if (const ASH_Arena* Arena = Cast<ASH_Arena>(Unit->CachedGym.Get()))
				{
					const int32 Other = 1 - static_cast<int32>(Stats->TeamId);
					const FVector MyLocation = Unit->GetActorLocation();
					float BestSq = TNumericLimits<float>::Max();
					for (const TWeakObjectPtr<ABase_Character>& Weak : Arena->GetTeamUnits(Other))
					{
						const ABase_Character* Enemy = Weak.Get();
						if (Enemy && Enemy->bEntityEnabled && Enemy->CombatStats && Enemy->CombatStats->IsAlive())
						{
							BestSq = FMath::Min(BestSq, FVector::DistSquaredXY(MyLocation, Enemy->GetActorLocation()));
						}
					}
					if (BestSq < TNumericLimits<float>::Max())
					{
						NearestEnemyDistanceSum += FMath::Sqrt(BestSq);
						++NearestEnemySamples;
					}
				}
			}
		}

		const int32 Total = FMath::Max(1, Agents.Num());
		AliveAccumulator   += static_cast<double>(Alive)   / Total;
		VisibleAccumulator += static_cast<double>(Visible) / Total;
		++MetricSampleCount;
	}

	// --- 학습 한 스텝 --------------------------------------------------------
	// ★ 이 한 줄이 전부다. RunTraining 이 내부에서 GatherCompletions / GatherRewards /
	//   ProcessExperience / RunInference(= GatherObservations + MakeActionModifiers +
	//   EncodeObservations + EvaluatePolicy + DecodeAndSampleActions + PerformActions)
	//   를 전부 부른다. 바깥에서 또 부르면 스텝이 이중 수집된다.
	Trainer->RunTraining(TrainingSettings, GameSettings, /*bResetAgentsOnBegin=*/true, bResetAgentsOnUpdate);
	bTrainingStarted = true;

	ScanForNonFinite();

	// --- 리포트 -------------------------------------------------------------
	if ((RealNow - LastReportRealTime) >= ReportInterval)
	{
		Report(RealNow - LastReportRealTime);
		LastReportRealTime = RealNow;
	}
}

void ASH_CombatTrainer::ScanForNonFinite()
{
	if (!Policy || !Interactor || NonFiniteReports >= 40)
	{
		return;
	}

	if (AgentIds.Num() == 0 && Manager)
	{
		TArray<UObject*> Objects;
		Manager->GetAllAgents(Objects, AgentIds, ASH_RLCharacter::StaticClass());
	}

	const TLearningArrayView<2, const float> Encoded = Policy->GetObservationVectorsEncoded();
	const TLearningArrayView<2, const float> Memory = Policy->GetMemoryState();
	const TLearningArrayView<2, const float> Observed = Interactor->GetObservationVectorsArrayView();

	auto Check = [this](const TCHAR* What, const int32 AgentId, const TLearningArrayView<1, const float> Row) -> bool
	{
		for (int32 Index = 0; Index < Row.Num(); ++Index)
		{
			const float Value = Row[Index];
			if (!FMath::IsFinite(Value) || FMath::Abs(Value) > 1.0e6f)
			{
				const ASH_RLCharacter* Unit = Cast<ASH_RLCharacter>(Manager->GetAgent(AgentId));
				const bool bAlive = Unit && Unit->bEntityEnabled && Unit->CombatStats && Unit->CombatStats->IsAlive();
				const ASH_Arena* Arena = Unit ? Cast<ASH_Arena>(Unit->CachedGym.Get()) : nullptr;
				UE_LOG(LogTemp, Error,
					TEXT("[SH_CombatTrainer] 비정상 값 — %s[%d] = %g | 에이전트 %d (%s, 팀 %d, %s, 라운드 %.1f초%s) | 결정 %d"),
					What, Index, Value, AgentId,
					Unit ? *Unit->GetName() : TEXT("?"),
					(Unit && Unit->CombatStats) ? static_cast<int32>(Unit->CombatStats->TeamId) : -1,
					bAlive ? TEXT("생존") : TEXT("사망"),
					Arena ? Arena->GetRoundElapsed() : -1.0f,
					(Arena && Arena->IsRoundOver()) ? TEXT(", 종료됨") : TEXT(""),
					DecisionCount);
				++NonFiniteReports;
				return true;
			}
		}
		return false;
	};

	for (const int32 AgentId : AgentIds)
	{
		if (NonFiniteReports >= 40)
		{
			break;
		}
		if (AgentId < Observed.Num<0>()) { Check(TEXT("관측"), AgentId, Observed[AgentId]); }
		if (AgentId < Encoded.Num<0>())  { Check(TEXT("인코딩"), AgentId, Encoded[AgentId]); }
		if (AgentId < Memory.Num<0>())   { Check(TEXT("메모리"), AgentId, Memory[AgentId]); }
	}
}

void ASH_CombatTrainer::Report(double RealElapsed)
{
	if (RealElapsed <= 0.0)
	{
		return;
	}

	const int32 Decisions = DecisionCount - DecisionsAtLastReport;
	const double DecisionsPerSecond = static_cast<double>(Decisions) / RealElapsed;
	const double StepsPerSecond = DecisionsPerSecond * Agents.Num();

	const double AliveRate   = (MetricSampleCount > 0) ? (AliveAccumulator   / MetricSampleCount) : 0.0;
	const double VisibleRate = (MetricSampleCount > 0) ? (VisibleAccumulator / MetricSampleCount) : 0.0;

	const int64 Fired = ShotsFiredTotal - ShotsFiredAtLastReport;
	const int64 Hit   = ShotsHitTotal   - ShotsHitAtLastReport;
	const double HitRate = (Fired > 0) ? (100.0 * static_cast<double>(Hit) / static_cast<double>(Fired)) : 0.0;

	// --- 라운드 지표 (대치 감지) ----------------------------------------------
	// ★ 시간 초과 비율, 첫 피해까지 시간, 최근접 적 거리가 같이 오르면 대치다.
	TArray<FSHRoundRecord> Rounds;
	double ZoneDamageOnAgents = 0.0;
	for (const TWeakObjectPtr<ASH_Arena>& Weak : Arenas)
	{
		if (ASH_Arena* Arena = Weak.Get())
		{
			Arena->ConsumeRoundRecords(Rounds);
			for (int32 Team = 0; Team < 2; ++Team)
			{
				const float Zone = Arena->ConsumeZoneDamage(Team);
				if (AgentTeamMask & (1 << Team))
				{
					ZoneDamageOnAgents += Zone;
				}
			}
		}
	}
	int32 RoundWins[2] = { 0, 0 };
	int32 RoundTimeouts = 0;
	int32 RoundMutual = 0;
	double RoundDurationSum = 0.0;
	double FirstDamageSum = 0.0;
	int32 FirstDamageCount = 0;
	for (const FSHRoundRecord& Round : Rounds)
	{
		switch (Round.End)
		{
		case ESHRoundEnd::Team0Wins:  ++RoundWins[0]; break;
		case ESHRoundEnd::Team1Wins:  ++RoundWins[1]; break;
		case ESHRoundEnd::Timeout:    ++RoundTimeouts; break;
		case ESHRoundEnd::MutualWipe: ++RoundMutual; break;
		default: break;
		}
		RoundDurationSum += Round.Duration;
		if (Round.FirstDamageTime >= 0.0f)
		{
			FirstDamageSum += Round.FirstDamageTime;
			++FirstDamageCount;
		}
	}
	const double MeanRoundDuration = Rounds.Num() > 0 ? RoundDurationSum / Rounds.Num() : 0.0;
	const double MeanFirstDamage = FirstDamageCount > 0 ? FirstDamageSum / FirstDamageCount : 0.0;
	const double TimeoutRate = Rounds.Num() > 0 ? 100.0 * RoundTimeouts / Rounds.Num() : 0.0;
	const double MeanNearestEnemy = NearestEnemySamples > 0 ? NearestEnemyDistanceSum / NearestEnemySamples : 0.0;
	NearestEnemyDistanceSum = 0.0;
	NearestEnemySamples = 0;

	// --- 진단 지표 3개 --------------------------------------------------------
	// ★ 명중률만으로는 **이동과 발사 타이밍, 엄폐가 학습됐는지 알 수 없다.**
	//   세 지표가 각각 그 셋을 담당한다. 상세한 근거는 각 Consume 함수의 주석에 있다.

	// ① 받은 피해 / 결정 1,000회 — 엄폐 효과를 재는 유일한 직접 지표
	double DamageTaken = 0.0;
	double DamageDealt = 0.0;
	int64 Gathers = 0;
	if (TrainingEnvironment)
	{
		TrainingEnvironment->ConsumeDamageStats(DamageTaken, DamageDealt, Gathers);
	}
	const double TakenPerK = (Gathers > 0) ? (1000.0 * DamageTaken / static_cast<double>(Gathers)) : 0.0;
	// 받은 피해 중 구역이 준 몫. 높으면 정책이 구역을 피하지 못하는 것이다.
	const double ZoneShare = (DamageTaken > 0.0) ? (100.0 * ZoneDamageOnAgents / DamageTaken) : 0.0;
	const double DealtPerK = (Gathers > 0) ? (1000.0 * DamageDealt / static_cast<double>(Gathers)) : 0.0;

	// ② 이동 행동 분포 — 각 20% 에 가까우면 정책은 이동을 무작위로 뽑고 있다
	// ③ 발사 기회 사용률 — 쿨다운이 끝난 결정 중 실제로 쏜 비율
	USH_ShooterInteractor::FActionStats ActionStats;
	if (Interactor)
	{
		Interactor->ConsumeActionStats(ActionStats);
	}

	double MovePercent[SHRL::MoveBins] = {};
	if (ActionStats.Decisions > 0)
	{
		for (int32 Bin = 0; Bin < SHRL::MoveBins; ++Bin)
		{
			MovePercent[Bin] = 100.0 * static_cast<double>(ActionStats.MoveCounts[Bin])
				/ static_cast<double>(ActionStats.Decisions);
		}
	}

	const double FireUseRate = (ActionStats.FireOpportunities > 0)
		? (100.0 * static_cast<double>(ActionStats.FiresUsed)
			/ static_cast<double>(ActionStats.FireOpportunities))
		: 0.0;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] %5.0f초 | 결정 %d (%.1f/s, %.0f step/s) | ")
		TEXT("생존 %.1f%% | 시야에 적 %.1f%% | 사격 %lld 명중 %lld (%.1f%%) | 셰이핑 x%.2f | 라운드 보상 x%.2f"),
		FPlatformTime::Seconds() - StartRealTime,
		DecisionCount, DecisionsPerSecond, StepsPerSecond,
		100.0 * AliveRate, 100.0 * VisibleRate,
		Fired, Hit, HitRate,
		TrainingEnvironment ? TrainingEnvironment->GetShapingScale() : 0.0f,
		TrainingEnvironment ? TrainingEnvironment->GetRoundRewardScale() : 0.0f);

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer]        라운드 %d (팀0 승 %d / 팀1 승 %d / 시간초과 %d (%.0f%%) / 동시전멸 %d) | ")
		TEXT("평균 길이 %.1f초 | 첫 피해 %.1f초 | 최근접 적 %.0f cm"),
		Rounds.Num(), RoundWins[0], RoundWins[1], RoundTimeouts, TimeoutRate, RoundMutual,
		MeanRoundDuration, MeanFirstDamage, MeanNearestEnemy);

	// 발사 가능한 결정이 전체의 몇 퍼센트인지. 고정값이 아니다 —
	// ★ 정책이 기회를 건너뛰면 쿨다운이 안 걸려서 이 비율이 **올라간다.**
	//   즉 사용률과 이 값은 반대로 움직여야 정상이다. 같은 방향으로 가면 카운터가 틀린 것이다.
	const double FireOpportunityShare = (ActionStats.Decisions > 0)
		? (100.0 * static_cast<double>(ActionStats.FireOpportunities)
			/ static_cast<double>(ActionStats.Decisions))
		: 0.0;

	// ★ 한 줄에 다 넣으면 로그가 읽히지 않는다. 진단 지표는 둘째 줄로 분리한다.
	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer]        피해 받음 %.1f (구역 %.0f%%) / 줌 %.1f (결정 1k당, 생존결정 %lld) | ")
		TEXT("발사기회 사용 %.1f%% (%lld/%lld, 기회비율 %.1f%%) | ")
		TEXT("이동 정지 %.0f%% 전진 %.0f%% 후진 %.0f%% 좌 %.0f%% 우 %.0f%% (균등=20%%)"),
		TakenPerK, ZoneShare, DealtPerK, ActionStats.Decisions,
		FireUseRate, ActionStats.FiresUsed, ActionStats.FireOpportunities, FireOpportunityShare,
		MovePercent[0], MovePercent[1], MovePercent[2], MovePercent[3], MovePercent[4]);

	// 구간 기준선을 갱신한다. 누산기를 비우는 것이 아니라 차분의 기준을 옮긴다.
	DecisionsAtLastReport = DecisionCount;
	ShotsFiredAtLastReport = ShotsFiredTotal;
	ShotsHitAtLastReport = ShotsHitTotal;
	AliveAccumulator = 0.0;
	VisibleAccumulator = 0.0;
	MetricSampleCount = 0;
}

void ASH_CombatTrainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ★ 반드시 EndTraining() 을 부른다. 안 부르면 python.exe 가 공유메모리를 잡은 채
	//   고아 프로세스로 남고, 다음 실행이 같은 공유메모리 이름으로 붙지 못한다.
	if (Trainer && Trainer->IsTraining())
	{
		UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] EndPlay — EndTraining() 호출."));
		Trainer->EndTraining();
	}

	bAssembled = false;

	Super::EndPlay(EndPlayReason);
}
