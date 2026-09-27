// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_ShooterInteractor.h"

#include "Combat/CombatStatsComponent.h"
#include "GameFramework/Controller.h"
#include "Gun.h"
#include "LearningAgentsActions.h"
#include "LearningAgentsObservations.h"
#include "SH_RLCharacter.h"
#include "SH_VisionComponent.h"

// ★ 태그를 한 곳에만 둔다. Specify 와 Gather/Get 이 같은 상수를 쓰게 만드는 것이 목적이다.
//   문자열을 직접 쓰면 오타가 "해시는 통과하는데 의미만 뒤바뀐" 상태를 만들고,
//   그건 보상 0 으로만 드러난다.
namespace
{
	// 관측
	const FName Tag_Root       (TEXT("Root"));
	const FName Tag_Fan        (TEXT("Fan"));
	const FName Tag_Ray        (TEXT("Ray"));
	const FName Tag_RayDepth   (TEXT("RayDepth"));
	const FName Tag_RayHostile (TEXT("RayHostile"));
	const FName Tag_Enemy      (TEXT("Enemy"));
	const FName Tag_EnemyBody  (TEXT("EnemyBody"));
	const FName Tag_EnemyAngle (TEXT("EnemyAngle"));
	const FName Tag_EnemyDepth (TEXT("EnemyDepth"));
	const FName Tag_Memory     (TEXT("Memory"));
	const FName Tag_TimeSince  (TEXT("TimeSince"));
	const FName Tag_LastAngle  (TEXT("LastAngle"));
	const FName Tag_Self       (TEXT("Self"));
	const FName Tag_HPRatio    (TEXT("HPRatio"));
	const FName Tag_CanFire    (TEXT("CanFire"));

	// 행동
	const FName Tag_ActRoot (TEXT("ActRoot"));
	const FName Tag_Turn    (TEXT("Turn"));
	const FName Tag_Move    (TEXT("Move"));
	const FName Tag_Fire    (TEXT("Fire"));

	/** 이동 인덱스 → 조준선 기준 상대 방향. 0 = 정지. */
	const FVector MoveDirections[SHRL::MoveBins] = {
		FVector::ZeroVector,
		FVector::ForwardVector,
		-FVector::ForwardVector,
		-FVector::RightVector,
		FVector::RightVector
	};
}

ASH_RLCharacter* USH_ShooterInteractor::ResolveUnit(const int32 AgentId) const
{
	// GetAgent 는 등록된 UObject 를 그대로 돌려준다. 우리는 폰을 등록한다 —
	// 풀이 재사용하는 것이 폰이고 컨트롤러는 원칙적으로 교체될 수 있기 때문.
	return Cast<ASH_RLCharacter>(const_cast<USH_ShooterInteractor*>(this)->GetAgent(AgentId));
}

//~ 관측 ----------------------------------------------------------------------

