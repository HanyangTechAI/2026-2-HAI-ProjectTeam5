#include "SH_CombatTrainer.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/DateTime.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SH_RLCharacter.h"
#include "SH_ShooterInteractor.h"
#include "SH_ShooterTrainingEnv.h"
#include "SH_VisionComponent.h"

void ASH_CombatTrainer::WriteExperimentSettings()
{
	// A GUID prevents concurrent runs or same-second PIE sessions from sharing files.
	ExperimentDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("SHExperiments"),
		SnapshotRunStamp + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	if (!IFileManager::Get().MakeDirectory(*ExperimentDirectory, true))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_Experiment] Cannot create directory: %s"), *ExperimentDirectory);
		ExperimentDirectory.Empty();
		return;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("format_version"), 1);
	Root->SetStringField(TEXT("implementation"), TEXT("aim-experiment-v1"));
	Root->SetStringField(TEXT("experiment"), ExperimentName);
	Root->SetStringField(TEXT("run"), SnapshotRunStamp);
	Root->SetStringField(TEXT("started_at"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("map"), GetWorld()->GetMapName());
	Root->SetStringField(TEXT("mode"), bEvalMode ? TEXT("evaluation") : TEXT("training"));
	Root->SetNumberField(TEXT("seed"), Seed);
	Root->SetStringField(TEXT("resume_directory"), ResumeDir);
	Root->SetStringField(TEXT("evaluation_snapshot"), EvalSnapshotDir);
	Root->SetStringField(TEXT("evaluation_opponent"), EvalOpponentDir);
	Root->SetNumberField(TEXT("evaluation_noise"), EvalActionNoise);
	Root->SetNumberField(TEXT("evaluation_max_rounds"), EvalMaxRounds);
	Root->SetBoolField(TEXT("evaluation_realtime"), bEvalRealtime);
	Root->SetBoolField(TEXT("aim_assist"), bAimAssist);
	Root->SetNumberField(TEXT("aim_assist_strength"), AimAssistStrength);
	Root->SetStringField(TEXT("aim_assist_scope"), TEXT("learner interactor only; both teams in self-play; snapshot/bot opponents unchanged"));
	Root->SetStringField(TEXT("aim_assist_rule"), TEXT("blend yaw rates toward nearest visible enemy; no visible enemy: policy Turn; Fire unchanged"));
	Root->SetBoolField(TEXT("aim_reward"), bAimReward);
	Root->SetNumberField(TEXT("aim_reward_scale"), AimRewardScale);
	Root->SetBoolField(TEXT("see_reward"), bSeeReward);
	Root->SetNumberField(TEXT("see_reward_scale"), SeeRewardScale);
	Root->SetNumberField(TEXT("decision_rate_hz"), DecisionRateHz);
	Root->SetNumberField(TEXT("report_interval_seconds"), ReportInterval);
	Root->SetNumberField(TEXT("max_wall_clock_seconds"), MaxWallClockSeconds);
	Root->SetNumberField(TEXT("snapshot_interval_seconds"), SnapshotIntervalSeconds);
	Root->SetStringField(TEXT("snapshot_root"), SnapshotRootDir);
	Root->SetBoolField(TEXT("reset_agents_on_update"), bResetAgentsOnUpdate);
	Root->SetBoolField(TEXT("self_play"), bSelfPlay);
	Root->SetBoolField(TEXT("opponent_mix"), bOpponentMix);
	Root->SetNumberField(TEXT("mix_snapshot_arenas"), MixSnapshotArenas);
	Root->SetNumberField(TEXT("mix_bot_arenas"), MixBotArenas);
	Root->SetNumberField(TEXT("opponent_swap_seconds"), OpponentSwapSeconds);
	Root->SetNumberField(TEXT("opponent_latest_probability"), OpponentLatestProbability);
	Root->SetStringField(TEXT("opponent_pool"), OpponentPoolDir);
	Root->SetNumberField(TEXT("bot_reaction_delay_min"), BotReactionDelayRange.X);
	Root->SetNumberField(TEXT("bot_reaction_delay_max"), BotReactionDelayRange.Y);
	Root->SetNumberField(TEXT("observation_size"), Interactor->GetObservationVectorSize());
	Root->SetNumberField(TEXT("observation_encoded_size"), Interactor->GetObservationEncodedVectorSize());
	Root->SetNumberField(TEXT("action_size"), Interactor->GetActionVectorSize());
	Root->SetNumberField(TEXT("action_distribution_size"), Interactor->GetActionDistributionVectorSize());
	Root->SetNumberField(TEXT("initial_reward_steps"), TrainingEnvironment->GetRewardStepCount());
	int32 AgentOrder = 0;
	int32 SwapSides = 0;
	FParse::Value(FCommandLine::Get(), TEXT("SHAgentOrder="), AgentOrder);
	FParse::Value(FCommandLine::Get(), TEXT("SHSwapSpawnSides="), SwapSides);
	Root->SetNumberField(TEXT("agent_order_override"), AgentOrder >= 0 && AgentOrder <= 2 ? AgentOrder : 0);
	Root->SetBoolField(TEXT("swap_spawn_sides"), SwapSides == 1);
	TArray<ASH_RLCharacter*> Learners;
	CollectAgentUnits(Learners);
	Root->SetNumberField(TEXT("learner_count"), Learners.Num());
	if (!Learners.IsEmpty())
	{
		const ASH_RLCharacter* Unit = Learners[0];
		TSharedRef<FJsonObject> UnitSettings = MakeShared<FJsonObject>();
		UnitSettings->SetStringField(TEXT("class"), Unit->GetClass()->GetPathName());
		UnitSettings->SetNumberField(TEXT("max_turn_rate_deg"), Unit->MaxTurnRateDeg);
		UnitSettings->SetNumberField(TEXT("hit_memory_horizon_seconds"), Unit->HitMemoryHorizonSeconds);
		if (Unit->Vision)
		{
			UnitSettings->SetNumberField(TEXT("fov_degrees"), Unit->Vision->FOVDegrees);
			UnitSettings->SetNumberField(TEXT("vision_range"), Unit->Vision->MaxRange);
			UnitSettings->SetNumberField(TEXT("memory_horizon_seconds"), Unit->Vision->MemoryHorizonSeconds);
		}
		Root->SetObjectField(TEXT("representative_learner"), UnitSettings);
	}

	// Capture effective USTRUCT settings after command-line overrides (not just defaults).
	auto AddSettings = [&Root](const TCHAR* Key, const auto& Settings)
	{
		const TSharedPtr<FJsonObject> Object = FJsonObjectConverter::UStructToJsonObject(Settings);
		if (Object.IsValid()) { Root->SetObjectField(Key, Object); }
		else { UE_LOG(LogTemp, Error, TEXT("[SH_Experiment] Cannot serialize %s"), Key); }
	};
	AddSettings(TEXT("ppo_training_settings"), TrainingSettings);
	AddSettings(TEXT("ppo_trainer_settings"), TrainerSettings);
	AddSettings(TEXT("game_settings"), GameSettings);
	AddSettings(TEXT("process_settings"), ProcessSettings);
	AddSettings(TEXT("shared_memory_settings"), SharedMemorySettings);

	const USH_ShooterTrainingEnv* Env = TrainingEnvironment;
	TSharedRef<FJsonObject> Rewards = MakeShared<FJsonObject>();
	Rewards->SetNumberField(TEXT("damage_dealt_weight"), Env->DamageDealtWeight);
	Rewards->SetNumberField(TEXT("damage_taken_weight"), Env->DamageTakenWeight);
	Rewards->SetNumberField(TEXT("kill_bonus"), Env->KillBonus);
	Rewards->SetNumberField(TEXT("death_penalty"), Env->DeathPenalty);
	Rewards->SetNumberField(TEXT("miss_weight"), Env->MissWeight);
	Rewards->SetNumberField(TEXT("time_penalty"), Env->TimePenalty);
	Rewards->SetNumberField(TEXT("see_weight"), Env->SeeWeight);
	Rewards->SetNumberField(TEXT("align_weight"), Env->AlignWeight);
	Rewards->SetNumberField(TEXT("effective_see_weight"), bSeeReward ? Env->SeeWeight * SeeRewardScale : 0.0f);
	Rewards->SetNumberField(TEXT("effective_align_weight"), bAimReward ? Env->AlignWeight * AimRewardScale : 0.0f);
	Rewards->SetNumberField(TEXT("approach_weight"), Env->ApproachWeight);
	Rewards->SetNumberField(TEXT("engage_range_cm"), Env->EngageRangeCm);
	Rewards->SetNumberField(TEXT("approach_max_step_cm"), Env->ApproachMaxStepCm);
	Rewards->SetNumberField(TEXT("shaping_decay_steps"), Env->ShapingDecaySteps);
	Rewards->SetNumberField(TEXT("shaping_floor"), Env->ShapingFloor);
	Rewards->SetNumberField(TEXT("outcome_reward"), Env->OutcomeReward);
	Rewards->SetNumberField(TEXT("team_spirit"), Env->TeamSpirit);
	Rewards->SetNumberField(TEXT("round_reward_ramp_steps"), Env->RoundRewardRampSteps);
	Rewards->SetBoolField(TEXT("round_clock_cost"), Env->bRoundClockCost);
	Rewards->SetNumberField(TEXT("clock_discount"), Env->ClockDiscount);
	Root->SetObjectField(TEXT("rewards"), Rewards);

	TArray<TSharedPtr<FJsonValue>> ArenaSettings;
	for (const TWeakObjectPtr<ASH_Arena>& Weak : Arenas)
	{
		if (const ASH_Arena* Arena = Weak.Get())
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Arena->GetName());
			Entry->SetNumberField(TEXT("role"), static_cast<int32>(GetArenaRole(Arena)));
			Entry->SetNumberField(TEXT("team0_size"), Arena->GetTeamSize(0));
			Entry->SetNumberField(TEXT("team1_size"), Arena->GetTeamSize(1));
			Entry->SetNumberField(TEXT("round_time_limit_seconds"), Arena->RoundTimeLimitSeconds);
			Entry->SetBoolField(TEXT("timeout_is_loss_for_both"), Arena->bTimeoutIsLossForBoth);
			Entry->SetBoolField(TEXT("shrink_zone"), Arena->bShrinkZone);
			Entry->SetNumberField(TEXT("arena_radius"), Arena->ArenaRadius);
			Entry->SetNumberField(TEXT("zone_start_seconds"), Arena->ZoneStartSeconds);
			Entry->SetNumberField(TEXT("zone_shrink_seconds"), Arena->ZoneShrinkSeconds);
			Entry->SetNumberField(TEXT("zone_final_radius"), Arena->ZoneFinalRadius);
			Entry->SetNumberField(TEXT("zone_damage_per_second"), Arena->ZoneDamagePerSecond);
			Entry->SetNumberField(TEXT("zone_damage_interval"), Arena->ZoneDamageInterval);
			Entry->SetNumberField(TEXT("outcome_reward_scale"), Arena->OutcomeRewardScale);
			Entry->SetNumberField(TEXT("spawn_angle_jitter"), Arena->SpawnAngleJitterDegrees);
			Entry->SetNumberField(TEXT("spawn_radius_jitter"), Arena->SpawnRadiusJitter);
			ArenaSettings.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	Root->SetArrayField(TEXT("arenas"), ArenaSettings);
	Root->SetStringField(TEXT("reward_csv_semantics"), TEXT("interval signed sums; raw_* before team mixing; individual_applied + team_applied + outcome + clock = total; samples include dead agents"));

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root, Writer) || !FFileHelper::SaveStringToFile(Text,
		*FPaths::Combine(ExperimentDirectory, TEXT("settings.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_Experiment] Cannot write settings: %s"), *ExperimentDirectory);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[SH_Experiment] aim_assist=%d strength=%.3f aim_reward=%d scale=%.3f see_reward=%d scale=%.3f | %s"),
		bAimAssist ? 1 : 0, AimAssistStrength, bAimReward ? 1 : 0, AimRewardScale,
		bSeeReward ? 1 : 0, SeeRewardScale, *ExperimentDirectory);
}

