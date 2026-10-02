// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_ShooterInteractor.h"

#include "Combat/CombatStatsComponent.h"
#include "GameFramework/Controller.h"
#include "Gun.h"
#include "LearningAgentsActions.h"
#include "LearningAgentsObservations.h"
#include "SH_Arena.h"
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
	const FName Tag_Hit        (TEXT("Hit"));
	const FName Tag_TimeSinceHit(TEXT("TimeSinceHit"));
	const FName Tag_HitAngle   (TEXT("HitAngle"));
	const FName Tag_TimeSince  (TEXT("TimeSince"));
	const FName Tag_LastAngle  (TEXT("LastAngle"));
	const FName Tag_Team         (TEXT("Team"));
	const FName Tag_AlliesAlive  (TEXT("AlliesAlive"));
	const FName Tag_EnemiesAlive (TEXT("EnemiesAlive"));
	const FName Tag_RoundProgress(TEXT("RoundProgress"));
	const FName Tag_ZoneRadius   (TEXT("ZoneRadius"));
	const FName Tag_ZoneMargin   (TEXT("ZoneMargin"));
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

void USH_ShooterInteractor::ConsumeActionStats(FActionStats& OutStats)
{
	OutStats = ActionStats;
	ActionStats = FActionStats();
}

namespace
{
	/**
	 *  관측값이 유한하지 않으면 0 으로 바꾸고 기록한다.
	 *
	 *  ★ 2026-10-02 크래시 조사용. 엔진의 이산 행동 샘플러는 로짓이 NaN 이면
	 *  "MaxIdx != INDEX_NONE" 체크에 걸려 프로세스가 죽는다. 원인이 관측이라면 여기서 막히고
	 *  어느 필드인지 남는다. 로그는 처음 50건만.
	 */
	float SanitizeObservation(const float Value, const TCHAR* Field, const int32 AgentId)
	{
		if (FMath::IsFinite(Value))
		{
			return Value;
		}
		static int32 Reported = 0;
		if (Reported < 50)
		{
			++Reported;
			UE_LOG(LogTemp, Error,
				TEXT("[SH_ShooterInteractor] 비유한 관측값 — 에이전트 %d, 필드 %s, 값 %f. 0 으로 바꿔 넣는다."),
				AgentId, Field, Value);
		}
		return 0.0f;
	}
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

	// --- 피격 기억: 후방 위협을 담는 유일한 관측 ------------------------------
	// Scale 이 ASH_RLCharacter::HitMemoryHorizonSeconds 와 같아야 0~1 로 떨어진다.
	FLearningAgentsFloatObservationNormalizationSettings HitTimeNormalization;
	HitTimeNormalization.Scale = HitMemoryHorizonSeconds;

	TMap<FName, FLearningAgentsObservationSchemaElement> Hit;
	Hit.Add(Tag_TimeSinceHit, O::SpecifyFloatObservation(InObservationSchema, HitTimeNormalization, true, Tag_TimeSinceHit));
	Hit.Add(Tag_HitAngle,     O::SpecifyAngleObservation(InObservationSchema,                       true, Tag_HitAngle));