void USH_ShooterInteractor::SpecifyAgentObservation_Implementation(
	FLearningAgentsObservationSchemaElement& OutObservationSchemaElement,
	ULearningAgentsObservationSchema* InObservationSchema)
{
	using O = ULearningAgentsObservations;

	// --- 레이 하나의 모양 ---------------------------------------------------
	TMap<FName, FLearningAgentsObservationSchemaElement> Ray;
	Ray.Add(Tag_RayDepth,   O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_RayDepth));
	Ray.Add(Tag_RayHostile, O::SpecifyBoolObservation (InObservationSchema,     true, Tag_RayHostile));

	const FLearningAgentsObservationSchemaElement RaySchema =
		O::SpecifyStructObservation(InObservationSchema, Ray, true, Tag_Ray);

	// ★ Conv1d 가 아니라 StaticArray 다. 이유는 헤더 주석 참조 (파이썬 트레이너 미구현).
	const FLearningAgentsObservationSchemaElement Fan =
		O::SpecifyStaticArrayObservation(InObservationSchema, RaySchema, SHRL::RayCount, true, Tag_Fan);

	// --- 적: 보일 때만 존재하는 관측 ----------------------------------------
	// Optional 은 내부적으로 ExclusiveUnion{Null, Valid} + Encoding 이다
	// (LearningAgentsObservations.cpp:1342). JSON 타입 OrExclusive 로 내려가고
	// 파이썬이 처리한다.
	TMap<FName, FLearningAgentsObservationSchemaElement> Enemy;
	Enemy.Add(Tag_EnemyAngle, O::SpecifyAngleObservation(InObservationSchema,     true, Tag_EnemyAngle));
	Enemy.Add(Tag_EnemyDepth, O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_EnemyDepth));

	const FLearningAgentsObservationSchemaElement EnemyOptional =
		O::SpecifyOptionalObservation(InObservationSchema,
			O::SpecifyStructObservation(InObservationSchema, Enemy, true, Tag_EnemyBody),
			EnemyEncodingSize, true, Tag_Enemy);

	// --- 기억: 안 보일 때 쓰는 것 --------------------------------------------
	// TimeSince 를 0~1 로 떨어뜨린다. Scale 이 Vision 의 MemoryHorizonSeconds 와 같아야 한다.
	FLearningAgentsFloatObservationNormalizationSettings TimeNormalization;
	TimeNormalization.Scale = MemoryHorizonSeconds;

	TMap<FName, FLearningAgentsObservationSchemaElement> Memory;
	Memory.Add(Tag_TimeSince, O::SpecifyFloatObservation(InObservationSchema, TimeNormalization, true, Tag_TimeSince));
	Memory.Add(Tag_LastAngle, O::SpecifyAngleObservation(InObservationSchema,                    true, Tag_LastAngle));

	// --- 자신 ---------------------------------------------------------------
	TMap<FName, FLearningAgentsObservationSchemaElement> Self;
	Self.Add(Tag_HPRatio, O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_HPRatio));
	Self.Add(Tag_CanFire, O::SpecifyBoolObservation (InObservationSchema,     true, Tag_CanFire));

	TMap<FName, FLearningAgentsObservationSchemaElement> Root;
	Root.Add(Tag_Fan,    Fan);
	Root.Add(Tag_Enemy,  EnemyOptional);
	Root.Add(Tag_Memory, O::SpecifyStructObservation(InObservationSchema, Memory, true, Tag_Memory));
	Root.Add(Tag_Self,   O::SpecifyStructObservation(InObservationSchema, Self,   true, Tag_Self));

	OutObservationSchemaElement = O::SpecifyStructObservation(InObservationSchema, Root, true, Tag_Root);
}

