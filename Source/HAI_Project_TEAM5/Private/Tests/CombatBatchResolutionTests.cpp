#if WITH_DEV_AUTOMATION_TESTS

#include "Base_Character.h"
#include "Combat/CombatStatsComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gun.h"
#include "Misc/AutomationTest.h"
#include "RL/SH_ShooterTrainingEnv.h"

namespace
{
	struct FCombatShotFixture
	{
		ABase_Character* Unit = nullptr;
		AAIController* Controller = nullptr;
		AGun* Gun = nullptr;
	};

	struct FCombatResolutionSnapshot
	{
		TArray<float> UnitHealth;
		TArray<float> DamageDealt;
		TArray<float> DamageTaken;
		TArray<int32> ShotsFired;
		TArray<int32> ShotsHit;
		TArray<int32> Kills;
		TArray<bool> bDiedThisInterval;
		TArray<float> ShotDamage;
		TArray<bool> bShotHitHostile;
	};

	bool SpawnCombatShotFixture(
		FAutomationTestBase& Test,
		UWorld* World,
		const FVector& Location,
		const float Yaw,
		const uint8 TeamId,
		const float MaxHealth,
		const float ShotDamage,
		FCombatShotFixture& OutFixture)
	{
		if (!Test.TestNotNull(TEXT("Test world"), World))
		{
			return false;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		OutFixture.Unit = World->SpawnActor<ABase_Character>(
			ABase_Character::StaticClass(), Location, FRotator::ZeroRotator, SpawnParameters);
		if (!Test.TestNotNull(TEXT("Combat unit"), OutFixture.Unit))
		{
			return false;
		}

		OutFixture.Unit->CombatStats->TeamId = TeamId;
		OutFixture.Unit->CombatStats->MaxHP = MaxHealth;
		OutFixture.Unit->CombatStats->CurrentHP = MaxHealth;
		OutFixture.Unit->CombatStats->OnDeath.AddUniqueDynamic(OutFixture.Unit, &ABase_Character::HandleDeath);
		OutFixture.Unit->GetCharacterMovement()->DisableMovement();

		OutFixture.Controller = World->SpawnActor<AAIController>(
			AAIController::StaticClass(), Location, FRotator::ZeroRotator, SpawnParameters);
		if (!Test.TestNotNull(TEXT("AI controller"), OutFixture.Controller))
		{
			return false;
		}
		OutFixture.Controller->Possess(OutFixture.Unit);
		OutFixture.Controller->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
		OutFixture.Controller->SetActorTickEnabled(false);

		OutFixture.Gun = World->SpawnActor<AGun>(
			AGun::StaticClass(), Location, FRotator::ZeroRotator, SpawnParameters);
		if (!Test.TestNotNull(TEXT("Gun"), OutFixture.Gun))
		{
			return false;
		}
		OutFixture.Gun->SetOwner(OutFixture.Unit);
		OutFixture.Gun->Damage = ShotDamage;
		OutFixture.Gun->Range = 2000.0f;
		OutFixture.Gun->FireInterval = 0.0f;
		OutFixture.Gun->SpreadDegrees = 0.0f;
		OutFixture.Gun->MuzzleHeight = 60.0f;
		OutFixture.Unit->gun = OutFixture.Gun;
		return true;
	}

	void DestroyCombatShotFixtures(UWorld* World, TArray<FCombatShotFixture>& Fixtures)
	{
		for (FCombatShotFixture& Fixture : Fixtures)
		{
			if (Fixture.Gun)
			{
				Fixture.Gun->Destroy();
			}
			if (Fixture.Controller)
			{
				Fixture.Controller->Destroy();
			}
			if (Fixture.Unit)
			{
				Fixture.Unit->Destroy();
			}
		}
		Fixtures.Reset();
	}

	FCombatResolutionSnapshot CaptureCombatResolution(
		const TArray<FCombatShotFixture>& Fixtures,
		const TArray<FGunFireResult>& Shots)
	{
		FCombatResolutionSnapshot Snapshot;
		for (int32 Index = 0; Index < Fixtures.Num(); ++Index)
		{
			const UCombatStatsComponent* Stats = Fixtures[Index].Unit->CombatStats;
			Snapshot.UnitHealth.Add(Stats->CurrentHP);
			Snapshot.DamageDealt.Add(Stats->DamageDealtAccum);
			Snapshot.DamageTaken.Add(Stats->DamageTakenAccum);
			Snapshot.ShotsFired.Add(Stats->ShotsFiredAccum);
			Snapshot.ShotsHit.Add(Stats->ShotsHitAccum);
			Snapshot.Kills.Add(Stats->KillsAccum);
			Snapshot.bDiedThisInterval.Add(Stats->bDiedThisInterval);
		}
		for (const FGunFireResult& Shot : Shots)
		{
			Snapshot.ShotDamage.Add(Shot.DamageApplied);
			Snapshot.bShotHitHostile.Add(Shot.bHitHostile);
		}
		return Snapshot;
	}

	bool PrepareShots(
		FAutomationTestBase& Test,
		const TArray<FCombatShotFixture>& Shooters,
		const TArray<AActor*>& ExpectedTargets,
		TArray<FGunFireResult>& OutShots)
	{
		OutShots.Reset(Shooters.Num());
		for (int32 Index = 0; Index < Shooters.Num(); ++Index)
		{
			FGunFireResult Shot = Shooters[Index].Gun->PrepareShot();
			Test.TestTrue(TEXT("Shot was fired"), Shot.bFired);
			Test.TestTrue(TEXT("Hits a collision object"), Shot.bHitSomething);
			Test.TestTrue(TEXT("Trace resolves to expected target"),
				ExpectedTargets.IsValidIndex(Index) && Shot.HitActor == ExpectedTargets[Index]);
			OutShots.Add(Shot);
		}
		return OutShots.Num() == Shooters.Num();
	}

	void ResolveShotsInOrder(
		const TArray<FCombatShotFixture>& Shooters,
		TArray<FGunFireResult>& Shots,
		const TArray<int32>& ResolveOrder)
	{
		for (const int32 ShotIndex : ResolveOrder)
		{
			Shooters[ShotIndex].Gun->ResolveShot(Shots[ShotIndex]);
		}
	}

	void TestSameSnapshot(
		FAutomationTestBase& Test,
		const FCombatResolutionSnapshot& Actual,
		const FCombatResolutionSnapshot& Expected,
		const TCHAR* Context)
	{
		Test.TestEqual(FString::Printf(TEXT("%s: unit count"), Context), Actual.UnitHealth.Num(), Expected.UnitHealth.Num());
		for (int32 Index = 0; Index < Expected.UnitHealth.Num(); ++Index)
		{
			Test.TestEqual(FString::Printf(TEXT("%s: unit %d HP"), Context, Index), Actual.UnitHealth[Index], Expected.UnitHealth[Index]);
			Test.TestEqual(FString::Printf(TEXT("%s: unit %d damage dealt"), Context, Index), Actual.DamageDealt[Index], Expected.DamageDealt[Index]);
			Test.TestEqual(FString::Printf(TEXT("%s: unit %d damage taken"), Context, Index), Actual.DamageTaken[Index], Expected.DamageTaken[Index]);
			Test.TestEqual(FString::Printf(TEXT("%s: unit %d shots fired"), Context, Index), Actual.ShotsFired[Index], Expected.ShotsFired[Index]);
			Test.TestEqual(FString::Printf(TEXT("%s: unit %d shots hit"), Context, Index), Actual.ShotsHit[Index], Expected.ShotsHit[Index]);
			Test.TestEqual(FString::Printf(TEXT("%s: unit %d kills"), Context, Index), Actual.Kills[Index], Expected.Kills[Index]);
			Test.TestEqual(FString::Printf(TEXT("%s: unit %d death flag"), Context, Index), Actual.bDiedThisInterval[Index], Expected.bDiedThisInterval[Index]);
		}
		Test.TestEqual(FString::Printf(TEXT("%s: shot count"), Context), Actual.ShotDamage.Num(), Expected.ShotDamage.Num());
		for (int32 Index = 0; Index < Expected.ShotDamage.Num(); ++Index)
		{
			Test.TestEqual(FString::Printf(TEXT("%s: shot %d applied damage"), Context, Index), Actual.ShotDamage[Index], Expected.ShotDamage[Index]);
			Test.TestEqual(FString::Printf(TEXT("%s: shot %d hostile hit"), Context, Index), Actual.bShotHitHostile[Index], Expected.bShotHitHostile[Index]);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeathMeshRotationResetTest,
	"HAI.Combat.DeathMeshRotationReset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FDeathMeshRotationResetTest::RunTest(const FString& Parameters)
{
	UWorld* World = GWorld;
	if (!TestNotNull(TEXT("Active Unreal world"), World))
	{
		return false;
	}

	TArray<FCombatShotFixture> Fixtures;
	Fixtures.SetNum(1);
	if (!SpawnCombatShotFixture(*this, World, FVector(180000.0f, 100000.0f, 1000.0f),
		0.0f, 0, 100.0f, 10.0f, Fixtures[0]))
	{
		DestroyCombatShotFixtures(World, Fixtures);
		return false;
	}

	ABase_Character* Unit = Fixtures[0].Unit;
	const FRotator AliveRotation = Unit->GetMesh()->GetRelativeRotation();
	const FRotator DeadRotation = AliveRotation + FRotator(90.0f, 0.0f, 0.0f);

	Unit->CombatStats->ApplyDamage(Unit->CombatStats->MaxHP, nullptr);
	TestTrue(TEXT("Death rotates mesh by 90 degrees"),
		Unit->GetMesh()->GetRelativeRotation().Equals(DeadRotation));
	TestTrue(TEXT("Death rotation remains until reset"),
		Unit->GetMesh()->GetRelativeRotation().Equals(DeadRotation));

	Unit->ResetForNewMatch();
	TestTrue(TEXT("Placement reset restores alive mesh rotation"),
		Unit->GetMesh()->GetRelativeRotation().Equals(AliveRotation));

	Unit->CombatStats->ApplyDamage(Unit->CombatStats->MaxHP, nullptr);
	TestTrue(TEXT("Second battle death rotates mesh again"),
		Unit->GetMesh()->GetRelativeRotation().Equals(DeadRotation));

	DestroyCombatShotFixtures(World, Fixtures);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTwoShootersSingleTargetRewardCreditTest,
	"HAI.Combat.BatchResolution.TwoShootersSingleTargetRewardCredit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FTwoShootersSingleTargetRewardCreditTest::RunTest(const FString& Parameters)
{
	UWorld* World = GWorld;
	if (!TestNotNull(TEXT("Active Unreal world"), World))
	{
		return false;
	}

	USH_ShooterTrainingEnv* RewardEnv = NewObject<USH_ShooterTrainingEnv>();
	if (!TestNotNull(TEXT("Production reward environment"), RewardEnv))
	{
		return false;
	}

	const FVector Origin(140000.0f, 100000.0f, 1000.0f);
	auto RunCase = [this, World, RewardEnv, Origin](
		const float TargetHealth, const TArray<int32>& ResolveOrder, const FVector& Offset,
		FCombatResolutionSnapshot& OutSnapshot, TArray<float>& OutRewards) -> bool
	{
		TArray<FCombatShotFixture> Fixtures;
		Fixtures.SetNum(3);
		const FVector CaseOrigin = Origin + Offset;
		if (!SpawnCombatShotFixture(*this, World, CaseOrigin + FVector(0.0f, -75.0f, 0.0f), 0.0f, 0, 100.0f, 10.0f, Fixtures[0])
			|| !SpawnCombatShotFixture(*this, World, CaseOrigin + FVector(0.0f, 75.0f, 0.0f), 0.0f, 0, 100.0f, 10.0f, Fixtures[1])
			|| !SpawnCombatShotFixture(*this, World, CaseOrigin + FVector(500.0f, 0.0f, 0.0f), 0.0f, 1, TargetHealth, 10.0f, Fixtures[2]))
		{
			DestroyCombatShotFixtures(World, Fixtures);
			return false;
		}
		Fixtures[2].Unit->GetCapsuleComponent()->SetCapsuleSize(160.0f, 200.0f);

		TArray<FCombatShotFixture> Shooters = { Fixtures[0], Fixtures[1] };
		TArray<AActor*> ExpectedTargets = { Fixtures[2].Unit, Fixtures[2].Unit };
		TArray<FGunFireResult> Shots;
		PrepareShots(*this, Shooters, ExpectedTargets, Shots);
		ResolveShotsInOrder(Shooters, Shots, ResolveOrder);
		OutSnapshot = CaptureCombatResolution(Fixtures, Shots);
		OutRewards = {
			RewardEnv->CalculateCombatReward(*Fixtures[0].Unit->CombatStats),
			RewardEnv->CalculateCombatReward(*Fixtures[1].Unit->CombatStats)
		};
		DestroyCombatShotFixtures(World, Fixtures);
		return true;
	};

	FCombatResolutionSnapshot UnderkillForward, UnderkillReverse;
	TArray<float> UnderkillForwardRewards, UnderkillReverseRewards;
	TestTrue(TEXT("Run underkill forward"), RunCase(25.0f, { 0, 1 }, FVector::ZeroVector,
		UnderkillForward, UnderkillForwardRewards));
	TestTrue(TEXT("Run underkill reverse"), RunCase(25.0f, { 1, 0 }, FVector(10000.0f, 0.0f, 0.0f),
		UnderkillReverse, UnderkillReverseRewards));
	for (int32 ShooterIndex = 0; ShooterIndex < 2; ++ShooterIndex)
	{
		TestEqual(TEXT("Underkill gives each shooter full damage"), UnderkillForward.DamageDealt[ShooterIndex], 10.0f);
		TestEqual(TEXT("Underkill gives no kill"), UnderkillForward.Kills[ShooterIndex], 0);
		TestEqual(TEXT("Underkill production combat reward"), UnderkillForwardRewards[ShooterIndex], 0.1f);
		TestEqual(TEXT("Underkill damage is order invariant"), UnderkillReverse.DamageDealt[ShooterIndex], UnderkillForward.DamageDealt[ShooterIndex]);
		TestEqual(TEXT("Underkill reward is order invariant"), UnderkillReverseRewards[ShooterIndex], UnderkillForwardRewards[ShooterIndex]);
	}

	FCombatResolutionSnapshot OverkillForward, OverkillReverse;
	TArray<float> OverkillForwardRewards, OverkillReverseRewards;
	TestTrue(TEXT("Run overkill forward"), RunCase(15.0f, { 0, 1 }, FVector(20000.0f, 0.0f, 0.0f),
		OverkillForward, OverkillForwardRewards));
	TestTrue(TEXT("Run overkill reverse"), RunCase(15.0f, { 1, 0 }, FVector(30000.0f, 0.0f, 0.0f),
		OverkillReverse, OverkillReverseRewards));

	TestEqual(TEXT("Forward first shooter damage"), OverkillForward.DamageDealt[0], 10.0f);
	TestEqual(TEXT("Forward lethal shooter remaining damage"), OverkillForward.DamageDealt[1], 5.0f);
	TestEqual(TEXT("Forward lethal shooter kill"), OverkillForward.Kills[1], 1);
	TestEqual(TEXT("Forward first shooter production combat reward"), OverkillForwardRewards[0], 0.1f);
	TestEqual(TEXT("Forward lethal shooter production combat reward"), OverkillForwardRewards[1], 0.55f);
	TestEqual(TEXT("Reverse lethal shooter remaining damage"), OverkillReverse.DamageDealt[0], 5.0f);
	TestEqual(TEXT("Reverse first shooter damage"), OverkillReverse.DamageDealt[1], 10.0f);
	TestEqual(TEXT("Reverse lethal shooter kill"), OverkillReverse.Kills[0], 1);
	TestEqual(TEXT("Reverse lethal shooter production combat reward"), OverkillReverseRewards[0], 0.55f);
	TestEqual(TEXT("Reverse first shooter production combat reward"), OverkillReverseRewards[1], 0.1f);
	TestEqual(TEXT("Overkill team damage is order invariant"),
		OverkillForward.DamageDealt[0] + OverkillForward.DamageDealt[1],
		OverkillReverse.DamageDealt[0] + OverkillReverse.DamageDealt[1]);
	TestEqual(TEXT("Overkill team kill count is order invariant"),
		OverkillForward.Kills[0] + OverkillForward.Kills[1],
		OverkillReverse.Kills[0] + OverkillReverse.Kills[1]);
	TestEqual(TEXT("Overkill team combat reward is order invariant"),
		OverkillForwardRewards[0] + OverkillForwardRewards[1],
		OverkillReverseRewards[0] + OverkillReverseRewards[1]);
	TestNotEqual(TEXT("Shooter 0 production reward changes with order"),
		OverkillForwardRewards[0], OverkillReverseRewards[0]);

	UCombatStatsComponent* StandaloneStats = NewObject<UCombatStatsComponent>();
	StandaloneStats->DamageDealtAccum = 10.0f;
	StandaloneStats->ShotsFiredAccum = 1;
	StandaloneStats->ShotsHitAccum = 1;
	TestEqual(TEXT("Standalone nonlethal production combat reward"),
		RewardEnv->CalculateCombatReward(*StandaloneStats), 0.1f);
	StandaloneStats->KillsAccum = 1;
	TestEqual(TEXT("Standalone lethal production combat reward"),
		RewardEnv->CalculateCombatReward(*StandaloneStats), 0.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReciprocalLethalBatchResolutionTest,
	"HAI.Combat.BatchResolution.ReciprocalLethalShots",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FReciprocalLethalBatchResolutionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GWorld;
	if (!TestNotNull(TEXT("Active Unreal world"), World))
	{
		return false;
	}

	const FVector Origin(100000.0f, 100000.0f, 1000.0f);
	FCombatResolutionSnapshot Reference;
	for (int32 OrderIndex = 0; OrderIndex < 2; ++OrderIndex)
	{
		TArray<FCombatShotFixture> Fixtures;
		Fixtures.SetNum(2);
		const FVector RunOrigin = Origin + FVector(OrderIndex * 10000.0f, 0.0f, 0.0f);
		if (!SpawnCombatShotFixture(*this, World, RunOrigin, 0.0f, 0, 20.0f, 25.0f, Fixtures[0])
			|| !SpawnCombatShotFixture(*this, World, RunOrigin + FVector(400.0f, 0.0f, 0.0f), 180.0f, 1, 20.0f, 25.0f, Fixtures[1]))
		{
			DestroyCombatShotFixtures(World, Fixtures);
			return false;
		}

		TArray<FGunFireResult> Shots;
		TArray<AActor*> ExpectedTargets = { Fixtures[1].Unit, Fixtures[0].Unit };
		PrepareShots(*this, Fixtures, ExpectedTargets, Shots);
		TestTrue(TEXT("Second shot targets the first shooter"), Shots[1].HitActor == Fixtures[0].Unit);

		const TArray<int32> ForwardOrder = { 0, 1 };
		const TArray<int32> ReverseOrder = { 1, 0 };
		ResolveShotsInOrder(Fixtures, Shots, OrderIndex == 0 ? ForwardOrder : ReverseOrder);

		const FCombatResolutionSnapshot Snapshot = CaptureCombatResolution(Fixtures, Shots);
		for (int32 UnitIndex = 0; UnitIndex < Fixtures.Num(); ++UnitIndex)
		{
			TestFalse(TEXT("Both combatants die"), Fixtures[UnitIndex].Unit->CombatStats->IsAlive());
			TestEqual(TEXT("Death callback disables capsule collision"),
				static_cast<uint8>(Fixtures[UnitIndex].Unit->GetCapsuleComponent()->GetCollisionEnabled()),
				static_cast<uint8>(ECollisionEnabled::NoCollision));
			TestTrue(TEXT("Death interval is recorded"), Snapshot.bDiedThisInterval[UnitIndex]);
			TestEqual(TEXT("One shot recorded for each combatant"), Snapshot.ShotsFired[UnitIndex], 1);
			TestEqual(TEXT("Both prepared shots count as hits"), Snapshot.ShotsHit[UnitIndex], 1);
			TestEqual(TEXT("Each lethal attack is credited"), Snapshot.Kills[UnitIndex], 1);
			TestEqual(TEXT("Overkill is clamped to remaining HP"), Snapshot.DamageDealt[UnitIndex], 20.0f);
			TestEqual(TEXT("Each combatant takes lethal damage"), Snapshot.DamageTaken[UnitIndex], 20.0f);
			TestEqual(TEXT("Prepared shot applies lethal damage"), Snapshot.ShotDamage[UnitIndex], 20.0f);
			TestTrue(TEXT("Prepared shot remains a hostile hit"), Snapshot.bShotHitHostile[UnitIndex]);
		}

		if (OrderIndex == 0)
		{
			Reference = Snapshot;
		}
		else
		{
			TestSameSnapshot(*this, Snapshot, Reference, TEXT("Reversed reciprocal shot order"));
		}
		DestroyCombatShotFixtures(World, Fixtures);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMultipleShootersSingleTargetBatchResolutionTest,
	"HAI.Combat.BatchResolution.MultipleShootersSingleTarget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FMultipleShootersSingleTargetBatchResolutionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GWorld;
	if (!TestNotNull(TEXT("Active Unreal world"), World))
	{
		return false;
	}

	const FVector Origin(120000.0f, 100000.0f, 1000.0f);
	FCombatResolutionSnapshot Reference;
	for (int32 OrderIndex = 0; OrderIndex < 2; ++OrderIndex)
	{
		TArray<FCombatShotFixture> Shooters;
		Shooters.SetNum(4);
		const FVector RunOrigin = Origin + FVector(OrderIndex * 10000.0f, 0.0f, 0.0f);
		const float ShooterY[3] = { -100.0f, 0.0f, 100.0f };
		bool bSpawned = true;
		for (int32 ShooterIndex = 0; ShooterIndex < 3; ++ShooterIndex)
		{
			bSpawned &= SpawnCombatShotFixture(
				*this, World, RunOrigin + FVector(0.0f, ShooterY[ShooterIndex], 0.0f),
				0.0f, 0, 100.0f, 10.0f, Shooters[ShooterIndex]);
		}
		bSpawned &= SpawnCombatShotFixture(
			*this, World, RunOrigin + FVector(500.0f, 0.0f, 0.0f),
			0.0f, 1, 15.0f, 10.0f, Shooters[3]);
		if (!bSpawned)
		{
			DestroyCombatShotFixtures(World, Shooters);
			return false;
		}
		Shooters[3].Unit->GetCapsuleComponent()->SetCapsuleSize(180.0f, 200.0f);

		TArray<FCombatShotFixture> AttackingShooters;
		TArray<AActor*> ExpectedTargets;
		for (int32 ShooterIndex = 0; ShooterIndex < 3; ++ShooterIndex)
		{
			AttackingShooters.Add(Shooters[ShooterIndex]);
			ExpectedTargets.Add(Shooters[3].Unit);
		}

		TArray<FGunFireResult> Shots;
		PrepareShots(*this, AttackingShooters, ExpectedTargets, Shots);
		const TArray<int32> ForwardOrder = { 0, 1, 2 };
		const TArray<int32> ReverseOrder = { 2, 0, 1 };
		ResolveShotsInOrder(AttackingShooters, Shots, OrderIndex == 0 ? ForwardOrder : ReverseOrder);

		const FCombatResolutionSnapshot Snapshot = CaptureCombatResolution(Shooters, Shots);
		float TeamDamageDealt = 0.0f;
		int32 TeamKills = 0;
		int32 TeamShotsFired = 0;
		int32 TeamShotsHit = 0;
		float TeamCombatReward = 0.0f;
		for (int32 ShooterIndex = 0; ShooterIndex < 3; ++ShooterIndex)
		{
			TeamDamageDealt += Snapshot.DamageDealt[ShooterIndex];
			TeamKills += Snapshot.Kills[ShooterIndex];
			TeamShotsFired += Snapshot.ShotsFired[ShooterIndex];
			TeamShotsHit += Snapshot.ShotsHit[ShooterIndex];

			const int32 MissedShots = Snapshot.ShotsFired[ShooterIndex] - Snapshot.ShotsHit[ShooterIndex];
			TeamCombatReward += Snapshot.DamageDealt[ShooterIndex] / 100.0f
				+ 0.5f * Snapshot.Kills[ShooterIndex]
				- 0.005f * MissedShots;
		}

		TestFalse(TEXT("Shared target is killed"), Shooters[3].Unit->CombatStats->IsAlive());
		TestEqual(TEXT("Death callback disables shared target collision"),
			static_cast<uint8>(Shooters[3].Unit->GetCapsuleComponent()->GetCollisionEnabled()),
			static_cast<uint8>(ECollisionEnabled::NoCollision));
		TestTrue(TEXT("Shared target death interval is recorded"), Snapshot.bDiedThisInterval[3]);
		TestEqual(TEXT("Team receives only remaining target HP"), TeamDamageDealt, 15.0f);
		TestEqual(TEXT("Exactly one kill is credited to the team"), TeamKills, 1);
		TestEqual(TEXT("All three trigger pulls are recorded"), TeamShotsFired, 3);
		TestEqual(TEXT("Only positive-damage shots count as hits"), TeamShotsHit, 2);
		TestEqual(TEXT("Target damage taken is clamped to HP"), Snapshot.DamageTaken[3], 15.0f);
		TestEqual(TEXT("Aggregate combat reward contribution is order invariant"), TeamCombatReward, 0.645f);

		const int32 LethalShooter = OrderIndex == 0 ? 1 : 0;
		const int32 FirstShooter = OrderIndex == 0 ? 0 : 2;
		const int32 LastShooter = OrderIndex == 0 ? 2 : 1;
		TestEqual(TEXT("First resolved attacker receives partial damage"), Snapshot.DamageDealt[FirstShooter], 10.0f);
		TestEqual(TEXT("Last damaging attacker receives remaining HP damage"), Snapshot.DamageDealt[LethalShooter], 5.0f);
		TestEqual(TEXT("Zero-damage overkill is not credited"), Snapshot.DamageDealt[LastShooter], 0.0f);
		TestEqual(TEXT("Kill goes to the first shot that crosses zero HP"), Snapshot.Kills[LethalShooter], 1);
		TestEqual(TEXT("Other attackers receive no kill"), Snapshot.Kills[FirstShooter] + Snapshot.Kills[LastShooter], 0);
		TestEqual(TEXT("Zero-damage shot is not recorded as a hit"), Snapshot.ShotsHit[LastShooter], 0);

		const float FirstShooterReward = Snapshot.DamageDealt[FirstShooter] / 100.0f
			+ 0.5f * Snapshot.Kills[FirstShooter]
			- 0.005f * (Snapshot.ShotsFired[FirstShooter] - Snapshot.ShotsHit[FirstShooter]);
		const float LethalShooterReward = Snapshot.DamageDealt[LethalShooter] / 100.0f
			+ 0.5f * Snapshot.Kills[LethalShooter]
			- 0.005f * (Snapshot.ShotsFired[LethalShooter] - Snapshot.ShotsHit[LethalShooter]);
		const float LastShooterReward = Snapshot.DamageDealt[LastShooter] / 100.0f
			+ 0.5f * Snapshot.Kills[LastShooter]
			- 0.005f * (Snapshot.ShotsFired[LastShooter] - Snapshot.ShotsHit[LastShooter]);

		TestEqual(TEXT("First damaging attack combat reward"), FirstShooterReward, 0.1f);
		TestEqual(TEXT("Lethal attack combat reward"), LethalShooterReward, 0.55f);
		TestEqual(TEXT("Post-death shot combat reward"), LastShooterReward, -0.005f);

		if (OrderIndex == 0)
		{
			Reference = Snapshot;
		}
		else
		{
			TestNotEqual(TEXT("Individual kill attribution changes with order"),
				Snapshot.Kills[LethalShooter], Reference.Kills[LethalShooter]);
			TestEqual(TEXT("Team damage is invariant under reversed order"),
				Snapshot.DamageDealt[0] + Snapshot.DamageDealt[1] + Snapshot.DamageDealt[2],
				Reference.DamageDealt[0] + Reference.DamageDealt[1] + Reference.DamageDealt[2]);
			TestEqual(TEXT("Team kill count is invariant under reversed order"),
				Snapshot.Kills[0] + Snapshot.Kills[1] + Snapshot.Kills[2],
				Reference.Kills[0] + Reference.Kills[1] + Reference.Kills[2]);
			TestEqual(TEXT("Team hit count is invariant under reversed order"),
				Snapshot.ShotsHit[0] + Snapshot.ShotsHit[1] + Snapshot.ShotsHit[2],
				Reference.ShotsHit[0] + Reference.ShotsHit[1] + Reference.ShotsHit[2]);
			TestNotEqual(TEXT("Individual damage attribution changes with order"),
				Snapshot.DamageDealt[0], Reference.DamageDealt[0]);
		}
		DestroyCombatShotFixtures(World, Shooters);
	}
	return true;
}

#endif
