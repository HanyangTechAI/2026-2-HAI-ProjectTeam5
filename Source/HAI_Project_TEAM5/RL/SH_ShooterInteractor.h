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

	/** 조준 각속도 구간 수. 인덱스 i → (i - TurnBins/2) / (TurnBins/2) 배율. */
	constexpr int32 TurnBins = 5;   // -1, -0.5, 0, +0.5, +1

	/** 이동 방향 수: 정지 / 전진 / 후진 / 좌 / 우. */
	constexpr int32 MoveBins = 5;
}

/**
 *  시야 제한 사격 정책의 관측/행동 스키마.
 *
 *  관측 Root {
 *    Fan:    StaticArray(16 x Struct{ RayDepth:Float, RayHostile:Bool })
 *    Enemy:  Optional( Struct{ EnemyAngle:Angle, EnemyDepth:Float }, EncodingSize 32 )
 *    Memory: Struct{ TimeSince:Float(Scale = MemoryHorizon), LastAngle:Angle }
 *    Self:   Struct{ HPRatio:Float, CanFire:Bool }
 *  }
 *  행동 Root { Turn:ExclusiveDiscrete(5), Move:ExclusiveDiscrete(5), Fire:Bool(prior 0.3) }
 *
 *  ★ 적 좌표를 주지 않는다. Optional 이 "지금 적이 보이는가" 를 스키마 차원에서 표현한다 —
 *  bVisible 불리언 + 0 채우기 같은 수작업이 필요 없다.
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
	/** Optional(적) 의 인코딩 크기. 스키마에 굳어지므로 나중에 못 바꾼다. */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Schema", meta = (ClampMin = "1"))
	int32 EnemyEncodingSize = 32;

	/**
	 *  시야 기억 지평(초). TimeSince 관측의 Float 정규화 Scale 로 쓴다.
	 *
	 *  ★ 유닛의 USH_VisionComponent::MemoryHorizonSeconds 와 같아야 0~1 로 떨어진다.
	 *  Specify 시점에는 에이전트가 아직 없을 수 있어 여기에 따로 들고 있고,
	 *  ASH_CombatTrainer::Assemble 이 유닛에서 읽어 맞춰 준다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|RL|Schema", meta = (ClampMin = "0.1"))
	float MemoryHorizonSeconds = 3.0f;

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

protected:
	/** 에이전트로 등록된 것은 폰이다. 풀이 재사용하는 것이 폰이기 때문. */
	ASH_RLCharacter* ResolveUnit(const int32 AgentId) const;
};
