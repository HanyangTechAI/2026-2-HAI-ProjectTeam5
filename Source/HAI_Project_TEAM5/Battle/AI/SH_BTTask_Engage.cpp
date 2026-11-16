// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/AI/SH_BTTask_Engage.h"

#include "Base_Character.h"
#include "Battle/AI/SH_BotSteering.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Combat/CombatStatsComponent.h"
#include "Gun.h"
#include "RL/SH_RLCharacter.h"

USH_BTTask_Engage::USH_BTTask_Engage()
{
	NodeName = TEXT("SH Engage");

	// 키 이름을 미리 넣어 두면 BB_SHCombat 에 같은 이름의 키가 있을 때 에디터에서 따로 고를 필요가 없다.
	TargetKey.SelectedKeyName = SHBot::Keys::TargetActor;
	TargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_Engage, TargetKey), AActor::StaticClass());
	OutsideZoneKey.SelectedKeyName = SHBot::Keys::OutsideZone;
	OutsideZoneKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_Engage, OutsideZoneKey));
	ZoneCenterKey.SelectedKeyName = SHBot::Keys::ZoneCenter;
	ZoneCenterKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(USH_BTTask_Engage, ZoneCenterKey));
}

void USH_BTTask_Engage::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		TargetKey.ResolveSelectedKey(*Blackboard);
		OutsideZoneKey.ResolveSelectedKey(*Blackboard);
		ZoneCenterKey.ResolveSelectedKey(*Blackboard);
	}
}

FString USH_BTTask_Engage::GetStaticDescription() const
{
	return FString::Printf(TEXT("표적: %s\n사격 콘 %.1f° · 거리 %.0f±%.0f cm%s"),
		*TargetKey.SelectedKeyName.ToString(), AccuracyConeDeg, DesiredRange, RangeTolerance,
		bStrafe ? TEXT(" · 좌우 이동") : TEXT(""));
}

EBTNodeResult::Type USH_BTTask_Engage::Begin(UBehaviorTreeComponent& /*OwnerComp*/, ASH_RLCharacter& Unit)
{
	StrafeSign = SHBot::PerUnitSign(Unit);
	LastFlipTime = 0.0f;
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type USH_BTTask_Engage::Decide(UBehaviorTreeComponent& OwnerComp, ASH_RLCharacter& Unit, const float ElapsedSeconds)
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const ABase_Character* Target = Blackboard
		? Cast<ABase_Character>(Blackboard->GetValueAsObject(TargetKey.SelectedKeyName)) : nullptr;
	if (!Target || !Target->CombatStats || !Target->CombatStats->IsAlive())
	{
		return EBTNodeResult::Succeeded;   // 표적이 없다 — 트리가 다음 행동을 고른다
	}
	const FVector TargetLocation = Target->GetActorLocation();

	// --- 조준 · 사격 ---------------------------------------------------------
	// ★ 오차는 이번 결정 시점의 조준으로 잰다. 회전은 다음 결정까지 유닛 Tick 이 적용한다(정책과 같은 순서).
	const float Error = SHBot::YawErrorTo(Unit, TargetLocation);
	Unit.PendingTurnRateDeg = SHBot::TurnRateFor(Unit, Error);
	if (FMath::Abs(Error) <= AccuracyConeDeg && Unit.gun && Unit.gun->CanFire())
	{
		// 시야 서비스가 "가리는 것 없이 보인다" 를 확인한 표적이다 — 같은 채널(Bullet)이라 쏘면 맞을 선이다.
		Unit.gun->PullTrigger();
	}

	// --- 이동 ----------------------------------------------------------------
	if (ElapsedSeconds - LastFlipTime >= StrafeFlipSeconds)
	{
		StrafeSign = -StrafeSign;
		LastFlipTime = ElapsedSeconds;
	}

	FVector Move = FVector::ZeroVector;
	if (Blackboard->GetValueAsBool(OutsideZoneKey.SelectedKeyName))
	{
		// 구역 밖에서 버티면 초당 피해를 받는다 — 쏘면서 안으로 들어간다.
		Move = SHBot::FlatDirectionTo(Unit, Blackboard->GetValueAsVector(ZoneCenterKey.SelectedKeyName));
	}
	else
	{
		const FVector ToTarget = SHBot::FlatDirectionTo(Unit, TargetLocation);
		const FVector Side = FVector(-ToTarget.Y, ToTarget.X, 0.0) * StrafeSign;
		const double Distance = FVector::Dist2D(Unit.GetActorLocation(), TargetLocation);
		const double SideWeight = bStrafe ? 0.5 : 0.0;
		if (Distance > DesiredRange + RangeTolerance)
		{
			Move = ToTarget + Side * SideWeight;      // 다가가며 지그재그
		}
		else if (Distance < DesiredRange - RangeTolerance)
		{
			Move = -ToTarget + Side * SideWeight;     // 물러나며 지그재그
		}
		else if (bStrafe)
		{
			Move = Side;
		}
	}
	Unit.PendingMoveInput = Move.GetSafeNormal();
	return EBTNodeResult::InProgress;
}
