// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SH_RLDirector.generated.h"

class ALearningAgentsGymsManager;
class ASH_Arena;

/**
 *  학습 실행을 조립하는 디렉터. 레벨에 하나만 놓는다.
 *
 *  ★ ALearningAgentsGymsManager::Start() 는 아무도 대신 불러주지 않는다.
 *  안 부르면 아레나가 0개인 채로 조용히 진행되고, 에러도 경고도 없다.
 *
 *  지금은 Start() 호출과 배치 검증만 한다. Manager/Interactor/Policy/Critic/
 *  TrainingEnvironment/PPOTrainer 조립과 20Hz 결정 루프는 이후 단계에서 여기에 붙는다.
 *  조립 순서는 계획 Part 7 참조 — 리스너가 생기기 전에 추가된 에이전트는
 *  그 리스너에게 영원히 보이지 않는다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API ASH_RLDirector : public AActor
{
	GENERATED_BODY()

public:
	ASH_RLDirector();

	/** 비워 두면 BeginPlay 에서 레벨의 GymsManager 를 자동으로 찾는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SH|RL")
	TObjectPtr<ALearningAgentsGymsManager> GymsManager;

	/** 스폰된 아레나의 배치를 로그로 검증한다 (Gate 2 의 격자·겹침·원점 항목). */
	UPROPERTY(EditAnywhere, Category = "SH|Debug")
	bool bVerifyArenaLayout = true;

	//~ 렌더링 ----------------------------------------------------------------

	/**
	 *  학습용 렌더 설정을 적용한다.
	 *
	 *  ★ DefaultEngine.ini 를 고치지 않는 이유: 출고 빌드와 공유하는 파일이라
	 *  학습용 설정이 게임에 그대로 딸려 나간다. 콘솔 오버라이드는 이 세션에만 적용된다.
	 *
	 *  기본값 false — 지금은 화면을 보며 디버깅하는 단계다.
	 *  실제 학습 실행에서는 켤 것. 처리량 리포트가 적용 여부를 함께 찍는다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Rendering")
	bool bApplyTrainingRenderSettings = false;

	/** 적용할 콘솔 명령. 코드를 고치지 않고 레벨에서 조정할 수 있게 열어 둔다. */
	UPROPERTY(EditAnywhere, Category = "SH|Rendering")
	TArray<FString> TrainingRenderCommands;

	//~ 처리량 ----------------------------------------------------------------

	/** 주기적으로 처리량을 로그로 찍는다. Gate 2 의 누출·풀링·성능 항목을 한 번에 본다. */
	UPROPERTY(EditAnywhere, Category = "SH|Throughput")
	bool bReportThroughput = true;

	UPROPERTY(EditAnywhere, Category = "SH|Throughput", meta = (ClampMin = "1.0"))
	float ThroughputReportInterval = 10.0f;

	/** 처리량 환산에 쓰는 결정 주기(Hz). 계획값은 20 — 실제 RL 루프와 맞춰 둘 것. */
	UPROPERTY(EditAnywhere, Category = "SH|Throughput", meta = (ClampMin = "1.0"))
	float DecisionRateHz = 20.0f;

	/** PPO 이터레이션 하나에 필요한 스텝 수. 소요 시간 환산에 쓴다. */
	UPROPERTY(EditAnywhere, Category = "SH|Throughput", meta = (ClampMin = "1"))
	int32 StepsPerIteration = 16384;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** 아레나 개수 · 격자 간격 · 월드 원점 중복을 검사해 로그로 남긴다. */
	void VerifyArenaLayout() const;

	void ApplyTrainingRenderSettings();
	void ReportThroughput(double RealElapsed, float SimElapsed, int32 Frames) const;

private:
	double LastReportRealTime = 0.0;
	float LastReportSimTime = 0.0f;
	int32 FramesSinceReport = 0;

	/** 직전 리포트의 유닛 액터 수. 늘어나면 풀링이 깨진 것이다. */
	mutable int32 PreviousUnitActorCount = -1;
};
