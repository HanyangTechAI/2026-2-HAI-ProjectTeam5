// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsPPOTrainer.h"
#include "LearningAgentsTrainer.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "SH_HelloWorldTrainer.generated.h"

class ULearningAgentsCritic;
class ULearningAgentsManager;
class ULearningAgentsPolicy;

/**
 *  학습 대상 "에이전트" — 액터도 폰도 아닌 그냥 UObject 다.
 *
 *  ULearningAgentsManager::AddAgent 가 UObject* 를 받으므로, 에이전트는 무엇이든 될 수 있다.
 *  전투 코드를 한 줄도 섞지 않고 배관만 증명하는 것이 이 클래스의 목적이다.
 *
 *  과제: 1차원 직선 위의 X 를 Target 으로 옮긴다. 그게 전부다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API USH_HelloAgent : public UObject
{
	GENERATED_BODY()

public:
	/** 현재 위치. 행동이 이것을 움직인다. */
	UPROPERTY(VisibleAnywhere, Category = "SH|Hello")
	float X = 0.0f;

	/** 목표 위치. 에피소드마다 새로 뽑는다. */
	UPROPERTY(VisibleAnywhere, Category = "SH|Hello")
	float Target = 0.0f;

	/** 에피소드 시작 상태로 되돌린다. */
	void Randomize(float Extent);

	float GetError() const { return Target - X; }
};

/**
 *  관측 1개 · 행동 3개짜리 최소 Interactor.
 *
 *  ★ Specify 와 Gather/Perform 의 Tag 문자열이 어긋나면 해시는 통과하면서 의미만 뒤바뀐다.
 *    그래서 태그를 상수로 한 곳에만 둔다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API USH_HelloInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()

public:
	/** 한 번의 결정으로 움직이는 거리. */
	UPROPERTY(EditAnywhere, Category = "SH|Hello")
	float StepSize = 0.05f;

	/** X 가 벗어날 수 없는 범위. 관측 정규화 스케일도 이 값에서 나온다. */
	UPROPERTY(EditAnywhere, Category = "SH|Hello")
	float WorldExtent = 1.0f;

	static const FName ErrorTag;
	static const FName MoveTag;

	virtual void SpecifyAgentObservation_Implementation(
		FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
		ULearningAgentsObservationSchema* InObservationSchema) override;

	virtual void GatherAgentObservation_Implementation(
		FLearningAgentsObservationObjectElement& OutObservationObjectElement,
		ULearningAgentsObservationObject* InObservationObject,
		const int32 AgentId) override;

	virtual void SpecifyAgentAction_Implementation(
		FLearningAgentsActionSchemaElement& OutActionSchemaElement,
		ULearningAgentsActionSchema* InActionSchema) override;

	virtual void PerformAgentAction_Implementation(
		const ULearningAgentsActionObject* InActionObject,
		const FLearningAgentsActionObjectElement& InActionObjectElement,
		const int32 AgentId) override;
};

