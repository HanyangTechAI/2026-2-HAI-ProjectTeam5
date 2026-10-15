// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsPPOTrainer.h"
#include "LearningAgentsTrainer.h"
#include "SH_Arena.h"
#include "SH_CombatTrainer.generated.h"

class ASH_Arena;
class ASH_RLCharacter;
class ABase_Character;
class ULearningAgentsCritic;
class ULearningAgentsInteractor;
class ULearningAgentsManager;
class ULearningAgentsPolicy;
class USH_ShooterInteractor;
class USH_ShooterTrainingEnv;

/** 상대 혼합에서 아레나의 역할. 학습 정책은 항상 팀 0 이고, 팀 1 이 역할에 따라 다르다. */
enum class ESHArenaRole : uint8
{
	SelfPlay,   /** 팀 1 도 학습 정책 (양쪽 데이터 모두 학습에 쓴다). */
	Snapshot,   /** 팀 1 은 과거 스냅샷 정책 (추론만). */
	Bot,        /** 팀 1 은 스크립트 봇 (반응 지연 무작위). */
};

/** 상대 풀의 한 항목 — 스냅샷 폴더 하나와 그 상대에 대한 학습 정책의 전적. */
struct FSHOpponentEntry
{
	FString Directory;
	int32 Games = 0;
	int32 LearnerWins = 0;
};

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

	/**
	 *  정책이 갱신될 때마다 전 에이전트를 리셋할지(RunTraining 의 4번째 인자).
	 *
	 *  ★ 기본 false. true 면 갱신 순간 열려 있던 에피소드를 **버린다**.
	 *  게더 1회(16,384스텝)는 에이전트 160 에서 시뮬레이션 5.1초, 320 에서 2.6초 분량이라
	 *  20~45초짜리 라운드는 끝까지 갈 수 없고 통째로 버려진다.
	 *  false 면 라운드는 MaxEpisodeStepNum 단위로 Truncation 되고 세계는 이어진다.
	 *  대가: 갱신을 걸친 에피소드 조각에 한 번 전 정책의 데이터가 섞인다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training")
	bool bResetAgentsOnUpdate = false;

	/**
	 *  self-play. 켜면 조립 직전에 아레나 유닛 중 RL 컨트롤러가 아닌 것(팀 1 의 스크립트 봇)의
	 *  컨트롤러를 ASH_RLBotController 로 바꿔 끼운다. 두 팀이 같은 정책을 공유한다.
	 *
	 *  ★ 에셋(BP_Unit_Team1 의 AIControllerClass)을 고치지 않는다. 실행 인자 하나로 1단계와
	 *  self-play 를 오갈 수 있고, 대치 A/B 실험을 같은 빌드로 돌릴 수 있다.
	 *  유닛은 풀에서 재사용되고 Enable/Disable 이 빙의를 건드리지 않으므로 교체가 유지된다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training")
	bool bSelfPlay = false;

	/**
	 *  self-play 일 때 게더당 기록 스텝 수.
	 *  ★ 16,384 는 에이전트 320 에서 시뮬레이션 2.6초 분량이라 갱신(약 5초) 시간이 지배한다.
	 *  65,536 이면 약 10초 분량. 갱신 1회 비용은 미니배치 크기로 정해져 크게 늘지 않는다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training", meta = (ClampMin = "1024"))
	int32 SelfPlayRecordedSteps = 65536;

	/** 벽시계 기준 이 시간이 지나면 학습을 끝낸다. 0 이면 무제한.
	 *  헤드리스(-game) 실행을 스스로 끝내기 위한 것이다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training", meta = (ClampMin = "0.0"))
	float MaxWallClockSeconds = 0.0f;

	/** 진행 상황 로그 주기(초, 실제 시간). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training", meta = (ClampMin = "1.0"))
	float ReportInterval = 15.0f;

	/**
	 *  버전별 스냅샷 저장 주기(초, 실제 시간). 0 이면 끈다. -SHSnapshotInterval= 로 바꾼다.
	 *
	 *  ★ 엔진의 스냅샷(TrainingSettings.bSaveSnapshots)은 매번 **같은 파일 이름을 덮어쓴다**
	 *  (train_ppo.py: Snapshots/policy.bin). 과거 정책이 남지 않아 평가·상대 풀을 만들 수 없다.
	 *  그래서 언리얼 쪽 네트워크(갱신마다 파이썬에서 받아 온 최신 가중치)를 직접 저장한다.
	 *
	 *  위치 (2026-10-14 부터):
	 *    Snapshots/<저장 날짜 yyyy-MM-dd>/cum<누적 시간 HHHhMMm>/cum<누적>_{policy,encoder,decoder,critic}.bin, _meta.txt
	 *    Snapshots/<저장 날짜>.md — 그 날의 수정사항(사람이 적는다) + 실행 기록(트레이너가 덧붙인다)
	 *  누적 시간 = -SHResume 으로 이어 온 이전 실행들의 학습 시간 + 이번 실행의 경과 시간.
	 *  학습 종료 시에도 한 번 저장한다(meta 의 label=final).
	 *  2026-10-13 까지의 스냅샷은 Saved/SHSnapshots/<실행 시각>/<경과 초>s/policy.bin 형식이다 — 둘 다 읽는다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Training", meta = (ClampMin = "0.0"))
	float SnapshotIntervalSeconds = 3600.0f;

	//~ 평가 모드 ---------------------------------------------------------------
	//
	//  -SHEval=<스냅샷 폴더> 로 켠다. 학습기·파이썬·크리틱 없이 정책을 불러와 추론만 돌린다.
	//  상대는 지금의 팀 1 컨트롤러다 — 기본은 스크립트 봇, -SHSelfPlay=1 이면 같은 정책.
	//  라운드 승패를 모아 RL 팀(팀 0)의 승률과 95% 신뢰구간을 낸다.
	//  -SHEvalRealtime=1 이면 고정 델타타임을 쓰지 않아 화면을 켜고 실제 속도로 볼 수 있다.

	/** 평가에서 행동을 뽑을 때의 잡음 배율. 1 = 학습 때와 같은 분포, 0 = 가장 확률 높은 행동. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Eval", meta = (ClampMin = "0.0"))
	float EvalActionNoise = 1.0f;

	/** 이 라운드 수를 넘으면 평가를 끝낸다. 0 이면 MaxWallClockSeconds 까지. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Eval", meta = (ClampMin = "0"))
	int32 EvalMaxRounds = 0;

	/**
	 *  레벨에 저장해 두는 평가 설정 — 시각 확인용 레벨(Lvl_SHView)에서 Play 만 누르면 평가 모드로 돈다.
	 *  ★ 명령줄(-SHEval= 등)이 있으면 명령줄이 우선한다. 학습 레벨은 이 값을 꺼 둔다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Eval")
	bool bEvalOnPlay = false;

	/** 볼 정책의 스냅샷 폴더 (policy.bin / encoder.bin / decoder.bin 이 있는 곳). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Eval", meta = (EditCondition = "bEvalOnPlay"))
	FDirectoryPath EvalSnapshotDirectory;

	/** 비우면 팀 1 은 스크립트 봇(bSelfPlay 면 같은 정책). 채우면 그 스냅샷 정책과 싸운다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Eval", meta = (EditCondition = "bEvalOnPlay"))
	FDirectoryPath EvalOpponentDirectory;

	/** 고정 델타타임을 쓰지 않고 실제 속도로 진행한다(화면으로 볼 때). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Eval", meta = (EditCondition = "bEvalOnPlay"))
	bool bEvalRealtimeOnPlay = true;

	/** RL 유닛의 시야를 그린다: 부채꼴 레이 16개(적 = 빨강, 아군 = 파랑, 벽 = 회색)와
	 *  보이는 적까지의 선(초록). 아레나가 많으면 무겁다 — 시각 확인 레벨용. -SHDrawVision=1 */
	UPROPERTY(EditAnywhere, Category = "SH|Debug")
	bool bDrawVisionDebug = false;

	//~ 상대 혼합 (Phase 3b) ----------------------------------------------------
	//
	//  -SHOpponentMix=1 로 켠다. 아레나를 셋으로 나눈다:
	//   self-play(나머지) / 과거 스냅샷(MixSnapshotArenas) / 스크립트 봇(MixBotArenas).
	//  ★ 목적: (1) 같은 정책끼리만 싸우면 생기는 전략 순환·망각을 막는다.
	//          (2) 상대가 고정된 아레나에서는 승률이 실력에 따라 움직이므로 승패 보상이 신호가 된다
	//              (self-play 아레나의 승패 보상은 0 — ASH_Arena::OutcomeRewardScale).
	//  ★ 스냅샷 상대는 두 번째 Manager + Interactor + Policy 로 추론만 한다(학습기 없음).
	//  평가 모드에서 -SHEvalOpponent=<폴더> 를 주면 모든 아레나가 그 스냅샷과 싸운다.

	UPROPERTY(EditAnywhere, Category = "SH|RL|OpponentMix")
	bool bOpponentMix = false;

	UPROPERTY(EditAnywhere, Category = "SH|RL|OpponentMix", meta = (ClampMin = "0"))
	int32 MixSnapshotArenas = 6;

	UPROPERTY(EditAnywhere, Category = "SH|RL|OpponentMix", meta = (ClampMin = "0"))
	int32 MixBotArenas = 2;

	/** 스냅샷 상대를 바꾸는 주기(초, 실제 시간). -SHOpponentSwap= */
	UPROPERTY(EditAnywhere, Category = "SH|RL|OpponentMix", meta = (ClampMin = "10.0"))
	float OpponentSwapSeconds = 600.0f;

	/** 교체 때 가장 최근 스냅샷을 고를 확률. 나머지는 PFSP(지는 상대일수록 자주). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|OpponentMix", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OpponentLatestProbability = 0.3f;

	/** 이 실행의 스냅샷 외에 풀에 넣을 폴더. 하위 폴더(두 단계까지) 중 정책 파일이 있는 것을 모두 넣는다.
	 *  -SHOpponentPool=<폴더> (예: Snapshots/2026-10-14, 또는 옛 형식 Saved/SHSnapshots/<실행 시각>). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|OpponentMix")
	FString OpponentPoolDir;

	/** 스크립트 봇 반응 지연을 라운드마다 이 범위에서 무작위로 뽑는다(초).
	 *  ★ 학습된 좌선회 66% 는 봇의 0.25초 고정 조준점 갱신을 공략한 것이었다(TODO 1A). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|OpponentMix")
	FVector2D BotReactionDelayRange = FVector2D(0.1, 0.4);

	UPROPERTY(VisibleAnywhere, Category = "SH|RL")
	TObjectPtr<ULearningAgentsManager> OpponentManager;

	UPROPERTY(Transient)
	TObjectPtr<USH_ShooterInteractor> OpponentInteractor;

	UPROPERTY(Transient)
	TObjectPtr<ULearningAgentsPolicy> OpponentPolicy;

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

	/** 유닛이 있는 아레나. 라운드 지표를 모은다. */
	TArray<TWeakObjectPtr<ASH_Arena>> Arenas;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 8단계 조립. 성공하면 bAssembled = true. */
	void Assemble();

	/** 정책이 조종할 유닛을 모은다. 컨트롤러 클래스로 판별한다. */
	void CollectAgentUnits(TArray<ASH_RLCharacter*>& OutUnits, TArray<ABase_Character*>* OutOpponents = nullptr) const;

	/** 유닛의 Vision 설정이 스키마와 맞는지 검사한다 — 어긋나면 벡터 크기가 깨진다. */
	bool ValidateUnitSchema(const TArray<ASH_RLCharacter*>& Units) const;

	void Report(double RealElapsed);

	/**
	 *  결정 직후 등록된 에이전트의 인코딩 관측 · 정책 메모리에서 NaN / ±∞ / |x| > 1e6 을 찾아 기록한다.
	 *
	 *  ★ 2026-10-02 크래시 조사용. "MaxIdx != INDEX_NONE" 는 로짓이 NaN 일 때 난다.
	 *  메모리(GRU)가 먼저 오염되면 다음 결정에서 크래시하므로, 여기서 한 스텝 먼저 잡힌다.
	 *  엔진은 리셋된 에이전트의 인코딩 관측을 FLT_MAX 표식값으로 채우므로(LearningAgentsPolicy.cpp
	 *  OnAgentsReset), 결정 직후에 그 값이 남아 있으면 그것도 이상이다.
	 */
	void ScanForNonFinite();

	/** 정책·인코더·디코더·크리틱을 Snapshots/<날짜>/cum<누적 시간>/ 에 저장하고 날짜별 md 에 한 줄 남긴다.
	 *  위치 규칙은 SnapshotIntervalSeconds 주석. */
	void SaveVersionedSnapshot(bool bFinal);

	/** Snapshots/<오늘 날짜>.md 에 한 줄 덧붙인다. 파일이 없으면 머리말과 함께 만들고,
	 *  이 실행이 그 날짜 파일에 처음 쓰는 것이면 실행 머리말(RunLogHeader)을 먼저 쓴다. */
	void AppendDailyLog(const FString& Line);

	/** 누적 학습 시간(초) = 이어 온 이전 실행들 + 이번 실행 경과. */
	double GetCumulativeSeconds() const;

	/** 평가 모드: 폴더의 policy/encoder/decoder.bin 을 정책에 불러온다. 하나라도 없으면 false. */
	bool LoadPolicySnapshot(const FString& Directory);

	/**
	 *  이어서 학습하기: 정책·인코더·디코더·크리틱을 불러오고 보상 수집 누적 횟수를 복원한다.
	 *  ★ 첫 RunTraining 의 BeginTraining 이 언리얼 쪽 네트워크를 파이썬으로 보내므로
	 *  (LearningAgentsPPOTrainer.cpp SendNetwork) 그 전에 불러오면 된다.
	 *  ★ 옵티마이저(AdamW) 상태는 저장되지 않아 새로 시작한다 — 약 1,000 갱신(10분) 동안 흔들릴 수 있다.
	 */
	bool LoadResumeSnapshot(const FString& Directory);

	/** 평가 모드의 한 결정: 보상 누산 소비 → 끝난 아레나 리셋 → 추론. */
	void TickEvaluation();

	/** 평가 종료 요약(승률, 95% 윌슨 구간). */
	void LogEvaluationSummary() const;

	//~ 상대 혼합 ---------------------------------------------------------------

	/** 아레나 역할을 정한다(조립 초반, 컨트롤러 교체 전). */
	void AssignArenaRoles();

	ESHArenaRole GetArenaRole(const ASH_Arena* Arena) const;

	/** 이 아레나에서 이 팀이 학습 정책인가. */
	bool IsLearnerTeam(const ASH_Arena* Arena, int32 Team) const;

	/** 팀 1 을 RL 컨트롤러로 바꿔야 하는 아레나인가(self-play · 스냅샷). */
	bool NeedsRLTeam1(const ASH_Arena* Arena) const;

	/** 상대 Interactor/Policy 를 만들고 스냅샷 아레나의 팀 1 을 등록한다. */
	bool AssembleOpponent(const TArray<ABase_Character*>& OpponentUnits);

	/** 매 결정: 끝난 라운드 기록을 모으고, 새 라운드가 시작된 아레나를 처리하고, 상대 추론을 돌린다. */
	void TickOpponents(double RealNow);

	/** 아레나의 라운드 기록을 꺼내 역할별·상대별로 집계하고 PendingRounds 에 쌓는다. */
	void CollectRounds();

	/** 풀을 다시 훑고 다음 상대를 골라 불러온다. 풀이 비면 학습 정책의 지금 가중치를 복사한다. */
	void SwapOpponent();

	/** 풀 폴더를 다시 훑는다(이 실행의 스냅샷 + OpponentPoolDir). */
	void RefreshOpponentPool();

	/** 폴더의 policy/encoder/decoder 를 상대 정책에 불러온다. */
	bool LoadOpponentSnapshot(const FString& Directory);


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

	/** 살아 있는 RL 유닛의 최근접 (살아 있는) 적까지 거리 합과 표본 수.
	 *  ★ 대치 감지 지표. 서로 버티면 이 값이 커진다. */
	double NearestEnemyDistanceSum = 0.0;
	int64 NearestEnemySamples = 0;

	/** RL 에이전트가 속한 팀(비트). 1단계는 팀 0 만, self-play 는 둘 다. */
	int32 AgentTeamMask = 0;

	/** 등록된 에이전트 id (조립 때 고정). ScanForNonFinite 가 쓴다. */
	TArray<int32> AgentIds;
	int32 NonFiniteReports = 0;

	/** 커맨드라인 오버라이드(조립 때 아레나·환경에 적용). 음수 = 지정 안 됨. */
	int32 RoundRulesOverride = -1;
	float RoundTimeOverride = -1.0f;
	float TeamSpiritOverride = -1.0f;
	float OutcomeOverride = -1.0f;
	int32 RoundRampOverride = -1;
	int32 BufferOverride = -1;
	int32 GradClipOverride = -1;

	/** 구간 누적 사격/명중. Gun 이 기록하고 여기서 차분을 낸다. */
	int64 ShotsFiredTotal = 0;
	int64 ShotsHitTotal = 0;
	int64 ShotsFiredAtLastReport = 0;
	int64 ShotsHitAtLastReport = 0;

	double StartRealTime = 0.0;
	double LastReportRealTime = 0.0;
	int32 DecisionsAtLastReport = 0;

	/** 버전별 스냅샷. RunStamp 는 BeginPlay 시각(yyyyMMdd-HHmmss) — meta 와 md 에서 실행을 구별한다. */
	FString SnapshotRunStamp;
	double LastSnapshotRealTime = 0.0;

	/** 스냅샷 최상위 폴더. 기본 <프로젝트>/Snapshots, -SHSnapshotRoot= 로 바꾼다(시험 실행용). */
	FString SnapshotRootDir;

	/** -SHResume 으로 이어 온 이전 실행들의 누적 학습 시간(초). 처음부터면 0. */
	double CumulativeBaseSeconds = 0.0;

	/** 이 실행이 저장한 스냅샷 폴더들. 상대 풀이 쓴다. */
	TArray<FString> RunSnapshotDirs;

	/** 날짜별 md 의 실행 머리말. 조립 때 만들고, 이 실행이 처음 쓰는 날짜 파일마다 한 번 쓴다. */
	FString RunLogHeader;
	FString LastLogDate;

	/** 이어서 학습하기(-SHResume=<스냅샷 폴더>). 비어 있으면 무작위 초기화에서 시작한다. */
	FString ResumeDir;

	/** 평가 모드 상태. */
	bool bEvalMode = false;
	bool bEvalRealtime = false;
	bool bEvalStarted = false;
	FString EvalSnapshotDir;
	UE::Learning::Agents::FGameSettingsState EvalGameSettingsState;

	/** 아레나별 등록 에이전트 id. 평가 모드에서 끝난 아레나만 리셋하는 데 쓴다. */
	TMap<TWeakObjectPtr<ASH_Arena>, TArray<int32>> ArenaAgentIds;

	/** 실행 전체 누적 라운드 결과 (CollectRounds 가 더한다). 평가 승률의 분모. */
	int32 TotalRounds = 0;
	int32 TotalWins[2] = { 0, 0 };
	int32 TotalTimeouts = 0;
	int32 TotalMutual = 0;

	/** 리포트 구간 동안 모은 라운드 기록. */
	TArray<FSHRoundRecord> PendingRounds;

	/** 역할별 구간 전적 (학습 정책 = 팀 0 기준 승). 인덱스 = ESHArenaRole. */
	int32 RoleRounds[3] = { 0, 0, 0 };
	int32 RoleLearnerWins[3] = { 0, 0, 0 };

	/** 역할별 구간 지표 (학습 정책 유닛만). 받은 피해에는 구역 피해가 들어 있다. */
	int64 RoleShots[3] = { 0, 0, 0 };
	int64 RoleHits[3] = { 0, 0, 0 };
	double RoleDamageTaken[3] = { 0.0, 0.0, 0.0 };
	double RoleDamageDealt[3] = { 0.0, 0.0, 0.0 };
	double RoleZoneDamage[3] = { 0.0, 0.0, 0.0 };
	double RoleRoundDuration[3] = { 0.0, 0.0, 0.0 };
	int64 RoleAliveDecisions[3] = { 0, 0, 0 };

	//~ 상대 혼합 상태 -----------------------------------------------------------
	FString EvalOpponentDir;
	TMap<TWeakObjectPtr<ASH_Arena>, ESHArenaRole> ArenaRoles;
	TMap<TWeakObjectPtr<ASH_Arena>, int32> LastRoundIndex;
	TMap<TWeakObjectPtr<ASH_Arena>, TArray<int32>> OpponentArenaAgentIds;
	TArray<FSHOpponentEntry> OpponentPool;

	/** 지금 상대. 풀 인덱스, INDEX_NONE 이면 "학습 정책 복사본". */
	int32 CurrentOpponent = INDEX_NONE;
	FString CurrentOpponentName = TEXT("(없음)");
	double LastOpponentSwapRealTime = 0.0;
	bool bHasOpponents = false;
	FRandomStream MixRandom;
};
