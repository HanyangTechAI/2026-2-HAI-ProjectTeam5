// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_BattleBrain.h"

#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "Misc/Paths.h"
#include "RL/SH_RLCharacter.h"
#include "RL/SH_ShooterInteractor.h"
#include "RL/SH_SnapshotFiles.h"
#include "RL/SH_VisionComponent.h"

ASH_BattleBrain::ASH_BattleBrain()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("Manager"));
}

bool ASH_BattleBrain::Assemble(const TArray<ASH_RLCharacter*>& Units, const FString& SnapshotDirectory)
{
	if (Units.Num() == 0 || !Units[0])
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleBrain] 조종할 유닛이 없다."));
		return false;
	}

	// 1. 에이전트 상한은 리스너(Interactor·Policy)를 만들기 **전에** 정한다.
	Manager->SetMaxAgentNum(Units.Num());

	// 2. Interactor. MakeInteractor 는 매니저 포인터를 lvalue 참조로 받는다 — 지역 변수에 담아 넘긴다.
	ULearningAgentsManager* ManagerPtr = Manager;
	ULearningAgentsInteractor* InteractorPtr = ULearningAgentsInteractor::MakeInteractor(
		ManagerPtr, USH_ShooterInteractor::StaticClass(), TEXT("BattleInteractor"));
	Interactor = Cast<USH_ShooterInteractor>(InteractorPtr);
	if (!Interactor)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleBrain] MakeInteractor 실패."));
		return false;
	}

	// ★ 관측 정규화 값을 유닛과 맞춘다. 학습 때와 다르면 '본 지 몇 초'·'맞은 지 몇 초' 관측이 0~1 을 벗어난다.
	if (const USH_VisionComponent* Vision = Units[0]->Vision.Get())
	{
		Interactor->MemoryHorizonSeconds = Vision->MemoryHorizonSeconds;
	}
	Interactor->HitMemoryHorizonSeconds = Units[0]->HitMemoryHorizonSeconds;

	// 3. Policy — 무작위 초기화로 만든 뒤 스냅샷으로 덮어쓴다. 크리틱·학습 환경은 만들지 않는다.
	Policy = ULearningAgentsPolicy::MakePolicy(
		ManagerPtr, InteractorPtr, ULearningAgentsPolicy::StaticClass(), TEXT("BattlePolicy"),
		/*Encoder*/ nullptr, /*Policy*/ nullptr, /*Decoder*/ nullptr,
		/*bReinitEncoder*/ true, /*bReinitPolicy*/ true, /*bReinitDecoder*/ true,
		FLearningAgentsPolicySettings(), /*Seed*/ 1234);
	if (!Policy || !LoadSnapshot(SnapshotDirectory))
	{
		return false;
	}

	// 4. 에이전트 등록.
	AgentIds.Reset(Units.Num());
	for (ASH_RLCharacter* Unit : Units)
	{
		AgentIds.Add(Manager->AddAgent(Unit));
	}

	UE_LOG(LogTemp, Display, TEXT("[SH_BattleBrain] 조립 완료 — 에이전트 %d, 관측 크기 %d, 스냅샷 '%s'."),
		AgentIds.Num(), Interactor->GetObservationVectorSize(), *SnapshotDirectory);
	return true;
}

bool ASH_BattleBrain::LoadSnapshot(const FString& Directory)
{
	if (Directory.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_BattleBrain] 유닛 정의에 정책 스냅샷 폴더가 비어 있다."));
		return false;
	}

	// 상대 경로는 프로젝트 폴더 기준이다 — 팀원마다 프로젝트 위치가 달라도 같은 에셋을 쓴다.
	FString FullDirectory = FPaths::IsRelative(Directory)
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Directory))
		: Directory;
	FPaths::NormalizeDirectoryName(FullDirectory);   // 끝의 '/' 가 있으면 폴더 이름(파일 접두어)을 못 읽는다

	const TCHAR* Files[] = { TEXT("policy.bin"), TEXT("encoder.bin"), TEXT("decoder.bin") };
	for (const TCHAR* File : Files)
	{
		const FString Path = SHSnapshot::ResolveFile(FullDirectory, File);
		if (!FPaths::FileExists(Path))
		{
			UE_LOG(LogTemp, Error, TEXT("[SH_BattleBrain] 스냅샷 파일이 없다: %s"), *Path);
			return false;
		}
	}

	// ★ 스키마가 다른 스냅샷이면 엔진이 "Invalid Format" 을 찍고 정책은 무작위로 남는다.
	auto Load = [&FullDirectory](ULearningAgentsNeuralNetwork* Network, const TCHAR* FileName)
	{
		FFilePath Path;
		Path.FilePath = SHSnapshot::ResolveFile(FullDirectory, FileName);
		Network->LoadNetworkFromSnapshot(Path);
	};
	Load(Policy->GetPolicyNetworkAsset(),  TEXT("policy.bin"));
	Load(Policy->GetEncoderNetworkAsset(), TEXT("encoder.bin"));
	Load(Policy->GetDecoderNetworkAsset(), TEXT("decoder.bin"));
	return true;
}

void ASH_BattleBrain::StartThinking()
{
	Manager->ResetAgents(AgentIds);    // 정책의 순환 메모리를 비운다(지난 전투의 기억이 새지 않게)
	DecisionAccumulator = 0.0f;
	bThinking = true;
}

void ASH_BattleBrain::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bThinking || !Policy)
	{
		return;
	}

	DecisionAccumulator += DeltaSeconds;
	if (DecisionAccumulator < DecisionPeriod)
	{
		return;
	}

	// ★ 0 으로 되돌리지 않고 주기만큼 뺀다 — 프레임 간격이 흔들려도 평균 20 Hz 가 유지된다.
	// ★ while 로 여러 번 돌리지 않는다. 같은 프레임에 두 번 추론하면 게임이 진행되지 않은 같은 관측으로
	//   행동만 덮어쓴다. 학습 코드(ASH_CombatTrainer::Tick)도 한 프레임에 한 번이다.
	DecisionAccumulator -= DecisionPeriod;

	// 관측 수집 → 마스크 → 신경망 → 행동 실행까지 전부. 1.0 = 학습 때와 같은 확률 분포에서 뽑는다.
	Policy->RunInference(1.0f);
}