void USH_ShooterInteractor::GatherAgentObservation_Implementation(
	FLearningAgentsObservationObjectElement& OutObservationObjectElement,
	ULearningAgentsObservationObject* InObservationObject,
	const int32 AgentId)
{
	using O = ULearningAgentsObservations;

	// ★ 트리를 만드는 코드 경로는 **하나만** 둔다.
	//   죽은/비활성 에이전트도 RunTraining 의 Gather 루프에 포함되므로 스키마와 똑같은
	//   모양을 내야 한다. 여기서 조기 반환해 빈 Struct 를 내면 벡터 크기가 달라진다.
	//   그래서 구조는 고정하고 "값만" 갈라 쓴다.
	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	USH_VisionComponent* Vision = Unit ? Unit->Vision.Get() : nullptr;
	UCombatStatsComponent* Stats = Unit ? Unit->CombatStats.Get() : nullptr;

	const bool bLive = Unit && Vision && Stats && Unit->bEntityEnabled && Stats->IsAlive();

	if (bLive)
	{
		// 결정마다 한 번. 같은 프레임의 보상 계산과 결과를 공유한다
		// (USH_VisionComponent::Scan 의 프레임 가드).
		Vision->Scan();
	}

	// --- 부채꼴 -------------------------------------------------------------
	TArray<FLearningAgentsObservationObjectElement> RayElements;
	RayElements.Reserve(SHRL::RayCount);

	const TArray<FSHVisionRay>* Rays = bLive ? &Vision->GetRays() : nullptr;
	for (int32 Index = 0; Index < SHRL::RayCount; ++Index)
	{
		// 죽었으면 "아무것도 없는 시야" — Depth 1(끝까지 트임), 적대 없음.
		const bool  bHostile = (Rays && Rays->IsValidIndex(Index)) ? (*Rays)[Index].bHostile : false;
		const float Depth01  = (Rays && Rays->IsValidIndex(Index)) ? (*Rays)[Index].Depth01  : 1.0f;

		TMap<FName, FLearningAgentsObservationObjectElement> Ray;
		Ray.Add(Tag_RayDepth,   O::MakeFloatObservation(InObservationObject, Depth01,  true, Tag_RayDepth));
		Ray.Add(Tag_RayHostile, O::MakeBoolObservation (InObservationObject, bHostile, true, Tag_RayHostile));
		RayElements.Add(O::MakeStructObservation(InObservationObject, Ray, true, Tag_Ray));
	}

	const FLearningAgentsObservationObjectElement Fan =
		O::MakeStaticArrayObservation(InObservationObject, RayElements, true, Tag_Fan);

	// --- 적: 보일 때만 Valid -------------------------------------------------
	const bool bEnemyVisible = bLive && Vision->IsHostileVisible();

	TMap<FName, FLearningAgentsObservationObjectElement> Enemy;
	// ★ MakeAngleObservation 은 **도(degree)** 를 받는다. 라디안 버전은
	//   MakeAngleObservationRadians 로 따로 있다 (LearningAgentsObservations.h:1727 / 1751).
	Enemy.Add(Tag_EnemyAngle, O::MakeAngleObservation(InObservationObject,
		bEnemyVisible ? Vision->GetHostileAngleDeg() : 0.0f, 0.0f, true, Tag_EnemyAngle));
	Enemy.Add(Tag_EnemyDepth, O::MakeFloatObservation(InObservationObject,
		bEnemyVisible ? Vision->GetHostileDepth01() : 1.0f, true, Tag_EnemyDepth));

	const FLearningAgentsObservationObjectElement EnemyOptional =
		O::MakeOptionalObservation(InObservationObject,
			O::MakeStructObservation(InObservationObject, Enemy, true, Tag_EnemyBody),
			bEnemyVisible ? ELearningAgentsOptionalObservation::Valid
			              : ELearningAgentsOptionalObservation::Null,
			true, Tag_Enemy);

	// --- 기억 ---------------------------------------------------------------
	TMap<FName, FLearningAgentsObservationObjectElement> Memory;
	Memory.Add(Tag_TimeSince, O::MakeFloatObservation(InObservationObject,
		bLive ? Vision->GetTimeSinceSeen() : MemoryHorizonSeconds, true, Tag_TimeSince));
	Memory.Add(Tag_LastAngle, O::MakeAngleObservation(InObservationObject,
		bLive ? Vision->GetLastSeenAngleDeg() : 0.0f, 0.0f, true, Tag_LastAngle));

	// --- 자신 ---------------------------------------------------------------
	TMap<FName, FLearningAgentsObservationObjectElement> Self;
	Self.Add(Tag_HPRatio, O::MakeFloatObservation(InObservationObject,
		Stats ? Stats->GetHPRatio() : 0.0f, true, Tag_HPRatio));
	Self.Add(Tag_CanFire, O::MakeBoolObservation(InObservationObject,
		bLive && Unit->gun && Unit->gun->CanFire(), true, Tag_CanFire));

	TMap<FName, FLearningAgentsObservationObjectElement> Root;
	Root.Add(Tag_Fan,    Fan);
	Root.Add(Tag_Enemy,  EnemyOptional);
	Root.Add(Tag_Memory, O::MakeStructObservation(InObservationObject, Memory, true, Tag_Memory));
	Root.Add(Tag_Self,   O::MakeStructObservation(InObservationObject, Self,   true, Tag_Self));

	OutObservationObjectElement = O::MakeStructObservation(InObservationObject, Root, true, Tag_Root);
}

