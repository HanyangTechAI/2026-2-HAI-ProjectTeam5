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
	TrainingSettings.bUseTensorboard     = true;
	TrainingSettings.Device              = ELearningAgentsTrainingDevice::GPU;
}

void ASH_CombatTrainer::BeginPlay()
{
	Super::BeginPlay();

	StartRealTime = FPlatformTime::Seconds();
	LastReportRealTime = StartRealTime;

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
	Trainer->RunTraining(TrainingSettings, GameSettings, true, true);
	bTrainingStarted = true;

	// --- 리포트 -------------------------------------------------------------
	if ((RealNow - LastReportRealTime) >= ReportInterval)
	{
		Report(RealNow - LastReportRealTime);
		LastReportRealTime = RealNow;
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

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] %5.0f초 | 결정 %d (%.1f/s, %.0f step/s) | ")
		TEXT("생존 %.1f%% | 시야에 적 %.1f%% | 사격 %lld 명중 %lld (%.1f%%) | 셰이핑 x%.2f"),
		FPlatformTime::Seconds() - StartRealTime,
		DecisionCount, DecisionsPerSecond, StepsPerSecond,
		100.0 * AliveRate, 100.0 * VisibleRate,
		Fired, Hit, HitRate,
		TrainingEnvironment ? TrainingEnvironment->GetShapingScale() : 0.0f);

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
