// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsInteractor.h"
#include "SH_ShooterInteractor.generated.h"

class ASH_RLCharacter;

/** 스키마 치수. ★ 관측/행동 스키마는 학습 시작 후 절대 바꿀 수 없다 (호환성 해시). */
namespace SHRL
{
	/** 부채꼴 레이 개수. ★ USH_VisionComponent::RayCount 와 반드시 같아야 한다.
	 *  다르면 관측 벡터 크기가 어긋난다 — Specify 에서 검사한다. */
	constexpr int32 RayCount = 16;

	/**
	 *  조준 각속도 구간 수. 배율은 .cpp 의 TurnRateFractions (MaxTurnRateDeg 180 기준
	 *  −180, −45, −10, 0, +10, +45, +180 °/s).
	 *
	 *  ★ 5구간(±90, ±180)이던 때는 결정당 최소 회전이 90°/s × 0.05초 = 4.5° 였다. 2,400 cm 거리의
	 *  적 캡슐 폭은 약 2.0° 라 멀리 있는 적에게 조준을 맞출 수 없었다. ±10°/s 면 결정당 0.5°.
	 *  ★ 중앙(TurnBins/2)이 0 이어야 한다 — 사망 마스크가 중앙과 그 왼쪽을 남긴다.
	 */
	constexpr int32 TurnBins = 7;

	/** 이동 방향 수: 정지 / 전진 / 후진 / 좌 / 우. */
	constexpr int32 MoveBins = 5;

	/** 보이는 적 집합의 최대 크기 = 적 팀 인원. 아레나 UnitsPerTeam 보다 작으면 넘치는 적이 잘린다. */
	constexpr int32 MaxEnemies = 10;

	/** 아군 집합의 최대 크기 = 팀 인원 − 1(자신). */
	constexpr int32 MaxAllies = 9;
}

/**
 *  시야 제한 사격 정책의 관측/행동 스키마.
 *
 *  관측 Root {
 *    Fan:     StaticArray(16 x Struct{ RayDepth:Float, RayHostile:Bool, RayAlly:Bool })
 *    Enemies: Set(최대 10, 어텐션){ EnemyAngle:Angle, EnemyDepth:Float, EnemyHP:Float, EnemyFacing:Angle }
 *    Allies:  Set(최대 9, 어텐션){ AllyAngle:Angle, AllyDistance:Float, AllyHP:Float }
 *    Memory:  Struct{ TimeSince:Float(Scale = MemoryHorizon), LastAngle:Angle }
 *    Hit:     Struct{ TimeSinceHit:Float(Scale = HitMemoryHorizon), HitAngle:Angle }
 *    Team:    Struct{ AlliesAlive, EnemiesAlive, RoundProgress, ZoneRadius, ZoneMargin }  (전부 Float, 0~1 근처)
 *    Self:    Struct{ HPRatio:Float, CanFire:Bool }
 *  }
 *  행동 Root { Turn:ExclusiveDiscrete(7), Move:ExclusiveDiscrete(5), Fire:Bool(prior 0.3) }
 *
 *  ★ 2026-10-08 Phase 2 스키마 (10대10 인지). 이전 스키마로 저장한 스냅샷은 더 이상 불러올 수 없다.
 *   - Enemy(Optional, 가장 가까운 적 하나) → **Enemies(Set)**. 보이는 적 전원을 직접 트레이스로
 *     확인해 담는다(USH_VisionComponent::ScanEnemies). 스크립트 봇의 CanSeeActor 와 같은 판정이다.
 *   - **Allies(Set)**: 아군 위치·HP. 사용자 결정(2026-10-02)에 따라 아군 정보는 시야와 무관하게 준다.
 *   - **RayAlly**: 레이가 아군에 닿았는가. 아군 캡슐도 총알을 막으므로 벽과 구별해야 한다.
 *   - Turn 5 → 7구간 (SHRL::TurnBins 주석).
 *
 *  ★ 적 좌표를 주지 않는다. 보이는 적만 집합에 들어가고, 집합이 비면 "아무도 안 보인다" 다.
 *  빈 집합은 어텐션 집계가 0 벡터 + 원소 수 0 을 내므로 안전하다
 *  (nne_runtime_basic_cpu_pytorch.py AggregateSet 의 _softmax_plus_one_masked).
 *
 *  ★★ Hit 가 **후방 위협을 담는 유일한 관측**이다 (2026-10-01 추가).
 *  Fan 은 FOVDegrees(90도) 안만 보므로 뒤에서 쏘는 적은 관측에 전혀 나타나지 않는다.
 *  그 상태에서는 엄폐가 원리적으로 학습 불가능하다 — 누가 자기를 쏘는지 모르는데
 *  그 방향을 피할 수 없다. 실측으로도 받은 피해가 결정 1k당 958 → 1041 로 늘었다.
 *
 *  ★ Hit 를 Optional 로 하지 않은 이유. "한 번도 안 맞았다" 는
 *  TimeSinceHit = HitMemoryHorizon (포화) 으로 표현된다 — Memory 그룹이 쓰는 방식과 같고,
 *  Optional 은 유니온 오버헤드로 2 float 을 더 쓴다.
 *
 *  ★★ Conv1d 를 쓰지 않는 이유 (계획에서 변경).
 *  SpecifyConv1dObservation 은 C++ 쪽에는 있지만 **파이썬 트레이너가 구현하지 않았다.**
 *  train_common.py 의 스키마 → torch 모듈 디스패치에 'Conv1d' 분기가 없고
 *  (obs_type 분기는 And/OrExclusive/OrInclusive/Array/Set/Encoding 뿐),
 *  각 디스패처는 else: raise Exception('Not Implemented') 로 끝난다.
 *  'Conv1d' 는 "정규화 없음 / 분포 없음" 제외 목록 네 곳에만 이름이 있다.
 *  StaticArray 는 JSON 타입 'Array' 로 내려가고 파이썬이 완전히 처리한다.
 *
 *  ★ Tag 문자열이 Specify 와 Gather/Get 사이에서 어긋나면 **해시는 통과하면서 의미만
 *  뒤바뀐다** (보상 0, 에러 없음). data-config.json 에 태그 이름이 전혀 실리지 않아
 *  파이썬 쪽도 못 잡는다. 그래서 태그를 .cpp 의 한 곳에만 둔다.
 */
