// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsPPOTrainer.h"
#include "LearningAgentsTrainer.h"
#include "SH_CombatTrainer.generated.h"

class ASH_RLCharacter;
class ULearningAgentsCritic;
class ULearningAgentsInteractor;
class ULearningAgentsManager;
class ULearningAgentsPolicy;
class USH_ShooterInteractor;
class USH_ShooterTrainingEnv;

/**
 *  전투 RL 학습 실행을 조립하는 액터. 레벨에 하나만 놓는다.
 *
 *  ★ 조립을 BeginPlay 가 아니라 **첫 Tick** 에서 한다.
 *  에이전트는 아레나가 스폰한 유닛이고, 아레나는 ASH_RLDirector::BeginPlay 가
 *  GymsManager->Start() 를 불러야 생긴다. 두 액터의 BeginPlay 순서는 정해져 있지 않으므로
 *  BeginPlay 에서 유닛을 찾으면 0개가 나올 수 있고, 그러면 **에이전트 0개로 조용히**
 *  학습이 돌아간다. Tick 은 모든 BeginPlay 뒤에 돌므로 안전하다.
 *
 *  ★ 에이전트로 등록하는 기준은 "컨트롤러가 ASH_RLBotController 인 ASH_RLCharacter" 다.
 *  그래서 커리큘럼을 BP 설정만으로 바꿀 수 있다 —
 *   1단계(vs 스크립트 봇): Team0 만 RL 컨트롤러
 *   2단계(self-play):      두 팀 모두 RL 컨트롤러
 *  트레이너 코드는 그대로다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_CombatTrainer : public AActor
{
	GENERATED_BODY()

public:
	ASH_CombatTrainer();

	//~ 결정 주기 --------------------------------------------------------------

	/** 결정 주기(Hz). 20 = 0.05초. AGun::FireInterval(0.15초) 보다 촘촘해야
	 *  "언제 쏠지" 가 정책의 선택이 된다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL", meta = (ClampMin = "1.0"))
	float DecisionRateHz = 20.0f;

	/** 에이전트 상한. 실제 유닛 수보다 크게 잡아 둔다 — 부족하면 넘치는 유닛이
	 *  등록되지 않고 정책이 영원히 무시한다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL", meta = (ClampMin = "1"))
	int32 MaxAgentNum = 512;

	//~ 학습 설정 --------------------------------------------------------------

	/** ★ TrainerFileName 기본값은 learning_core.train_behavior_cloning 이다.
	 *  생성자에서 train_ppo 로 바꾼다. 안 바꾸면 모방학습 트레이너가 뜨고,
	 *  녹화 데이터가 없으니 조용히 아무것도 학습하지 않는다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training")
	FLearningAgentsTrainerProcessSettings ProcessSettings;

	/** 기본 10초는 파이썬 첫 기동(모듈 임포트 포함)에 모자라다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training")
	FLearningAgentsSharedMemoryCommunicatorSettings SharedMemorySettings;

	UPROPERTY(EditAnywhere, Category = "SH|RL|Training")
	FLearningAgentsPPOTrainerSettings TrainerSettings;

	UPROPERTY(EditAnywhere, Category = "SH|RL|Training")
	FLearningAgentsPPOTrainingSettings TrainingSettings;

	UPROPERTY(EditAnywhere, Category = "SH|RL|Training")
	FLearningAgentsTrainingGameSettings GameSettings;

	UPROPERTY(EditAnywhere, Category = "SH|RL|Training", meta = (ClampMin = "0"))
	int32 Seed = 1234;

	/** 벽시계 기준 이 시간이 지나면 학습을 끝낸다. 0 이면 무제한.
	 *  헤드리스(-game) 실행을 스스로 끝내기 위한 것이다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training", meta = (ClampMin = "0.0"))
	float MaxWallClockSeconds = 0.0f;

	/** 진행 상황 로그 주기(초, 실제 시간). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training", meta = (ClampMin = "1.0"))
	float ReportInterval = 15.0f;

	//~ 런타임 오브젝트 ---------------------------------------------------------

	UPROPERTY(VisibleAnywhere, Category = "SH|RL")
	TObjectPtr<ULearningAgentsManager> Manager;

	UPROPERTY(Transient)
	TObjectPtr<USH_ShooterInteractor> Interactor;

	UPROPERTY(Transient)
	TObjectPtr<ULearningAgentsPolicy> Policy;

	UPROPERTY(Transient)
	TObjectPtr<ULearningAgentsCritic> Critic;

	UPROPERTY(Transient)
	TObjectPtr<USH_ShooterTrainingEnv> TrainingEnvironment;

	UPROPERTY(Transient)
	TObjectPtr<ULearningAgentsPPOTrainer> Trainer;

	/** 등록된 유닛. 지표 집계에 쓴다. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASH_RLCharacter>> Agents;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 8단계 조립. 성공하면 bAssembled = true. */
	void Assemble();

	/** 정책이 조종할 유닛을 모은다. 컨트롤러 클래스로 판별한다. */
	void CollectAgentUnits(TArray<ASH_RLCharacter*>& OutUnits) const;

	/** 유닛의 Vision 설정이 스키마와 맞는지 검사한다 — 어긋나면 벡터 크기가 깨진다. */
	bool ValidateUnitSchema(const TArray<ASH_RLCharacter*>& Units) const;

	void Report(double RealElapsed);

private:
	FLearningAgentsCommunicator Communicator;

	bool bAssembled = false;
	bool bAssembleAttempted = false;

	/** RunTraining 을 한 번이라도 불렀는가.
	 *  ★ IsTraining() 만으로는 "아직 시작 안 함" 과 "끝남" 을 구분할 수 없다.
	 *  구분하지 않으면 시작 전에 종료로 오판해 학습이 아예 돌지 않는다. */
	bool bTrainingStarted = false;

	float DecisionAccumulator = 0.0f;
	int32 DecisionCount = 0;

	/**
	 *  ★ 게임 쪽 지표를 순간값으로 찍으면 안 된다.
	 *
	 *  bResetAgentsOnUpdate = true 라서 PPO gather 경계마다 에이전트가 전부 리셋된다.
	 *  리포트가 그 프레임에 걸리면 방금 리셋된 상태를 읽어 항상 초기값이 나온다.
	 *  결정마다 누산해서 구간 평균을 낸다 — 파이썬의 Avg Rew 와 같은 의미가 된다.
	 */
	double AliveAccumulator = 0.0;
	double VisibleAccumulator = 0.0;
	int32 MetricSampleCount = 0;

	/** 구간 누적 사격/명중. Gun 이 기록하고 여기서 차분을 낸다. */
	int64 ShotsFiredTotal = 0;
	int64 ShotsHitTotal = 0;
	int64 ShotsFiredAtLastReport = 0;
	int64 ShotsHitAtLastReport = 0;

	double StartRealTime = 0.0;
	double LastReportRealTime = 0.0;
	int32 DecisionsAtLastReport = 0;
};
