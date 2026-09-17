// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_HelloWorldTrainer.h"

#include "LearningAgentsActions.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsObservations.h"
#include "LearningAgentsPolicy.h"

const FName USH_HelloInteractor::ErrorTag = TEXT("PositionError");
const FName USH_HelloInteractor::MoveTag  = TEXT("MoveChoice");

// ============================================================================
//  USH_HelloAgent
// ============================================================================

void USH_HelloAgent::Randomize(float Extent)
{
	X      = FMath::FRandRange(-Extent, Extent);
	Target = FMath::FRandRange(-Extent, Extent);
}

// ============================================================================
//  USH_HelloInteractor
// ============================================================================

void USH_HelloInteractor::SpecifyAgentObservation_Implementation(
	FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
	ULearningAgentsObservationSchema* InObservationSchema)
{
	// 관측 1개: 목표까지의 부호 있는 오차.
	//
	// ★ Scale 은 선택이 아니다. 오차 범위가 ±2*WorldExtent 이므로 그 크기로 나눠
	//   신경망 첫 레이어에 들어가는 값이 대략 ±1 이 되게 한다.
	FLearningAgentsFloatObservationNormalizationSettings Norm;
	Norm.Scale = FMath::Max(KINDA_SMALL_NUMBER, WorldExtent);

	OutObservationSchemaElement = ULearningAgentsObservations::SpecifyFloatObservation(
		InObservationSchema, Norm, /*bActive=*/true, ErrorTag);
}

void USH_HelloInteractor::GatherAgentObservation_Implementation(
	FLearningAgentsObservationObjectElement& OutObservationObjectElement,
	ULearningAgentsObservationObject* InObservationObject,
	const int32 AgentId)
{
	const USH_HelloAgent* Agent = Cast<USH_HelloAgent>(GetAgent(AgentId));
	const float Error = Agent ? Agent->GetError() : 0.0f;

	OutObservationObjectElement = ULearningAgentsObservations::MakeFloatObservation(
		InObservationObject, Error, /*bActive=*/true, ErrorTag);
}

void USH_HelloInteractor::SpecifyAgentAction_Implementation(
	FLearningAgentsActionSchemaElement& OutActionSchemaElement,
	ULearningAgentsActionSchema* InActionSchema)
{
	// 행동 3개: 왼쪽 / 정지 / 오른쪽.
	OutActionSchemaElement = ULearningAgentsActions::SpecifyExclusiveDiscreteAction(
		InActionSchema, /*Size=*/3, TArray<float>(), /*bActive=*/true, MoveTag);
}

void USH_HelloInteractor::PerformAgentAction_Implementation(
	const ULearningAgentsActionObject* InActionObject,
	const FLearningAgentsActionObjectElement& InActionObjectElement,
	const int32 AgentId)
{
	USH_HelloAgent* Agent = Cast<USH_HelloAgent>(GetAgent(AgentId));
	if (!Agent)
	{
		return;
	}

	int32 Index = 1;
	if (!ULearningAgentsActions::GetExclusiveDiscreteAction(
			Index, InActionObject, InActionObjectElement, /*bActive=*/true, MoveTag))
	{
		return;
	}

	Agent->X = FMath::Clamp(
		Agent->X + StepSize * static_cast<float>(Index - 1),
		-WorldExtent * 1.5f,
		WorldExtent * 1.5f);
}

// ============================================================================
//  USH_HelloTrainingEnvironment
// ============================================================================

void USH_HelloTrainingEnvironment::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	const USH_HelloAgent* Agent = Cast<USH_HelloAgent>(GetAgent(AgentId));
	if (!Agent)
	{
		OutReward = 0.0f;
		return;
	}

	// 목표에서 멀수록 나쁘다. WorldExtent 로 나눠 스텝당 대략 [-2, 0] 범위로 맞춘다.
	OutReward = -FMath::Abs(Agent->GetError()) / FMath::Max(KINDA_SMALL_NUMBER, WorldExtent);
}

void USH_HelloTrainingEnvironment::GatherAgentCompletion_Implementation(
	ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
	// 항상 Running. 에피소드 종료는 MaxEpisodeStepNum 이 Truncation 으로 처리한다.
	OutCompletion = ELearningAgentsCompletion::Running;
}