/**
 *  보상 · 완료 · 리셋만 담당한다.
 *
 *  완료는 항상 Running 을 돌려준다 — 에피소드는 MaxEpisodeStepNum 으로 Truncation 된다.
 *  "목표에 닿으면 Termination" 으로 만들면 안 된다. Termination 은 "이후 보상이 전부 0"
 *  이라는 뜻이라 크리틱이 목표 지점의 가치를 0 으로 배운다. 이 과제는 도달이 아니라 유지다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API USH_HelloTrainingEnvironment : public ULearningAgentsTrainingEnvironment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "SH|Hello")
	float WorldExtent = 1.0f;

	virtual void GatherAgentReward_Implementation(float& OutReward, const int32 AgentId) override;
	virtual void GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;
	virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;
};

/**
 *  Gate 0c — Hello-World RL 루프.
 *
 *  이 액터 하나가 증명하는 것: 플러그인 로드 · pip 설치 · python.exe 스폰 · 공유메모리 ·
 *  관측/행동/모디파이어 3종 해시 일치 · PPO 반복 · PIE 중지 시 정상 종료.
 *  전부 서로 다른 실패 모드이고, 전투 코드와 섞이면 구분이 불가능해진다.
 *
 *  통과 기준: 로그의 평균 |오차| 가 초기 ~0.67 에서 StepSize 근처까지 내려간다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_HelloWorldTrainer : public AActor
{
	GENERATED_BODY()

public:
	ASH_HelloWorldTrainer();

	//~ 과제 ------------------------------------------------------------------

	/** 동시에 학습시킬 에이전트 수. 많을수록 이터레이션당 벽시계가 짧아진다. */
	UPROPERTY(EditAnywhere, Category = "SH|Hello", meta = (ClampMin = "1"))
	int32 AgentNum = 32;

	UPROPERTY(EditAnywhere, Category = "SH|Hello", meta = (ClampMin = "0.01"))
	float WorldExtent = 1.0f;

	UPROPERTY(EditAnywhere, Category = "SH|Hello", meta = (ClampMin = "0.001"))
	float StepSize = 0.05f;

	/** 결정 주기(Hz). 계획의 실전값과 맞춰 20 으로 둔다. */
	UPROPERTY(EditAnywhere, Category = "SH|Hello", meta = (ClampMin = "1.0"))
	float DecisionRateHz = 20.0f;

	//~ 학습 설정 --------------------------------------------------------------

	/**
	 *  ★ TrainerFileName 기본값은 learning_core.train_behavior_cloning 이다.
	 *  PPO 를 돌리려면 반드시 바꿔야 한다. 안 바꾸면 모방학습 트레이너가 뜨고,
	 *  녹화 데이터가 없으니 조용히 아무것도 학습하지 않는다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training")
	FLearningAgentsTrainerProcessSettings ProcessSettings;

	/** 기본 10초는 첫 파이썬 기동(모듈 임포트 포함)에 모자란다. */
	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training")
	FLearningAgentsSharedMemoryCommunicatorSettings SharedMemorySettings;

	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training")
	FLearningAgentsPPOTrainerSettings TrainerSettings;

	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training")
	FLearningAgentsPPOTrainingSettings TrainingSettings;

	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training")
	FLearningAgentsTrainingGameSettings GameSettings;

	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training", meta = (ClampMin = "0"))
	int32 Seed = 1234;

	/**
	 *  벽시계 기준 이 시간이 지나면 학습을 끝낸다. 0 이면 무제한.
	 *
	 *  헤드리스(-game) 실행을 스스로 끝내기 위한 것이다. 에디터(PIE)에서는 에디터를 닫지
	 *  않고 EndTraining() 만 부르고 틱을 멈춘다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training", meta = (ClampMin = "0.0"))
	float MaxWallClockSeconds = 0.0f;

	/** 진행 상황 로그 주기(초, 실제 시간). */
	UPROPERTY(EditAnywhere, Category = "SH|Hello|Training", meta = (ClampMin = "1.0"))
	float ReportInterval = 10.0f;

	//~ 런타임 오브젝트 ---------------------------------------------------------

	UPROPERTY(VisibleAnywhere, Category = "SH|Hello")
	TObjectPtr<ULearningAgentsManager> Manager;

	UPROPERTY(Transient)
	TObjectPtr<USH_HelloInteractor> Interactor;

	UPROPERTY(Transient)
	TObjectPtr<ULearningAgentsPolicy> Policy;

	UPROPERTY(Transient)
	TObjectPtr<ULearningAgentsCritic> Critic;

	UPROPERTY(Transient)
	TObjectPtr<USH_HelloTrainingEnvironment> TrainingEnvironment;

	UPROPERTY(Transient)
	TObjectPtr<ULearningAgentsPPOTrainer> Trainer;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USH_HelloAgent>> Agents;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Assemble();
	void Report(double RealElapsed);

	/** 현재 모든 에이전트의 평균 |오차|. */
	float ComputeMeanError() const;

private:
	FLearningAgentsCommunicator Communicator;

	bool bAssembled = false;
	bool bFailureReported = false;

	float DecisionAccumulator = 0.0f;
	int32 DecisionCount = 0;

	/**
	 *  ★ 순간값을 찍으면 안 된다.
	 *
	 *  bResetAgentsOnUpdate = true 라서 PPO gather 경계마다 32개 에이전트가 전부
	 *  리셋된다(로그의 "Resetting Agents [0 1 2 ...]"). 리포트가 그 프레임에 걸리면
	 *  방금 재추첨된 상태를 읽어 항상 무작위 기준선이 나온다.
	 *  결정마다 누산해서 구간 평균을 낸다 — 파이썬의 Avg Rew 와 같은 의미가 된다.
	 */
	double ErrorAccumulator = 0.0;
	int32 ErrorSampleCount = 0;

	double StartRealTime = 0.0;
	double LastReportRealTime = 0.0;
	int32 DecisionsAtLastReport = 0;
};