UCLASS()
class HAI_PROJECT_TEAM5_API USH_ShooterInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()

public:
	/** 집합(Enemies/Allies) 어텐션 설정. 스키마에 굳어지므로 나중에 못 바꾼다.
	 *  집합 하나의 인코딩 크기 = AttentionHeadNum x ValueEncodingSize + 1(원소 수). */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Schema", meta = (ClampMin = "1"))
	int32 SetAttentionEncodingSize = 16;

	UPROPERTY(EditAnywhere, Category = "SH|RL|Schema", meta = (ClampMin = "1"))
	int32 SetAttentionHeadNum = 4;

	UPROPERTY(EditAnywhere, Category = "SH|RL|Schema", meta = (ClampMin = "1"))
	int32 SetValueEncodingSize = 16;

	/**
	 *  시야 기억 지평(초). TimeSince 관측의 Float 정규화 Scale 로 쓴다.
	 *
	 *  ★ 유닛의 USH_VisionComponent::MemoryHorizonSeconds 와 같아야 0~1 로 떨어진다.
	 *  Specify 시점에는 에이전트가 아직 없을 수 있어 여기에 따로 들고 있고,
	 *  ASH_CombatTrainer::Assemble 이 유닛에서 읽어 맞춰 준다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Schema", meta = (ClampMin = "0.1"))
	float MemoryHorizonSeconds = 3.0f;

	/**
	 *  피격 기억 지평(초). TimeSinceHit 관측의 Float 정규화 Scale 로 쓴다.
	 *
	 *  ★ ASH_RLCharacter::HitMemoryHorizonSeconds 와 같아야 0~1 로 떨어진다.
	 *  ASH_CombatTrainer::Assemble 이 유닛에서 읽어 맞춰 준다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Schema", meta = (ClampMin = "0.1"))
	float HitMemoryHorizonSeconds = 5.0f;

	//~ ULearningAgentsInteractor ---------------------------------------------

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

	virtual void MakeAgentActionModifier_Implementation(
		FLearningAgentsActionModifierElement& OutActionModifierElement,
		ULearningAgentsActionModifier* InActionModifier,
		const ULearningAgentsObservationObject* InObservationObject,
		const FLearningAgentsObservationObjectElement& InObservationObjectElement,
		const int32 AgentId) override;

	//~ 진단 지표 --------------------------------------------------------------

	/**
	 *  행동 선택 통계를 담는 구간 누적값.
	 *
	 *  ★ 왜 필요한가. 명중률만으로는 **이동과 발사 타이밍이 학습됐는지 알 수 없다.**
	 *  이동 분포가 균등(각 20%)에 가까우면 정책은 이동을 무작위로 뽑고 있는 것이고,
	 *  그때 "이동을 학습했다" 고 말할 근거가 없다.
	 *
	 *  ★ 발사는 비율을 두 번 나눠야 의미가 생긴다. AGun::FireInterval(0.15초)이
	 *  결정 주기(0.05초)보다 길어서 **한 발이 최소 3스텝을 소비한다** —
	 *  즉 전체 스텝 대비 발사율의 이론 최대값은 33.3% 다.
	 *  "스텝의 22% 에서 쐈다" 는 "쏠 수 있었던 기회의 66% 를 썼다" 와 같은 말이고,
	 *  뒤쪽이 정책의 선택을 나타낸다.
	 */
	struct FActionStats
	{
		/** 이동 구간별 선택 횟수. 인덱스는 MoveDirections 와 같다
		 *  (0 정지 / 1 전진 / 2 후진 / 3 좌 / 4 우). */
		int64 MoveCounts[SHRL::MoveBins] = {};

		/** 쿨다운이 끝나 **쏠 수 있었던** 결정 횟수. */
		int64 FireOpportunities = 0;

		/** 그 중 실제로 쏜 횟수. */
		int64 FiresUsed = 0;

		/** 행동을 실제로 실행한 결정 횟수 (살아 있는 에이전트만). 분모다. */
		int64 Decisions = 0;
	};

	/** 통계를 읽고 **비운다**. ASH_CombatTrainer::Report 가 구간마다 부른다. */
	void ConsumeActionStats(FActionStats& OutStats);

protected:
	/** 에이전트로 등록된 것은 폰이다. 풀이 재사용하는 것이 폰이기 때문. */
	ASH_RLCharacter* ResolveUnit(const int32 AgentId) const;

private:
	FActionStats ActionStats;
};