void USH_HelloTrainingEnvironment::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	if (USH_HelloAgent* Agent = Cast<USH_HelloAgent>(GetAgent(AgentId)))
	{
		Agent->Randomize(WorldExtent);
	}
}

// ============================================================================
//  ASH_HelloWorldTrainer
// ============================================================================

ASH_HelloWorldTrainer::ASH_HelloWorldTrainer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("LearningAgentsManager"));

	// ★ 기본값이 learning_core.train_behavior_cloning 이다. 반드시 바꾼다.
	ProcessSettings.TrainerFileName = TEXT("learning_core.train_ppo");
	ProcessSettings.TaskName        = TEXT("SHHelloWorld");

	// 기본 10초는 파이썬 첫 기동에 모자라다.
	SharedMemorySettings.Timeout = 60.0f;

	// 에피소드 128스텝 = 20Hz 에서 6.4초. 32 에이전트면 이터레이션당 정확히 4096스텝.
	TrainerSettings.MaxEpisodeStepNum                   = 128;
	TrainerSettings.MaximumRecordedStepsPerIteration    = 4096;
	TrainerSettings.MaximumRecordedEpisodesPerIteration = 1000;

	// 기본 0.0 은 나쁜 기본값이다 — 엔트로피가 초반에 붕괴하면 탐색이 멈춘다.
	TrainingSettings.ActionEntropyWeight = 0.005f;
	TrainingSettings.bUseTensorboard     = true;
	TrainingSettings.Device              = ELearningAgentsTrainingDevice::GPU;
}

void ASH_HelloWorldTrainer::BeginPlay()
{
	Super::BeginPlay();
	Assemble();

	StartRealTime         = FPlatformTime::Seconds();
	LastReportRealTime    = StartRealTime;
	DecisionsAtLastReport = 0;
}