void ASH_CombatTrainer::ReportRewardStats(int64 AssistedDecisions, int64 LiveDecisions)
{
	if (!TrainingEnvironment) { return; }
	FSHRewardStats Stats;
	TrainingEnvironment->ConsumeRewardStats(Stats);
	if (Stats.Samples == 0) { return; }
	const double PerK = 1000.0 / Stats.Samples;
	UE_LOG(LogTemp, Display,
		TEXT("[SH_Rewards] raw/1k: dealt=%.3f taken=%.3f kill=%.3f miss=%.3f death=%.3f time=%.3f see=%.3f align=%.3f approach=%.3f | ")
		TEXT("applied/1k: individual=%.3f team=%.3f outcome=%.3f clock=%.3f total=%.3f | samples=%lld assist=%lld/%lld"),
		Stats.DamageDealt * PerK, Stats.DamageTaken * PerK, Stats.Kill * PerK, Stats.Miss * PerK,
		Stats.Death * PerK, Stats.Time * PerK, Stats.See * PerK, Stats.Align * PerK, Stats.Approach * PerK,
		Stats.IndividualApplied * PerK, Stats.TeamApplied * PerK, Stats.Outcome * PerK,
		Stats.Clock * PerK, Stats.Total * PerK, Stats.Samples, AssistedDecisions, LiveDecisions);
	if (ExperimentDirectory.IsEmpty()) { return; }
	const FString Path = FPaths::Combine(ExperimentDirectory, TEXT("rewards.csv"));
	FString Row;
	if (!FPaths::FileExists(Path))
	{
		Row = TEXT("elapsed_seconds,decisions,samples,raw_damage_dealt,raw_damage_taken,raw_kill,raw_miss,raw_death,raw_time,raw_see,raw_align,individual_applied,team_applied,outcome,clock,total,assisted_decisions,live_decisions,raw_approach\n");
	}
	Row += FString::Printf(TEXT("%.6f,%d,%lld,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%lld,%lld,%.9f\n"),
		FPlatformTime::Seconds() - StartRealTime, DecisionCount, Stats.Samples,
		Stats.DamageDealt, Stats.DamageTaken, Stats.Kill, Stats.Miss, Stats.Death, Stats.Time, Stats.See,
		Stats.Align, Stats.IndividualApplied, Stats.TeamApplied, Stats.Outcome, Stats.Clock, Stats.Total,
		AssistedDecisions, LiveDecisions, Stats.Approach);
	if (!FFileHelper::SaveStringToFile(Row, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append) && !bRewardLogWriteFailed)
	{
		bRewardLogWriteFailed = true;
		UE_LOG(LogTemp, Error, TEXT("[SH_Experiment] Cannot append reward log: %s"), *Path);
	}
}