//~ 행동 ----------------------------------------------------------------------

void USH_ShooterInteractor::SpecifyAgentAction_Implementation(
	FLearningAgentsActionSchemaElement& OutActionSchemaElement,
	ULearningAgentsActionSchema* InActionSchema)
{
	using A = ULearningAgentsActions;

	TMap<FName, FLearningAgentsActionSchemaElement> Action;

	// ★ Turn 은 **각속도**다. 절대 각도를 내게 하면 각속도 제약이 학습에서 사라지고
	//   "제자리에서 훑어보기" 도 표현하기 어려워진다.
	Action.Add(Tag_Turn, A::SpecifyExclusiveDiscreteAction(
		InActionSchema, SHRL::TurnBins, TArray<float>(), true, Tag_Turn));
	Action.Add(Tag_Move, A::SpecifyExclusiveDiscreteAction(
		InActionSchema, SHRL::MoveBins, TArray<float>(), true, Tag_Move));

	// 사전 확률 0.3 — 초기 정책이 어느 정도는 쏘게 만든다. 너무 낮으면 "명중" 경험을
	// 한 번도 만들지 못해 보상 신호 자체가 생기지 않는다.
	Action.Add(Tag_Fire, A::SpecifyBoolAction(InActionSchema, 0.3f, true, Tag_Fire));

	OutActionSchemaElement = A::SpecifyStructAction(InActionSchema, Action, true, Tag_ActRoot);
}