	// --- 자신 ---------------------------------------------------------------
	TMap<FName, FLearningAgentsObservationSchemaElement> Self;
	Self.Add(Tag_HPRatio, O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_HPRatio));
	Self.Add(Tag_CanFire, O::SpecifyBoolObservation (InObservationSchema,     true, Tag_CanFire));

	// --- 팀 · 라운드: 죽은 에이전트에게도 의미가 있는 유일한 관측 -----------------
	// ★ 사망 마스크로 죽은 에이전트가 라운드 끝까지 남으므로, 크리틱이 그 구간의 가치(팀 승패)를
	//   예측할 근거가 여기뿐이다. 값은 전부 0~1 근처로 미리 정규화해서 넣는다.
	TMap<FName, FLearningAgentsObservationSchemaElement> Team;
	Team.Add(Tag_AlliesAlive,   O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_AlliesAlive));
	Team.Add(Tag_EnemiesAlive,  O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_EnemiesAlive));
	Team.Add(Tag_RoundProgress, O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_RoundProgress));
	Team.Add(Tag_ZoneRadius,    O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_ZoneRadius));
	Team.Add(Tag_ZoneMargin,    O::SpecifyFloatObservation(InObservationSchema, {}, true, Tag_ZoneMargin));

	TMap<FName, FLearningAgentsObservationSchemaElement> Root;
	Root.Add(Tag_Fan,    Fan);
	Root.Add(Tag_Enemy,  EnemyOptional);
	Root.Add(Tag_Memory, O::SpecifyStructObservation(InObservationSchema, Memory, true, Tag_Memory));
	Root.Add(Tag_Hit,    O::SpecifyStructObservation(InObservationSchema, Hit,    true, Tag_Hit));
	Root.Add(Tag_Team,   O::SpecifyStructObservation(InObservationSchema, Team,   true, Tag_Team));
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
		Ray.Add(Tag_RayDepth,   O::MakeFloatObservation(InObservationObject, SanitizeObservation(Depth01, TEXT("RayDepth"), AgentId), true, Tag_RayDepth));
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
		SanitizeObservation(bEnemyVisible ? Vision->GetHostileAngleDeg() : 0.0f, TEXT("EnemyAngle"), AgentId), 0.0f, true, Tag_EnemyAngle));
	Enemy.Add(Tag_EnemyDepth, O::MakeFloatObservation(InObservationObject,
		SanitizeObservation(bEnemyVisible ? Vision->GetHostileDepth01() : 1.0f, TEXT("EnemyDepth"), AgentId), true, Tag_EnemyDepth));

	const FLearningAgentsObservationObjectElement EnemyOptional =
		O::MakeOptionalObservation(InObservationObject,
			O::MakeStructObservation(InObservationObject, Enemy, true, Tag_EnemyBody),
			bEnemyVisible ? ELearningAgentsOptionalObservation::Valid
			              : ELearningAgentsOptionalObservation::Null,
			true, Tag_Enemy);

	// --- 기억 ---------------------------------------------------------------
	TMap<FName, FLearningAgentsObservationObjectElement> Memory;
	Memory.Add(Tag_TimeSince, O::MakeFloatObservation(InObservationObject,
		SanitizeObservation(bLive ? Vision->GetTimeSinceSeen() : MemoryHorizonSeconds, TEXT("TimeSince"), AgentId), true, Tag_TimeSince));
	Memory.Add(Tag_LastAngle, O::MakeAngleObservation(InObservationObject,
		SanitizeObservation(bLive ? Vision->GetLastSeenAngleDeg() : 0.0f, TEXT("LastAngle"), AgentId), 0.0f, true, Tag_LastAngle));

	// --- 피격 기억 ----------------------------------------------------------
	// 죽었으면 "오래 전에 맞았고 방향 없음" — 지평값 + 0 도. 0 초를 넣으면
	// "방금 정면에서 맞았다" 가 되어 반대 뜻이 된다.
	TMap<FName, FLearningAgentsObservationObjectElement> Hit;
	Hit.Add(Tag_TimeSinceHit, O::MakeFloatObservation(InObservationObject,
		SanitizeObservation(bLive ? Unit->GetTimeSinceLastHit() : HitMemoryHorizonSeconds, TEXT("TimeSinceHit"), AgentId), true, Tag_TimeSinceHit));
	Hit.Add(Tag_HitAngle, O::MakeAngleObservation(InObservationObject,
		SanitizeObservation(bLive ? Unit->GetLastHitRelativeAngleDeg() : 0.0f, TEXT("HitAngle"), AgentId), 0.0f, true, Tag_HitAngle));

	// --- 자신 ---------------------------------------------------------------
	TMap<FName, FLearningAgentsObservationObjectElement> Self;
	Self.Add(Tag_HPRatio, O::MakeFloatObservation(InObservationObject,
		SanitizeObservation(Stats ? Stats->GetHPRatio() : 0.0f, TEXT("HPRatio"), AgentId), true, Tag_HPRatio));
	Self.Add(Tag_CanFire, O::MakeBoolObservation(InObservationObject,
		bLive && Unit->gun && Unit->gun->CanFire(), true, Tag_CanFire));

	// --- 팀 · 라운드 ----------------------------------------------------------
	// ★ 죽은 에이전트도 실제 값을 낸다(자기 위치에 의존하는 ZoneMargin 만 0).
	float AlliesAlive = 0.0f;
	float EnemiesAlive = 0.0f;
	float RoundProgress = 0.0f;
	float ZoneRadius01 = 1.0f;
	float ZoneMargin = 0.0f;
	if (const ASH_Arena* Arena = (Unit && Stats) ? Cast<ASH_Arena>(Unit->CachedGym.Get()) : nullptr)
	{
		const int32 MyTeam = static_cast<int32>(Stats->TeamId);
		const int32 OtherTeam = 1 - MyTeam;
		AlliesAlive  = static_cast<float>(Arena->GetAliveCount(MyTeam))    / static_cast<float>(FMath::Max(1, Arena->GetTeamSize(MyTeam)));
		EnemiesAlive = static_cast<float>(Arena->GetAliveCount(OtherTeam)) / static_cast<float>(FMath::Max(1, Arena->GetTeamSize(OtherTeam)));
		RoundProgress = FMath::Clamp(Arena->GetRoundElapsed() / FMath::Max(Arena->RoundTimeLimitSeconds, 1.0f), 0.0f, 1.0f);

		const float ArenaRadiusCm = FMath::Max(Arena->ArenaRadius, 1.0f);
		const float Zone = Arena->GetZoneRadius();
		ZoneRadius01 = Zone / ArenaRadiusCm;
		if (bLive)
		{
			// 양수 = 안전 구역 안쪽으로 그만큼 여유, 음수 = 밖에 있다(피해를 받는 중).
			const float MyDistance = FVector::DistXY(Unit->GetActorLocation(), Arena->GetActorLocation());
			ZoneMargin = (Zone - MyDistance) / ArenaRadiusCm;
		}
	}

	TMap<FName, FLearningAgentsObservationObjectElement> Team;
	Team.Add(Tag_AlliesAlive,   O::MakeFloatObservation(InObservationObject, SanitizeObservation(AlliesAlive,   TEXT("AlliesAlive"),   AgentId), true, Tag_AlliesAlive));
	Team.Add(Tag_EnemiesAlive,  O::MakeFloatObservation(InObservationObject, SanitizeObservation(EnemiesAlive,  TEXT("EnemiesAlive"),  AgentId), true, Tag_EnemiesAlive));
	Team.Add(Tag_RoundProgress, O::MakeFloatObservation(InObservationObject, SanitizeObservation(RoundProgress, TEXT("RoundProgress"), AgentId), true, Tag_RoundProgress));
	Team.Add(Tag_ZoneRadius,    O::MakeFloatObservation(InObservationObject, SanitizeObservation(ZoneRadius01,  TEXT("ZoneRadius"),    AgentId), true, Tag_ZoneRadius));
	Team.Add(Tag_ZoneMargin,    O::MakeFloatObservation(InObservationObject, SanitizeObservation(ZoneMargin,    TEXT("ZoneMargin"),    AgentId), true, Tag_ZoneMargin));

	TMap<FName, FLearningAgentsObservationObjectElement> Root;
	Root.Add(Tag_Fan,    Fan);
	Root.Add(Tag_Enemy,  EnemyOptional);
	Root.Add(Tag_Memory, O::MakeStructObservation(InObservationObject, Memory, true, Tag_Memory));
	Root.Add(Tag_Hit,    O::MakeStructObservation(InObservationObject, Hit,    true, Tag_Hit));
	Root.Add(Tag_Team,   O::MakeStructObservation(InObservationObject, Team,   true, Tag_Team));
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

	// --- 진단 지표 집계 -------------------------------------------------------
	// ★ 여기서 센다. 위의 조기 반환들을 지난 뒤이므로 **살아서 실제로 행동한 결정**만
	//   분모에 들어간다. 죽은 에이전트를 포함하면 이동 분포가 "정지" 로 쏠려 보인다.
	//
	// ★ 발사 가능 여부를 PullTrigger **전에** 읽어야 한다. 이 시점의 CanFire() 는
	//   MakeAgentActionModifier 가 마스킹을 결정할 때 본 값과 같다 —
	//   그 사이에 이 총이 발사되는 경로가 없다.
	++ActionStats.Decisions;
	++ActionStats.MoveCounts[FMath::Clamp(MoveIndex, 0, SHRL::MoveBins - 1)];

	const bool bCouldFire = Unit->gun && Unit->gun->CanFire();
	if (bCouldFire)
	{
		++ActionStats.FireOpportunities;
		if (bFire)
		{
			++ActionStats.FiresUsed;
		}
	}

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

	TMap<FName, FLearningAgentsActionModifierElement> Modifiers;

	// ★★★ 어떤 이산 행동도 **선택지 하나만 남기는 마스크**를 쓰지 않는다 (2026-10-02, 크래시 5회).
	//   엔진의 DistributionMultinoulliMasked(LearningRandom.cpp)는 검벨-최대 샘플링이다:
	//   값 = 로짓 − log(−log u). u 는 Float() 가 해시 상위 23비트로 만드는 [0, 1) 이라
	//   **정확히 0 이 나올 확률이 2^-23** 이고, 그러면 값이 −∞ 가 된다. 허용 선택지가 하나뿐이면
	//   그 하나가 −∞ 일 때 "MaxIdx != INDEX_NONE" 체크에 걸려 프로세스가 죽는다.
	//
	//   ★ 불리언 행동도 해당된다. SpecifyBoolAction 은 내부적으로 선택지 2개짜리 배타적 이산
	//   행동이고(LearningAgentsActions.cpp SpecifyBoolAction), MakeBoolActionModifier(false) 는
	//   "참" 인덱스를 막는 마스크라 **허용 선택지가 하나만 남는다.** 예전에는 쿨다운마다 발사를
	//   이렇게 막았고, 사망 마스크까지 더해지자 self-play 에서 수 분 간격으로 크래시했다.
	//   마스킹 빈도가 낮던 4·5회차(사망 시 에피소드 종료, 에이전트 160)는 수 시간 무사했다.

	// --- 사망 마스크 ----------------------------------------------------------
	// ★ 죽은 에이전트는 라운드 끝까지 에피소드에 남는다(USH_ShooterTrainingEnv 헤더 참조).
	//   행동은 PerformAgentAction 이 바로 버리므로 무엇이 뽑혀도 결과가 같다.
	//   MaskedIndices 는 막을 인덱스다(파이썬 log_softmax_masked 가 masked = 1 인 로짓을 지운다).
	//
	// ★★ 선택지를 **둘** 남긴다. 하나만 남기면 엔진이 크래시한다 (위 ★★★ 설명).
	//   죽은 에이전트 약 80 x 행동 2 x 초당 결정 11 = 초당 1,760 샘플 → 약 1.3시간에 한 번.
	//   둘을 남기면 (2^-23)^2 이라 사실상 0 이다. 대가는 죽은 스텝에 두 선택지 사이의 작은
	//   그래디언트가 생기는 것인데, 행동이 아무 효과가 없으므로 정책에 의미 있는 영향은 없다.
	//   ★ 전부 막아도 안 된다 — 허용된 선택지가 없으면 같은 체크에 항상 걸린다.
	const bool bDead = !Unit || !Unit->bEntityEnabled || !Unit->CombatStats || !Unit->CombatStats->IsAlive();
	if (bDead)
	{
		const int32 TurnKeepA = SHRL::TurnBins / 2;       // 회전 없음
		const int32 TurnKeepB = SHRL::TurnBins / 2 - 1;   // 그 옆 (효과 없음 — 행동이 버려진다)
		TArray<int32> TurnMasked;
		for (int32 Index = 0; Index < SHRL::TurnBins; ++Index)
		{
			if (Index != TurnKeepA && Index != TurnKeepB)
			{
				TurnMasked.Add(Index);
			}
		}
		TArray<int32> MoveMasked;
		for (int32 Index = 2; Index < SHRL::MoveBins; ++Index)   // 0 = 정지, 1 = 전진을 남긴다
		{
			MoveMasked.Add(Index);
		}

		Modifiers.Add(Tag_Turn, A::MakeExclusiveDiscreteActionModifier(InActionModifier, TurnMasked, Tag_Turn));
		Modifiers.Add(Tag_Move, A::MakeExclusiveDiscreteActionModifier(InActionModifier, MoveMasked, Tag_Move));
		// 발사는 막지 않는다(선택지가 둘뿐이라 막으면 하나가 남는다). 행동이 버려지므로 효과는 같다.
		Modifiers.Add(Tag_Fire, A::MakeNullActionModifier(InActionModifier, Tag_Fire));
		OutActionModifierElement = A::MakeStructActionModifier(InActionModifier, Modifiers, Tag_ActRoot);
		return;
	}

	// 회전과 이동은 막지 않는다 — 물리적으로 불가능한 선택지가 없다.
	Modifiers.Add(Tag_Turn, A::MakeExclusiveDiscreteActionModifier(InActionModifier, {}, Tag_Turn));
	Modifiers.Add(Tag_Move, A::MakeExclusiveDiscreteActionModifier(InActionModifier, {}, Tag_Move));

	// 발사는 막지 않는다. 쿨다운 중이면 AGun::PullTrigger 가 CanFire() 에서 바로 돌아가므로
	// 발사도 빗맞힘 집계도 생기지 않는다(Gun.cpp). 정책은 관측의 CanFire 로 쿨다운을 안다.
	// ★ 조준 오차로도 **절대** 마스킹하지 않는다 — 빗맞히는 게 나쁘다는 걸 영영 못 배운다.
	Modifiers.Add(Tag_Fire, A::MakeNullActionModifier(InActionModifier, Tag_Fire));

	OutActionModifierElement = A::MakeStructActionModifier(InActionModifier, Modifiers, Tag_ActRoot);
}