void ASH_HelloWorldTrainer::Assemble()
{
	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_Hello] Manager 가 없다."));
		return;
	}

	// --- 1. 리스너를 만들기 전에 최대 에이전트 수를 정한다 ---------------------
	AgentNum = FMath::Max(1, AgentNum);
	Manager->SetMaxAgentNum(AgentNum);

	ULearningAgentsManager* ManagerPtr = Manager;

	// --- 2. Interactor -------------------------------------------------------
	ULearningAgentsInteractor* InteractorPtr = ULearningAgentsInteractor::MakeInteractor(
		ManagerPtr, USH_HelloInteractor::StaticClass(), TEXT("HelloInteractor"));

	Interactor = Cast<USH_HelloInteractor>(InteractorPtr);
	if (!Interactor)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_Hello] MakeInteractor 실패."));
		return;
	}
	Interactor->StepSize    = StepSize;
	Interactor->WorldExtent = WorldExtent;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_Hello] 스키마 크기 — 관측 %d (인코딩 %d) / 행동 %d (분포 %d, 모디파이어 %d)"),
		Interactor->GetObservationVectorSize(),
		Interactor->GetObservationEncodedVectorSize(),
		Interactor->GetActionVectorSize(),
		Interactor->GetActionDistributionVectorSize(),
		Interactor->GetActionModifierVectorSize());

	// --- 3. Policy / 4. Critic ----------------------------------------------
	ULearningAgentsPolicy* PolicyPtr = ULearningAgentsPolicy::MakePolicy(
		ManagerPtr, InteractorPtr, ULearningAgentsPolicy::StaticClass(), TEXT("HelloPolicy"),
		/*Encoder*/ nullptr, /*Policy*/ nullptr, /*Decoder*/ nullptr,
		/*bReinitEncoder*/ true, /*bReinitPolicy*/ true, /*bReinitDecoder*/ true,
		FLearningAgentsPolicySettings(), Seed);
	Policy = PolicyPtr;

	ULearningAgentsCritic* CriticPtr = ULearningAgentsCritic::MakeCritic(
		ManagerPtr, InteractorPtr, PolicyPtr, ULearningAgentsCritic::StaticClass(), TEXT("HelloCritic"),
		/*Network*/ nullptr, /*bReinit*/ true, FLearningAgentsCriticSettings(), Seed);
	Critic = CriticPtr;

	// --- 5. TrainingEnvironment ---------------------------------------------
	ULearningAgentsTrainingEnvironment* EnvPtr = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(
		ManagerPtr, USH_HelloTrainingEnvironment::StaticClass(), TEXT("HelloTrainingEnv"));

	TrainingEnvironment = Cast<USH_HelloTrainingEnvironment>(EnvPtr);
	if (!TrainingEnvironment)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_Hello] MakeTrainingEnvironment 실패."));
		return;
	}
	TrainingEnvironment->WorldExtent = WorldExtent;

	// --- 6. 파이썬 트레이너 프로세스 -----------------------------------------
	UE_LOG(LogTemp, Display, TEXT("[SH_Hello] 파이썬 트레이너 기동 — 모듈 '%s', 타임아웃 %.0f초"),
		*ProcessSettings.TrainerFileName, SharedMemorySettings.Timeout);

	Communicator = ULearningAgentsCommunicatorLibrary::MakeSharedMemoryTrainingProcess(
		ProcessSettings, SharedMemorySettings);

	if (!Communicator.Trainer.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_Hello] 트레이너 프로세스 기동 실패. Intermediate/PipInstall 이 설치됐는지, ")
			TEXT("경로에 한글/공백이 없는지 확인할 것."));
		return;
	}

	// --- 7. PPOTrainer -------------------------------------------------------
	Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(
		ManagerPtr, InteractorPtr, EnvPtr, PolicyPtr, CriticPtr,
		Communicator, ULearningAgentsPPOTrainer::StaticClass(), TEXT("HelloPPOTrainer"),
		TrainerSettings);

	if (!Trainer)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_Hello] MakePPOTrainer 실패."));
		return;
	}

	// --- 8. ★ 에이전트 등록은 맨 마지막 --------------------------------------
	// 리스너가 생기기 전에 추가된 에이전트는 그 리스너에게 영원히 보이지 않고, 에러도 없다.
	Agents.Reset(AgentNum);
	for (int32 Index = 0; Index < AgentNum; ++Index)
	{
		USH_HelloAgent* Agent = NewObject<USH_HelloAgent>(this);
		Agent->Randomize(WorldExtent);
		Agents.Add(Agent);
		Manager->AddAgent(Agent);
	}

	bAssembled = true;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_Hello] 조립 완료 — 에이전트 %d, 결정 %.0fHz, 에피소드 %d스텝(%.1f초), ")
		TEXT("이터레이션당 %d스텝, 디바이스 %s"),
		AgentNum, DecisionRateHz,
		TrainerSettings.MaxEpisodeStepNum,
		TrainerSettings.MaxEpisodeStepNum / FMath::Max(1.0f, DecisionRateHz),
		TrainerSettings.MaximumRecordedStepsPerIteration,
		TrainingSettings.Device == ELearningAgentsTrainingDevice::GPU ? TEXT("GPU") : TEXT("CPU"));
}