void USH_ShooterInteractor::PerformAgentAction_Implementation(
	const ULearningAgentsActionObject* InActionObject,
	const FLearningAgentsActionObjectElement& InActionObjectElement,
	const int32 AgentId)
{
	using A = ULearningAgentsActions;

	ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	if (!Unit)
	{
		return;
	}

	// 죽었거나 풀에서 빠졌으면 의도를 비우고 나간다.
	// ★ 행동 자체는 이미 저장됐으므로 분포에는 영향이 없다 — 실행만 건너뛴다.
	if (!Unit->bEntityEnabled || !Unit->CombatStats || !Unit->CombatStats->IsAlive())
	{
		Unit->ClearPendingInput();
		return;
	}

	TMap<FName, FLearningAgentsActionObjectElement> Action;
	if (!A::GetStructAction(Action, InActionObject, InActionObjectElement, true, Tag_ActRoot))
	{
		Unit->ClearPendingInput();
		return;
	}

	const FLearningAgentsActionObjectElement* TurnElement = Action.Find(Tag_Turn);
	const FLearningAgentsActionObjectElement* MoveElement = Action.Find(Tag_Move);
	const FLearningAgentsActionObjectElement* FireElement = Action.Find(Tag_Fire);
	if (!TurnElement || !MoveElement || !FireElement)
	{
		// 태그가 어긋났다. 크게 알린다 — 조용히 넘기면 보상 0 으로만 드러난다.
		UE_LOG(LogTemp, Error,
			TEXT("[SH_ShooterInteractor] 행동 태그를 찾을 수 없다 (Turn=%s Move=%s Fire=%s). ")
			TEXT("Specify 와 Get 의 태그가 어긋났다."),
			TurnElement ? TEXT("O") : TEXT("X"),
			MoveElement ? TEXT("O") : TEXT("X"),
			FireElement ? TEXT("O") : TEXT("X"));
		Unit->ClearPendingInput();
		return;
	}

	int32 TurnIndex = SHRL::TurnBins / 2;   // 중앙 = 회전 없음
	int32 MoveIndex = 0;                    // 0 = 정지
	bool  bFire = false;

	A::GetExclusiveDiscreteAction(TurnIndex, InActionObject, *TurnElement, true, Tag_Turn);
	A::GetExclusiveDiscreteAction(MoveIndex, InActionObject, *MoveElement, true, Tag_Move);
	A::GetBoolAction             (bFire,     InActionObject, *FireElement, true, Tag_Fire);

	// --- 조준: 각속도만 적어 둔다. 실제 적용은 폰의 Tick 이다 -------------------
	// ★ 여기서 직접 SetControlRotation 하면 20Hz 로만 돌아 프레임률에 따라 실효
	//   각속도가 달라진다. ASH_RLCharacter::Tick 이 DeltaTime 을 곱해 매 프레임 적용한다.
	const float Half = 0.5f * static_cast<float>(SHRL::TurnBins - 1);         // 5 → 2.0
	const float TurnScale = (static_cast<float>(TurnIndex) - Half) / Half;    // -1 … +1
	Unit->PendingTurnRateDeg = TurnScale * Unit->MaxTurnRateDeg;

	// --- 이동: 조준선 기준 상대 방향 -------------------------------------------
	const FVector& LocalDirection = MoveDirections[FMath::Clamp(MoveIndex, 0, SHRL::MoveBins - 1)];
	if (LocalDirection.IsNearlyZero())
	{
		Unit->PendingMoveInput = FVector::ZeroVector;
	}
	else
	{
		// 몸통이 아니라 컨트롤 회전 기준이다 — 조준선과 이동이 같은 기준을 써야
		// "전진" 이 "보고 있는 쪽" 을 뜻한다. 몸통은 컨트롤 회전을 뒤늦게 따라온다.
		const FRotator YawOnly(0.0f, Unit->GetControlRotation().Yaw, 0.0f);
		Unit->PendingMoveInput = YawOnly.RotateVector(LocalDirection);
	}

	// --- 사격 ---------------------------------------------------------------
	// 래칭하지 않는다. FireInterval(0.15s) 이 결정 주기(0.05s) 보다 길어서
	// 쿨다운이 자연스럽게 발사율을 제한한다.
	if (bFire && Unit->gun)
	{
		Unit->gun->PullTrigger();
	}
}

void USH_ShooterInteractor::MakeAgentActionModifier_Implementation(
	FLearningAgentsActionModifierElement& OutActionModifierElement,
	ULearningAgentsActionModifier* InActionModifier,
	const ULearningAgentsObservationObject* /*InObservationObject*/,
	const FLearningAgentsObservationObjectElement& /*InObservationObjectElement*/,
	const int32 AgentId)
{
	using A = ULearningAgentsActions;

	const ASH_RLCharacter* Unit = ResolveUnit(AgentId);
	const bool bCanFire = Unit && Unit->gun && Unit->gun->CanFire();

	TMap<FName, FLearningAgentsActionModifierElement> Modifiers;

	// 회전과 이동은 막지 않는다 — 물리적으로 불가능한 선택지가 없다.
	Modifiers.Add(Tag_Turn, A::MakeExclusiveDiscreteActionModifier(InActionModifier, {}, Tag_Turn));
	Modifiers.Add(Tag_Move, A::MakeExclusiveDiscreteActionModifier(InActionModifier, {}, Tag_Move));

	// 쿨다운 중이면 발사를 물리적으로 막는다.
	// ★ 조준 오차로는 **절대** 마스킹하지 않는다 — 그러면 빗맞히는 게 나쁘다는 걸
	//   영영 못 배우고, "언제 쏠지" 를 정책이 스스로 판단하지 못한다.
	Modifiers.Add(Tag_Fire, bCanFire
		? A::MakeNullActionModifier(InActionModifier, Tag_Fire)
		: A::MakeBoolActionModifier(InActionModifier, false, Tag_Fire));

	OutActionModifierElement = A::MakeStructActionModifier(InActionModifier, Modifiers, Tag_ActRoot);
}