void ASH_HelloWorldTrainer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bAssembled || !Trainer)
	{
		return;
	}

	const float DecisionPeriod = 1.0f / FMath::Max(1.0f, DecisionRateHz);

	DecisionAccumulator += DeltaSeconds;
	if (DecisionAccumulator >= DecisionPeriod)
	{
		// 0 으로 리셋하지 않는다 — 프레임 지터가 있어도 평균 주기가 유지된다.
		DecisionAccumulator -= DecisionPeriod;
		++DecisionCount;

		// ★ 이 한 줄이 전부다.
		//   RunTraining 내부에서 GatherCompletions -> GatherRewards -> ProcessExperience
		//   -> Policy->RunInference() (= GatherObservations / MakeActionModifiers /
		//   EncodeObservations / EvaluatePolicy / DecodeAndSampleActions / PerformActions)
		//   를 전부 부른다. 밖에서 또 부르면 같은 스텝을 두 번 수집하게 된다.
		Trainer->RunTraining(TrainingSettings, GameSettings, true, true);

		// 결정마다 누산한다. 리포트 시점의 순간값은 리셋 프레임과 겹쳐 쓸모가 없다.
		ErrorAccumulator += ComputeMeanError();
		++ErrorSampleCount;

		if (!bFailureReported && Trainer->HasTrainingFailed())
		{
			bFailureReported = true;
			UE_LOG(LogTemp, Error,
				TEXT("[SH_Hello] ★ 학습 실패. 위쪽 LogLearning 로그를 볼 것. 학습을 종료한다."));
			Trainer->EndTraining();
		}
	}

	const double RealNow = FPlatformTime::Seconds();
	const double RealElapsed = RealNow - LastReportRealTime;
	if (RealElapsed >= ReportInterval)
	{
		Report(RealElapsed);
		LastReportRealTime    = RealNow;
		DecisionsAtLastReport = DecisionCount;
	}

	// --- 벽시계 제한 ---------------------------------------------------------
	if (MaxWallClockSeconds > 0.0f && (RealNow - StartRealTime) >= MaxWallClockSeconds)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SH_Hello] 벽시계 제한 %.0f초 도달 — 학습 종료."), MaxWallClockSeconds);

		Report(FMath::Max(0.001, RealNow - LastReportRealTime));

		if (Trainer->IsTraining())
		{
			Trainer->EndTraining();
		}
		bAssembled = false;

		// 에디터에서는 에디터를 닫으면 안 된다. 헤드리스일 때만 프로세스를 끝낸다.
		if (!GIsEditor)
		{
			FPlatformMisc::RequestExit(false);
		}
	}
}

float ASH_HelloWorldTrainer::ComputeMeanError() const
{
	if (Agents.Num() == 0)
	{
		return 0.0f;
	}

	float ErrorSum = 0.0f;
	for (const USH_HelloAgent* Agent : Agents)
	{
		if (Agent)
		{
			ErrorSum += FMath::Abs(Agent->GetError());
		}
	}
	return ErrorSum / Agents.Num();
}

void ASH_HelloWorldTrainer::Report(double RealElapsed)
{
	// 구간 평균 |오차| — 이게 내려가면 학습이 되고 있는 것이다.
	// 무작위 정책의 기댓값은 X 와 Target 이 [-1,1] 균등일 때 2/3 ≈ 0.667 이다.
	// 파이썬 로그의 Avg Rew 와 부호만 다른 같은 값이어야 한다.
	const float MeanError = (ErrorSampleCount > 0)
		? static_cast<float>(ErrorAccumulator / ErrorSampleCount)
		: ComputeMeanError();

	ErrorAccumulator = 0.0;
	ErrorSampleCount = 0;

	const int32 Decisions = DecisionCount - DecisionsAtLastReport;
	const double StepsPerSecond = (RealElapsed > 0.0)
		? (static_cast<double>(Decisions) * Agents.Num() / RealElapsed) : 0.0;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_Hello] 평균 |오차| %.4f  (무작위 기준선 %.3f, 한계 %.3f)  |  ")
		TEXT("수집 %.0f step/s  |  이터레이션당 %.1f초  |  학습중 %s"),
		MeanError,
		WorldExtent * 2.0f / 3.0f,
		StepSize * 0.5f,
		StepsPerSecond,
		(StepsPerSecond > 0.0)
			? TrainerSettings.MaximumRecordedStepsPerIteration / StepsPerSecond : 0.0,
		(Trainer && Trainer->IsTraining()) ? TEXT("예") : TEXT("아니오"));
}

void ASH_HelloWorldTrainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ★ 반드시 호출한다. BeginTraining 이 고정 델타타임 / t.MaxFPS / vsync 같은
	//   전역 엔진 상태를 바꾸고 이전 값을 트레이너 안에 보관한다.
	//   빼먹으면 PIE 를 멈춰도 에디터가 고정 타임스텝에 남는다.
	if (Trainer && Trainer->IsTraining())
	{
		UE_LOG(LogTemp, Display, TEXT("[SH_Hello] EndTraining() 호출 — 파이썬 프로세스 종료."));
		Trainer->EndTraining();
	}

	Super::EndPlay(EndPlayReason);
}
