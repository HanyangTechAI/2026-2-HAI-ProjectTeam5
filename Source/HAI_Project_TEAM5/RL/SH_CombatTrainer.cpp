// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_CombatTrainer.h"

#include "Combat/CombatStatsComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SH_Arena.h"
#include "SH_RLBotController.h"
#include "SH_RLCharacter.h"
#include "SH_ScriptBotController.h"
#include "SH_ShooterInteractor.h"
#include "SH_ShooterTrainingEnv.h"
#include "SH_VisionComponent.h"

namespace
{
	/**
	 *  스냅샷 폴더 안의 파일 경로.
	 *  새 형식은 파일 이름 앞에 폴더 이름(= 누적 시간)이 붙는다: cum005h00m/cum005h00m_policy.bin.
	 *  그 파일이 없으면 옛 형식(Saved/SHSnapshots/.../policy.bin)으로 본다.
	 */
	FString SnapshotFile(const FString& Directory, const TCHAR* FileName)
	{
		const FString Prefixed = FPaths::Combine(Directory, FPaths::GetCleanFilename(Directory) + TEXT("_") + FileName);
		return FPaths::FileExists(Prefixed) ? Prefixed : FPaths::Combine(Directory, FileName);
	}

	bool HasPolicyFile(const FString& Directory)
	{
		return FPaths::FileExists(SnapshotFile(Directory, TEXT("policy.bin")));
	}

	/** meta.txt 의 key=value 줄을 읽는다. */
	bool ReadSnapshotMeta(const FString& Directory, TMap<FString, FString>& OutMeta)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *SnapshotFile(Directory, TEXT("meta.txt"))))
		{
			return false;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			FString Key, Value;
			if (Line.Split(TEXT("="), &Key, &Value))
			{
				OutMeta.Add(Key, Value);
			}
		}
		return true;
	}

	/**
	 *  그 스냅샷까지의 누적 학습 시간(초).
	 *  meta 에 cumulative_seconds 가 있으면 그 값(2026-10-14 부터 기록), 없으면
	 *  elapsed_seconds 에 resumed_from 스냅샷의 누적값을 더한다(옛 스냅샷은 이어 온 사슬을 따라간다).
	 */
	double ReadCumulativeSeconds(const FString& Directory, int32 Depth = 0)
	{
		TMap<FString, FString> Meta;
		if (Depth > 32 || !ReadSnapshotMeta(Directory, Meta))
		{
			return 0.0;
		}
		if (const FString* Cumulative = Meta.Find(TEXT("cumulative_seconds")))
		{
			return FCString::Atod(**Cumulative);
		}
		double Seconds = 0.0;
		if (const FString* Elapsed = Meta.Find(TEXT("elapsed_seconds")))
		{
			Seconds = FCString::Atod(**Elapsed);
		}
		const FString* From = Meta.Find(TEXT("resumed_from"));
		if (From && !From->IsEmpty() && *From != TEXT("-"))
		{
			Seconds += ReadCumulativeSeconds(*From, Depth + 1);
		}
		return Seconds;
	}

	/** 005h00m. 수십 시간 학습까지 이름 순 정렬이 시간 순과 같도록 시간을 세 자리로 쓴다. */
	FString FormatHoursMinutes(double Seconds)
	{
		const int64 TotalMinutes = FMath::FloorToInt64(FMath::Max(0.0, Seconds) / 60.0);
		return FString::Printf(TEXT("%03lldh%02lldm"), TotalMinutes / 60, TotalMinutes % 60);
	}
}

ASH_CombatTrainer::ASH_CombatTrainer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("LearningAgentsManager"));

	// 상대 정책(과거 스냅샷) 전용 매니저. 상대 혼합을 안 쓰면 에이전트 0개로 남는다.
	OpponentManager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("OpponentManager"));

	// ★ 기본값이 learning_core.train_behavior_cloning 이다. 반드시 바꾼다.
	ProcessSettings.TrainerFileName = TEXT("learning_core.train_ppo");
	ProcessSettings.TaskName        = TEXT("SHCombat");

	// 기본 10초는 파이썬 첫 기동에 모자라다.
	SharedMemorySettings.Timeout = 60.0f;

	// 에피소드 256스텝 = 20Hz 에서 12.8초. 아레나를 가로지르고(7초) 교전할 시간이 필요하다.
	TrainerSettings.MaxEpisodeStepNum = 256;

	// ★ 리플레이 버퍼 상한. 에이전트 수 x 게더당 결정 수 보다 커야 한다 —
	//   160 에이전트면 게더당 약 102 결정(5.1초 시뮬)에 해당한다.
	TrainerSettings.MaximumRecordedStepsPerIteration    = 16384;
	TrainerSettings.MaximumRecordedEpisodesPerIteration = 4000;

	// 기본 0.0 은 나쁜 기본값이다 — 엔트로피가 초반에 붕괴하면 탐색이 멈춘다.
	TrainingSettings.ActionEntropyWeight = 0.005f;

	// ★ 기본값 false. 안 켜면 학습된 정책이 디스크에 남지 않는다 —
	//   2026-10-01 까지의 1~5회차 결과가 전부 이렇게 사라졌다.
	//   IterationsPerSnapshot(기본 1000) 마다 Intermediate/LearningCore/<Task>/ 아래에 기록된다.
	TrainingSettings.bSaveSnapshots = true;

	// ★ 그래디언트 노름 클리핑. 엔진 기본값은 꺼짐이다.
	//   라운드 시스템(에피소드 약 200스텝, 결과 ±2.0, 시계 비용)으로 반환값의 크기와 분산이 커진 뒤
	//   "MaxIdx != INDEX_NONE" 크래시(로짓이 NaN/−∞)가 3~13분 간격으로 났다. 관측·메모리·인코딩은
	//   직전 스텝까지 유한했으므로(ScanForNonFinite) 신경망 출력 자체를 의심한다.
	//   PPO 구현 대부분이 쓰는 0.5 로 묶는다. -SHGradClip=0 으로 끌 수 있다(대조 실험용).
	TrainingSettings.bUseGradNormMaxClipping = true;
	TrainingSettings.GradNormMax = 0.5f;
	TrainingSettings.bUseTensorboard     = true;
	TrainingSettings.Device              = ELearningAgentsTrainingDevice::GPU;
}

void ASH_CombatTrainer::BeginPlay()
{
	Super::BeginPlay();

	StartRealTime = FPlatformTime::Seconds();
	LastReportRealTime = StartRealTime;

	// --- 커맨드라인 오버라이드 ------------------------------------------------
	// ★ 실행 길이를 바꿀 때마다 레벨 에셋을 커멘드릿으로 다시 저장하면
	//   (1) 에디터를 닫아야 하고 (2) .umap 이 계속 더러워진다.
	//   헤드리스 실행에서 길이만 바꾸는 일이 잦으므로 커맨드라인으로 받는다.
	//
	//   UnrealEditor-Cmd.exe <uproject> /Game/RL/Lvl_SHTrain -game -SHTrainSeconds=5400
	float OverrideSeconds = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHTrainSeconds="), OverrideSeconds)
		&& OverrideSeconds > 0.0f)
	{
		MaxWallClockSeconds = OverrideSeconds;
	}

	float OverrideReportInterval = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHReportInterval="), OverrideReportInterval)
		&& OverrideReportInterval >= 1.0f)
	{
		ReportInterval = OverrideReportInterval;
	}

	// 대치 실험용 스위치. 0 = 시간 초과는 무승부 + 축소 구역 끔(대조군), 1 = 둘 다 켬.
	int32 OverrideRoundRules = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHRoundRules="), OverrideRoundRules))
	{
		RoundRulesOverride = OverrideRoundRules;
	}
	float OverrideRoundTime = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHRoundTime="), OverrideRoundTime) && OverrideRoundTime >= 1.0f)
	{
		RoundTimeOverride = OverrideRoundTime;
	}
	float OverrideTeamSpirit = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHTeamSpirit="), OverrideTeamSpirit) && OverrideTeamSpirit >= 0.0f)
	{
		TeamSpiritOverride = FMath::Clamp(OverrideTeamSpirit, 0.0f, 1.0f);
	}
	float OverrideOutcome = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHOutcome="), OverrideOutcome) && OverrideOutcome >= 0.0f)
	{
		OutcomeOverride = OverrideOutcome;
	}
	int32 OverrideRoundRamp = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHRoundRamp="), OverrideRoundRamp) && OverrideRoundRamp >= 0)
	{
		RoundRampOverride = OverrideRoundRamp;
	}
	int32 OverrideSelfPlay = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHSelfPlay="), OverrideSelfPlay) && (OverrideSelfPlay == 0 || OverrideSelfPlay == 1))
	{
		bSelfPlay = (OverrideSelfPlay == 1);
	}
	int32 OverrideBuffer = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHBuffer="), OverrideBuffer) && OverrideBuffer >= 1024)
	{
		BufferOverride = OverrideBuffer;
	}
	int32 OverrideGradClip = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHGradClip="), OverrideGradClip) && (OverrideGradClip == 0 || OverrideGradClip == 1))
	{
		TrainingSettings.bUseGradNormMaxClipping = (OverrideGradClip == 1);
	}

	float OverrideSnapshotInterval = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHSnapshotInterval="), OverrideSnapshotInterval) && OverrideSnapshotInterval >= 0.0f)
	{
		SnapshotIntervalSeconds = OverrideSnapshotInterval;
	}
	SnapshotRunStamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	LastSnapshotRealTime = StartRealTime;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SHSnapshotRoot="), SnapshotRootDir) || SnapshotRootDir.IsEmpty())
	{
		SnapshotRootDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Snapshots"));
	}
	SnapshotRootDir = FPaths::ConvertRelativePathToFull(SnapshotRootDir);
	FPaths::NormalizeDirectoryName(SnapshotRootDir);

	// --- 이어서 학습하기 -------------------------------------------------------
	if (FParse::Value(FCommandLine::Get(), TEXT("SHResume="), ResumeDir) && !ResumeDir.IsEmpty())
	{
		FPaths::NormalizeDirectoryName(ResumeDir);
		UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] 이어서 학습 — 스냅샷 '%s'"), *ResumeDir);
	}

	// --- 평가 모드 -----------------------------------------------------------
	if (FParse::Value(FCommandLine::Get(), TEXT("SHEval="), EvalSnapshotDir) && !EvalSnapshotDir.IsEmpty())
	{
		bEvalMode = true;
		FPaths::NormalizeDirectoryName(EvalSnapshotDir);
	}
	float OverrideEvalNoise = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHEvalNoise="), OverrideEvalNoise) && OverrideEvalNoise >= 0.0f)
	{
		EvalActionNoise = OverrideEvalNoise;
	}
	int32 OverrideEvalRounds = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHEvalRounds="), OverrideEvalRounds) && OverrideEvalRounds >= 0)
	{
		EvalMaxRounds = OverrideEvalRounds;
	}
	int32 OverrideEvalRealtime = 0;
	const bool bRealtimeOnCommandLine = FParse::Value(FCommandLine::Get(), TEXT("SHEvalRealtime="), OverrideEvalRealtime);
	if (bRealtimeOnCommandLine)
	{
		bEvalRealtime = (OverrideEvalRealtime == 1);
	}

	// 레벨에 저장된 평가 설정 (Lvl_SHView). 명령줄이 없을 때만 쓴다.
	if (!bEvalMode && bEvalOnPlay && !EvalSnapshotDirectory.Path.IsEmpty())
	{
		bEvalMode = true;
		EvalSnapshotDir = EvalSnapshotDirectory.Path;
		FPaths::NormalizeDirectoryName(EvalSnapshotDir);
		if (!bRealtimeOnCommandLine)
		{
			bEvalRealtime = bEvalRealtimeOnPlay;
		}
		if (EvalOpponentDir.IsEmpty() && !EvalOpponentDirectory.Path.IsEmpty())
		{
			EvalOpponentDir = EvalOpponentDirectory.Path;
			FPaths::NormalizeDirectoryName(EvalOpponentDir);
		}
	}

	int32 OverrideDrawVision = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHDrawVision="), OverrideDrawVision) && (OverrideDrawVision == 0 || OverrideDrawVision == 1))
	{
		bDrawVisionDebug = (OverrideDrawVision == 1);
	}

	// --- 상대 혼합 -----------------------------------------------------------
	int32 OverrideMix = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHOpponentMix="), OverrideMix) && (OverrideMix == 0 || OverrideMix == 1))
	{
		bOpponentMix = (OverrideMix == 1);
	}
	int32 OverrideMixSnapshot = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHMixSnapshot="), OverrideMixSnapshot) && OverrideMixSnapshot >= 0)
	{
		MixSnapshotArenas = OverrideMixSnapshot;
	}
	int32 OverrideMixBot = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHMixBot="), OverrideMixBot) && OverrideMixBot >= 0)
	{
		MixBotArenas = OverrideMixBot;
	}
	float OverrideSwap = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("SHOpponentSwap="), OverrideSwap) && OverrideSwap >= 10.0f)
	{
		OpponentSwapSeconds = OverrideSwap;
	}
	FParse::Value(FCommandLine::Get(), TEXT("SHOpponentPool="), OpponentPoolDir);
	FPaths::NormalizeDirectoryName(OpponentPoolDir);   // 끝의 '/' 가 있으면 폴더 이름(파일 접두어)을 못 읽는다
	if (FParse::Value(FCommandLine::Get(), TEXT("SHEvalOpponent="), EvalOpponentDir) && !EvalOpponentDir.IsEmpty())
	{
		FPaths::NormalizeDirectoryName(EvalOpponentDir);
	}
	MixRandom.Initialize(Seed);
	LastOpponentSwapRealTime = StartRealTime;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 실행 한도 %.0f초, 리포트 주기 %.0f초, 스냅샷 주기 %.0f초 (%s/<날짜>/cum<누적 시간>, 실행 %s)."),
		MaxWallClockSeconds, ReportInterval, SnapshotIntervalSeconds, *SnapshotRootDir, *SnapshotRunStamp);
	if (bEvalMode)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] 평가 모드 — 스냅샷 '%s', 행동 잡음 x%.2f, 라운드 상한 %d, %s."),
			*EvalSnapshotDir, EvalActionNoise, EvalMaxRounds,
			bEvalRealtime ? TEXT("실제 시간") : TEXT("고정 델타타임(최대 속도)"));
	}

	// ★ 여기서 조립하지 않는다. 아레나가 아직 없을 수 있다 — 헤더 주석 참조.
	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] BeginPlay. 조립은 첫 Tick 에서 한다 (아레나 스폰 이후)."));
}

void ASH_CombatTrainer::CollectAgentUnits(TArray<ASH_RLCharacter*>& OutUnits, TArray<ABase_Character*>* OutOpponents) const
{
	OutUnits.Reset();
	if (OutOpponents)
	{
		OutOpponents->Reset();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ASH_RLCharacter> It(World); It; ++It)
	{
		ASH_RLCharacter* Unit = *It;
		if (!Unit)
		{
			continue;
		}

		// ★ 판별 기준은 팀이 아니라 컨트롤러다. 커리큘럼을 BP 설정만으로 바꿀 수 있다.
		if (!Cast<ASH_RLBotController>(Unit->GetController()))
		{
			continue;
		}

		// 스냅샷 아레나의 팀 1 은 RL 컨트롤러지만 학습 정책이 아니라 상대 정책이 조종한다.
		const ASH_Arena* Arena = Cast<ASH_Arena>(Unit->CachedGym.Get());
		const int32 Team = Unit->CombatStats ? static_cast<int32>(Unit->CombatStats->TeamId) : 0;
		if (Arena && !IsLearnerTeam(Arena, Team))
		{
			if (OutOpponents)
			{
				OutOpponents->Add(Unit);
			}
			continue;
		}

		OutUnits.Add(Unit);
	}
}

void ASH_CombatTrainer::AssignArenaRoles()
{
	ArenaRoles.Reset();

	// 유닛이 있는 아레나만, 이름순으로(실행마다 같은 배정).
	TArray<ASH_Arena*> Found;
	for (TActorIterator<ASH_Arena> It(GetWorld()); It; ++It)
	{
		if (*It && (It->GetTeamSize(0) + It->GetTeamSize(1)) > 0)
		{
			Found.Add(*It);
		}
	}
	Found.Sort([](const ASH_Arena& A, const ASH_Arena& B) { return A.GetName() < B.GetName(); });

	const bool bEvalVsSnapshot = bEvalMode && !EvalOpponentDir.IsEmpty();
	int32 SnapshotLeft = bEvalVsSnapshot ? Found.Num() : (bOpponentMix ? MixSnapshotArenas : 0);
	int32 BotLeft = bEvalVsSnapshot ? 0 : (bOpponentMix ? MixBotArenas : 0);

	int32 Counts[3] = { 0, 0, 0 };
	for (ASH_Arena* Arena : Found)
	{
		ESHArenaRole ArenaRole;
		if (SnapshotLeft > 0)
		{
			ArenaRole = ESHArenaRole::Snapshot;
			--SnapshotLeft;
		}
		else if (BotLeft > 0)
		{
			ArenaRole = ESHArenaRole::Bot;
			--BotLeft;
		}
		else
		{
			// 혼합이 아니면 예전 방식: bSelfPlay 면 self-play, 아니면 봇.
			ArenaRole = (bOpponentMix || bSelfPlay) ? ESHArenaRole::SelfPlay : ESHArenaRole::Bot;
		}
		ArenaRoles.Add(Arena, ArenaRole);
		LastRoundIndex.Add(Arena, Arena->GetRoundIndex());
		++Counts[static_cast<int32>(ArenaRole)];
	}

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 아레나 역할 — self-play %d / 스냅샷 상대 %d / 스크립트 봇 %d%s"),
		Counts[0], Counts[1], Counts[2], bOpponentMix ? TEXT(" (상대 혼합)") : TEXT(""));
}

ESHArenaRole ASH_CombatTrainer::GetArenaRole(const ASH_Arena* Arena) const
{
	const ESHArenaRole* ArenaRole = Arena ? ArenaRoles.Find(const_cast<ASH_Arena*>(Arena)) : nullptr;
	return ArenaRole ? *ArenaRole : (bSelfPlay ? ESHArenaRole::SelfPlay : ESHArenaRole::Bot);
}

bool ASH_CombatTrainer::IsLearnerTeam(const ASH_Arena* Arena, const int32 Team) const
{
	return Team == 0 || GetArenaRole(Arena) == ESHArenaRole::SelfPlay;
}

bool ASH_CombatTrainer::NeedsRLTeam1(const ASH_Arena* Arena) const
{
	return GetArenaRole(Arena) != ESHArenaRole::Bot;
}

bool ASH_CombatTrainer::ValidateUnitSchema(const TArray<ASH_RLCharacter*>& Units) const
{
	bool bOk = true;

	for (const ASH_RLCharacter* Unit : Units)
	{
		const USH_VisionComponent* Vision = Unit ? Unit->Vision.Get() : nullptr;
		if (!Vision)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_CombatTrainer] %s 에 USH_VisionComponent 가 없다."),
				Unit ? *Unit->GetName() : TEXT("(null)"));
			bOk = false;
			continue;
		}

		// ★ 관측 스키마의 StaticArray 크기는 SHRL::RayCount 로 고정이다.
		//   유닛의 RayCount 가 다르면 부채꼴 길이가 어긋나 벡터 크기가 깨진다.
		if (Vision->RayCount != SHRL::RayCount)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_CombatTrainer] %s: Vision->RayCount(%d) != 스키마 RayCount(%d). ")
				TEXT("BP 유닛의 Vision 설정을 고칠 것 — 스키마는 코드 상수다."),
				*Unit->GetName(), Vision->RayCount, SHRL::RayCount);
			bOk = false;
		}

		// ★ 집합 관측의 최대 크기는 코드 상수다. 팀 인원이 더 많으면 넘치는 적/아군이 관측에서 잘린다.
		if (const ASH_Arena* Arena = Cast<ASH_Arena>(Unit->CachedGym.Get()))
		{
			const int32 MyTeam = Unit->CombatStats ? static_cast<int32>(Unit->CombatStats->TeamId) : 0;
			if (Arena->GetTeamSize(1 - MyTeam) > SHRL::MaxEnemies || Arena->GetTeamSize(MyTeam) - 1 > SHRL::MaxAllies)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[SH_CombatTrainer] %s: 아레나 팀 인원(아군 %d, 적 %d)이 스키마 상한(MaxAllies %d + 자신, MaxEnemies %d)을 넘는다."),
					*Unit->GetName(), Arena->GetTeamSize(MyTeam), Arena->GetTeamSize(1 - MyTeam), SHRL::MaxAllies, SHRL::MaxEnemies);
				bOk = false;
			}
		}

		// ★ 피격 기억 지평은 Units[0] 것만 인터랙터로 복사된다 (Assemble 참조).
		//   유닛마다 다르면 그 유닛의 클램프 상한과 정규화 Scale 이 어긋나서
		//   TimeSinceHit 이 0~1 을 벗어난다. 에러는 나지 않는다.
		if (!FMath::IsNearlyEqual(Unit->HitMemoryHorizonSeconds, Units[0]->HitMemoryHorizonSeconds))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[SH_CombatTrainer] %s: HitMemoryHorizonSeconds(%.2f) 가 다른 유닛(%.2f)과 다르다. ")
				TEXT("정규화 Scale 은 하나뿐이므로 모든 유닛이 같아야 한다."),
				*Unit->GetName(), Unit->HitMemoryHorizonSeconds, Units[0]->HitMemoryHorizonSeconds);
			bOk = false;
		}
	}

	return bOk;
}

void ASH_CombatTrainer::Assemble()
{
	bAssembleAttempted = true;

	if (!Manager)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] Manager 가 없다."));
		return;
	}

	// --- 0. 아레나 역할 (상대 혼합 / 평가 상대) --------------------------------
	AssignArenaRoles();

	// --- 0a. self-play · 스냅샷 아레나: 팀 1 의 컨트롤러를 RL 컨트롤러로 바꿔 끼운다 ----
	if (bSelfPlay || bOpponentMix || !EvalOpponentDir.IsEmpty())
	{
		int32 Swapped = 0;
		for (TActorIterator<ASH_RLCharacter> It(GetWorld()); It; ++It)
		{
			ASH_RLCharacter* Unit = *It;
			if (!Unit || !Unit->CachedGym)
			{
				continue;   // 아레나 소속 유닛만
			}
			if (!NeedsRLTeam1(Cast<ASH_Arena>(Unit->CachedGym.Get())))
			{
				continue;   // 봇 아레나는 스크립트 봇을 그대로 둔다
			}

			AController* OldController = Unit->GetController();
			if (OldController && OldController->IsA<ASH_RLBotController>())
			{
				continue;   // 이미 RL
			}
			if (OldController)
			{
				OldController->UnPossess();
				OldController->Destroy();
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ASH_RLBotController* NewController = GetWorld()->SpawnActor<ASH_RLBotController>(
				ASH_RLBotController::StaticClass(), Unit->GetActorTransform(), SpawnParams);
			if (NewController)
			{
				// OnPossess 가 회전 모드를 설정한다. 컨트롤 회전은 다음 아레나 리셋의
				// ApplySpawnSlots 가 슬롯 방향으로 맞춘다(bResetAgentsOnBegin = true).
				NewController->Possess(Unit);
				++Swapped;
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] 팀 1 유닛 %d개의 컨트롤러를 SH_RLBotController 로 교체했다."), Swapped);
	}

	// 게더당 기록 스텝. self-play 는 에이전트가 두 배라 버퍼도 키운다.
	if (BufferOverride >= 1024)
	{
		TrainerSettings.MaximumRecordedStepsPerIteration = BufferOverride;
	}
	else if (bSelfPlay || bOpponentMix)
	{
		TrainerSettings.MaximumRecordedStepsPerIteration = SelfPlayRecordedSteps;
	}

	// --- 0b. 에이전트가 될 유닛을 먼저 찾는다 ----------------------------------
	TArray<ASH_RLCharacter*> Units;
	TArray<ABase_Character*> OpponentUnits;
	CollectAgentUnits(Units, &OpponentUnits);

	// 시야 그리기 (시각 확인용). RL 정책이 조종하는 유닛 전부 — 학습 정책과 상대 정책.
	if (bDrawVisionDebug)
	{
		auto EnableDraw = [](ABase_Character* Unit)
		{
			if (USH_VisionComponent* Vision = Unit ? Unit->FindComponentByClass<USH_VisionComponent>() : nullptr)
			{
				Vision->bDrawDebug = true;
			}
		};
		for (ASH_RLCharacter* Unit : Units) { EnableDraw(Unit); }
		for (ABase_Character* Unit : OpponentUnits) { EnableDraw(Unit); }
		UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] 시야 그리기 켬 — 유닛 %d개."), Units.Num() + OpponentUnits.Num());
	}

	if (Units.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_CombatTrainer] 정책이 조종할 유닛이 0개다. 확인할 것: ")
			TEXT("(1) 레벨에 ASH_RLDirector 가 있고 GymsManager->Start() 가 불렸는가, ")
			TEXT("(2) BP_SHArena 의 GymTemplates 가 설정됐는가, ")
			TEXT("(3) 유닛 BP 의 AIControllerClass 가 SH_RLBotController 인가."));
		return;
	}

	if (!ValidateUnitSchema(Units))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] 유닛 스키마 검증 실패 — 조립을 중단한다."));
		return;
	}

	if (Units.Num() > MaxAgentNum)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_CombatTrainer] 유닛 %d개가 MaxAgentNum(%d) 을 넘는다. ")
			TEXT("넘치는 유닛은 등록되지 않고 정책이 영원히 무시한다 — MaxAgentNum 을 올릴 것."),
			Units.Num(), MaxAgentNum);
	}

	// --- 1. 리스너를 만들기 전에 최대 에이전트 수를 정한다 ---------------------
	Manager->SetMaxAgentNum(FMath::Max(MaxAgentNum, Units.Num()));

	ULearningAgentsManager* ManagerPtr = Manager;

	// --- 2. Interactor -------------------------------------------------------
	ULearningAgentsInteractor* InteractorPtr = ULearningAgentsInteractor::MakeInteractor(
		ManagerPtr, USH_ShooterInteractor::StaticClass(), TEXT("ShooterInteractor"));

	Interactor = Cast<USH_ShooterInteractor>(InteractorPtr);
	if (!Interactor)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] MakeInteractor 실패."));
		return;
	}

	// 관측 정규화 Scale 을 유닛의 기억 지평과 맞춘다. 어긋나면 TimeSince 가 0~1 을 벗어난다.
	if (const USH_VisionComponent* Vision = Units[0]->Vision.Get())
	{
		Interactor->MemoryHorizonSeconds = Vision->MemoryHorizonSeconds;
	}

	// 피격 기억도 같은 이유로 맞춘다. 이쪽은 컴포넌트가 아니라 폰이 들고 있다.
	Interactor->HitMemoryHorizonSeconds = Units[0]->HitMemoryHorizonSeconds;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 스키마 크기 — 관측 %d (인코딩 %d) / 행동 %d (분포 %d, 모디파이어 %d)"),
		Interactor->GetObservationVectorSize(),
		Interactor->GetObservationEncodedVectorSize(),
		Interactor->GetActionVectorSize(),
		Interactor->GetActionDistributionVectorSize(),
		Interactor->GetActionModifierVectorSize());

	// --- 3. Policy / 4. Critic ----------------------------------------------
	ULearningAgentsPolicy* PolicyPtr = ULearningAgentsPolicy::MakePolicy(
		ManagerPtr, InteractorPtr, ULearningAgentsPolicy::StaticClass(), TEXT("ShooterPolicy"),
		/*Encoder*/ nullptr, /*Policy*/ nullptr, /*Decoder*/ nullptr,
		/*bReinitEncoder*/ true, /*bReinitPolicy*/ true, /*bReinitDecoder*/ true,
		FLearningAgentsPolicySettings(), Seed);
	Policy = PolicyPtr;

	// 평가 모드: 무작위 초기화 위에 스냅샷을 덮어쓴다. 크리틱은 만들지 않는다(추론에 안 쓴다).
	ULearningAgentsCritic* CriticPtr = nullptr;
	if (bEvalMode)
	{
		if (!LoadPolicySnapshot(EvalSnapshotDir))
		{
			UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] 평가 스냅샷을 불러오지 못했다 — 조립을 중단한다."));
			return;
		}
	}
	else
	{
		CriticPtr = ULearningAgentsCritic::MakeCritic(
			ManagerPtr, InteractorPtr, PolicyPtr, ULearningAgentsCritic::StaticClass(), TEXT("ShooterCritic"),
			/*Network*/ nullptr, /*bReinit*/ true, FLearningAgentsCriticSettings(), Seed);
		Critic = CriticPtr;
	}

	// --- 5. TrainingEnvironment ---------------------------------------------
	ULearningAgentsTrainingEnvironment* EnvPtr = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(
		ManagerPtr, USH_ShooterTrainingEnv::StaticClass(), TEXT("ShooterTrainingEnv"));

	TrainingEnvironment = Cast<USH_ShooterTrainingEnv>(EnvPtr);
	if (!TrainingEnvironment)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] MakeTrainingEnvironment 실패."));
		return;
	}

	if (TeamSpiritOverride >= 0.0f)
	{
		TrainingEnvironment->TeamSpirit = TeamSpiritOverride;
	}
	if (OutcomeOverride >= 0.0f)
	{
		TrainingEnvironment->OutcomeReward = OutcomeOverride;
	}
	if (RoundRampOverride >= 0)
	{
		TrainingEnvironment->RoundRewardRampSteps = RoundRampOverride;
	}

	// ★ 시계 비용의 상쇄는 학습에 실제로 쓰는 γ 와 같을 때만 정확하다.
	TrainingEnvironment->ClockDiscount = TrainingSettings.DiscountFactor;

	// 이어서 학습하기: 네트워크 4개 + 보상 수집 누적 횟수. 환경이 생긴 뒤여야 한다.
	if (!bEvalMode && !ResumeDir.IsEmpty() && !LoadResumeSnapshot(ResumeDir))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] 이어서 학습할 스냅샷을 불러오지 못했다 — 조립을 중단한다."));
		return;
	}

	// --- 아레나: 라운드 지표 수집 대상 + 커맨드라인 오버라이드 ----------------
	Arenas.Reset();
	AgentTeamMask = 0;
	for (const ASH_RLCharacter* Unit : Units)
	{
		if (Unit && Unit->CombatStats)
		{
			AgentTeamMask |= 1 << static_cast<int32>(Unit->CombatStats->TeamId);
		}
	}
	for (TActorIterator<ASH_Arena> It(GetWorld()); It; ++It)
	{
		ASH_Arena* Arena = *It;
		if (!Arena || Arena->GetTeamSize(0) + Arena->GetTeamSize(1) == 0)
		{
			continue;   // 레벨에 편집용으로 놓은 빈 아레나
		}
		if (RoundRulesOverride == 0 || RoundRulesOverride == 1)
		{
			Arena->bTimeoutIsLossForBoth = (RoundRulesOverride == 1);
			Arena->bShrinkZone = (RoundRulesOverride == 1);
		}
		if (RoundTimeOverride >= 1.0f)
		{
			Arena->RoundTimeLimitSeconds = RoundTimeOverride;
		}
		// 상대 혼합: self-play 아레나는 승패 보상 0 (ASH_Arena::OutcomeRewardScale 주석).
		Arena->OutcomeRewardScale = (bOpponentMix && GetArenaRole(Arena) == ESHArenaRole::SelfPlay) ? 0.0f : 1.0f;
		Arenas.Add(Arena);
	}

	// 대조군(-SHRoundRules=0)에는 대치 방지 장치가 하나도 없어야 한다 — 시계 비용도 끈다.
	if (RoundRulesOverride == 0 || RoundRulesOverride == 1)
	{
		TrainingEnvironment->bRoundClockCost = (RoundRulesOverride == 1);
	}

	if (const ASH_Arena* First = Arenas.Num() > 0 ? Arenas[0].Get() : nullptr)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] 라운드 규칙 — 아레나 %d개, 상한 %.0f초, 시간 초과 = %s, 축소 구역 %s ")
			TEXT("(%.0f초부터 %.0f초 동안 %.0f → %.0f cm, 초당 %.0f), 스폰 지터 ±%.0f° / ±%.0f cm"),
			Arenas.Num(), First->RoundTimeLimitSeconds,
			First->bTimeoutIsLossForBoth ? TEXT("양쪽 패배") : TEXT("무승부"),
			First->bShrinkZone ? TEXT("켬") : TEXT("끔"),
			First->ZoneStartSeconds, First->ZoneShrinkSeconds, First->ArenaRadius, First->ZoneFinalRadius,
			First->ZoneDamagePerSecond, First->SpawnAngleJitterDegrees, First->SpawnRadiusJitter);
	}
	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 보상 — 준/받은 피해 %.2f/%.2f, 킬/사망 %.2f/%.2f, 빗맞힘 %.3f, 결과 ±%.1f, 팀 스피릿 %.2f ")
		TEXT("(램프 %d), 시계 비용 %s (γ %.3f → 최대 %.4f/스텝), 갱신 시 리셋 %s, 스냅샷 저장 %s, 그래디언트 클리핑 %s"),
		TrainingEnvironment->DamageDealtWeight, TrainingEnvironment->DamageTakenWeight,
		TrainingEnvironment->KillBonus, TrainingEnvironment->DeathPenalty, TrainingEnvironment->MissWeight,
		TrainingEnvironment->OutcomeReward, TrainingEnvironment->TeamSpirit,
		TrainingEnvironment->RoundRewardRampSteps,
		TrainingEnvironment->bRoundClockCost ? TEXT("켬") : TEXT("끔"),
		TrainingEnvironment->ClockDiscount,
		TrainingEnvironment->bRoundClockCost
			? TrainingEnvironment->OutcomeReward * (1.0f - TrainingEnvironment->ClockDiscount) : 0.0f,
		bResetAgentsOnUpdate ? TEXT("켬") : TEXT("끔"),
		TrainingSettings.bSaveSnapshots ? TEXT("켬") : TEXT("끔"),
		TrainingSettings.bUseGradNormMaxClipping ? TEXT("켬") : TEXT("끔"));

	// --- 날짜별 학습 기록(md)의 실행 머리말 ---------------------------------------
	// 무엇으로 학습했는지는 코드만 봐서는 알 수 없다(명령줄 오버라이드가 많다). 실행마다 남긴다.
	if (!bEvalMode)
	{
		const FDateTime Now = FDateTime::Now();
		RunLogHeader = FString::Printf(
			TEXT("\n### 실행 %s (시작 %s)\n")
			TEXT("- 시작 정책: %s\n")
			TEXT("- 실행 한도 %s, 스냅샷 주기 %s\n")
			TEXT("- self-play %s, 상대 혼합 %s, 학습 에이전트 %d, 관측 크기 %d, 버퍼 %d 스텝\n")
			TEXT("- 보상: 결과 ±%.1f, 팀 스피릿 %.2f, 시간 초과 %s, 축소 구역 %s\n")
			TEXT("- 스냅샷:\n"),
			*SnapshotRunStamp, *Now.ToString(TEXT("%Y-%m-%d %H:%M")),
			ResumeDir.IsEmpty()
				? TEXT("무작위 초기화 (누적 0)")
				: *FString::Printf(TEXT("`%s` 에서 이어서 (누적 %s 부터)"), *ResumeDir, *FormatHoursMinutes(CumulativeBaseSeconds)),
			MaxWallClockSeconds > 0.0f ? *FormatHoursMinutes(MaxWallClockSeconds) : TEXT("없음"),
			SnapshotIntervalSeconds > 0.0f ? *FormatHoursMinutes(SnapshotIntervalSeconds) : TEXT("끔 (종료 시에만)"),
			bSelfPlay ? TEXT("켬") : TEXT("끔"),
			bOpponentMix ? *FString::Printf(TEXT("켬 (스냅샷 %d / 봇 %d 아레나)"), MixSnapshotArenas, MixBotArenas) : TEXT("끔"),
			Units.Num(), Interactor->GetObservationVectorSize(), TrainerSettings.MaximumRecordedStepsPerIteration,
			TrainingEnvironment->OutcomeReward, TrainingEnvironment->TeamSpirit,
			(Arenas.Num() > 0 && Arenas[0].IsValid() && Arenas[0]->bTimeoutIsLossForBoth) ? TEXT("양쪽 패배") : TEXT("무승부"),
			(Arenas.Num() > 0 && Arenas[0].IsValid() && Arenas[0]->bShrinkZone) ? TEXT("켬") : TEXT("끔"));
		AppendDailyLog(FString());
	}

	// --- 상대 정책 (스냅샷 아레나의 팀 1) ---------------------------------------
	if (OpponentUnits.Num() > 0 && !AssembleOpponent(OpponentUnits))
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] 상대 정책 조립 실패 — 조립을 중단한다."));
		return;
	}

	// --- 평가 모드: 파이썬·학습기 없이 바로 에이전트 등록 -----------------------
	if (bEvalMode)
	{
		Agents.Reset(Units.Num());
		ArenaAgentIds.Reset();
		for (ASH_RLCharacter* Unit : Units)
		{
			const int32 AgentId = Manager->AddAgent(Unit);
			Agents.Add(Unit);
			if (ASH_Arena* Arena = Cast<ASH_Arena>(Unit->CachedGym.Get()))
			{
				ArenaAgentIds.FindOrAdd(Arena).Add(AgentId);
			}
		}

		// ★ 학습 때와 같은 고정 델타타임으로 돌린다(BeginTraining 이 하는 일). 끄면 프레임률에 따라
		//   한 결정 사이의 시뮬레이션 시간이 달라져 학습 때와 다른 게임이 된다.
		if (!bEvalRealtime)
		{
			UE::Learning::Agents::ApplyGameSettings(GameSettings, GetWorld(), EvalGameSettingsState);
		}

		bAssembled = true;
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] 평가 조립 완료 — 에이전트 %d, 아레나 %d, 상대 = %s."),
			Agents.Num(), ArenaAgentIds.Num(),
			bHasOpponents ? *FString::Printf(TEXT("스냅샷 '%s'"), *CurrentOpponentName)
				: (bSelfPlay ? TEXT("같은 정책") : TEXT("팀 1 기본 컨트롤러(스크립트 봇)")));
		return;
	}

	// --- 6. 파이썬 트레이너 프로세스 -----------------------------------------
	UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] 파이썬 트레이너 기동 — 모듈 '%s', 타임아웃 %.0f초"),
		*ProcessSettings.TrainerFileName, SharedMemorySettings.Timeout);

	Communicator = ULearningAgentsCommunicatorLibrary::MakeSharedMemoryTrainingProcess(
		ProcessSettings, SharedMemorySettings);

	if (!Communicator.Trainer.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SH_CombatTrainer] 트레이너 프로세스 기동 실패. Intermediate/PipInstall 이 ")
			TEXT("설치됐는지, 경로에 한글/공백이 없는지 확인할 것."));
		return;
	}

	// --- 7. PPOTrainer -------------------------------------------------------
	Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(
		ManagerPtr, InteractorPtr, EnvPtr, PolicyPtr, CriticPtr,
		Communicator, ULearningAgentsPPOTrainer::StaticClass(), TEXT("ShooterPPOTrainer"),
		TrainerSettings);

	if (!Trainer)
	{
		UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] MakePPOTrainer 실패."));
		return;
	}

	// --- 8. ★ 에이전트 등록은 맨 마지막 --------------------------------------
	// 리스너가 생기기 전에 추가된 에이전트는 그 리스너에게 영원히 보이지 않고, 에러도 없다.
	Agents.Reset(Units.Num());
	for (ASH_RLCharacter* Unit : Units)
	{
		// ★ 폰을 등록한다. 풀이 재사용하는 것이 폰이고(DisableEntity 가 Destroy 하지 않는다),
		//   컨트롤러는 원칙적으로 교체될 수 있다.
		Manager->AddAgent(Unit);
		Agents.Add(Unit);
	}

	bAssembled = true;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 조립 완료 — 에이전트 %d, 결정 %.0fHz, 에피소드 %d스텝(%.1f초), ")
		TEXT("게더당 %d스텝, 디바이스 %s"),
		Agents.Num(), DecisionRateHz,
		TrainerSettings.MaxEpisodeStepNum,
		TrainerSettings.MaxEpisodeStepNum / FMath::Max(1.0f, DecisionRateHz),
		TrainerSettings.MaximumRecordedStepsPerIteration,
		TrainingSettings.Device == ELearningAgentsTrainingDevice::GPU ? TEXT("GPU") : TEXT("CPU"));
}

void ASH_CombatTrainer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 첫 Tick 에서 한 번만 조립한다. 실패했으면 매 프레임 재시도하지 않는다 —
	// 같은 에러가 로그를 도배하면 진짜 원인이 묻힌다.
	if (!bAssembled)
	{
		if (!bAssembleAttempted)
		{
			Assemble();
		}
		return;
	}

	if (!Trainer && !bEvalMode)
	{
		return;
	}

	const double RealNow = FPlatformTime::Seconds();

	// --- 평가 모드의 종료 조건 -------------------------------------------------
	if (bEvalMode)
	{
		const bool bTimeUp = MaxWallClockSeconds > 0.0f && (RealNow - StartRealTime) >= MaxWallClockSeconds;
		const bool bRoundsDone = EvalMaxRounds > 0 && TotalRounds >= EvalMaxRounds;
		if (bTimeUp || bRoundsDone)
		{
			Report(RealNow - LastReportRealTime);
			LogEvaluationSummary();
			if (!bEvalRealtime)
			{
				UE::Learning::Agents::RevertGameSettings(EvalGameSettingsState, GetWorld());
			}
			bAssembled = false;
			if (!GIsEditor)
			{
				FPlatformMisc::RequestExit(false);
			}
			return;
		}
	}

	// ★★ IsTraining() 으로 **먼저** 걸러서는 안 된다.
	//    RunTraining 이 첫 호출에서 BeginTraining 을 하므로, 시작 전에는 IsTraining() 이
	//    false 다. 앞에서 걸러 버리면 학습이 영원히 시작되지 않고 에러도 나지 않는다 —
	//    조립 로그까지는 정상으로 보이고 그 뒤로 아무 일도 일어나지 않는다.
	//    (SH_HelloWorldTrainer.cpp:292 도 무조건 부르고, IsTraining 은 종료 판정에만 쓴다.)
	if (!bEvalMode && bTrainingStarted && !Trainer->IsTraining())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SH_CombatTrainer] 학습이 종료됐다 (파이썬 프로세스 종료 또는 EndTraining). ")
			TEXT("결정 %d회 수행."), DecisionCount);
		bAssembled = false;
		return;
	}

	// --- 벽시계 상한 --------------------------------------------------------
	if (!bEvalMode && MaxWallClockSeconds > 0.0f && (RealNow - StartRealTime) >= MaxWallClockSeconds)
	{
		Report(RealNow - LastReportRealTime);
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer] MaxWallClockSeconds(%.0f초) 도달 — 학습을 종료한다."),
			MaxWallClockSeconds);

		// 마지막 정책을 남긴다. EndTraining 뒤에는 네트워크가 더 갱신되지 않지만, 순서를 앞에 둬서
		// 파이썬 종료 처리와 겹치지 않게 한다.
		if (bTrainingStarted)
		{
			SaveVersionedSnapshot(/*bFinal=*/true);
		}

		Trainer->EndTraining();
		bAssembled = false;

		if (!GIsEditor)
		{
			FPlatformMisc::RequestExit(false);
		}
		return;
	}

	// --- 결정 주기 ----------------------------------------------------------
	const float DecisionPeriod = 1.0f / FMath::Max(1.0f, DecisionRateHz);
	DecisionAccumulator += DeltaSeconds;
	if (DecisionAccumulator < DecisionPeriod)
	{
		return;
	}

	// ★ 0 으로 리셋하지 않고 주기만큼 뺀다. 프레임 지터가 있어도 장기 평균 주기가 유지된다.
	DecisionAccumulator -= DecisionPeriod;
	++DecisionCount;

	// --- 지표 표본 (RunTraining 보다 먼저) -----------------------------------
	// ★ 순서가 중요하다. RunTraining 안의 GatherAgentReward 가
	//   ConsumeRewardAccumulators() 로 사격 누산기를 비우므로, 그 전에 읽어야 한다.
	{
		int32 Alive = 0;
		int32 Visible = 0;
		for (const ASH_RLCharacter* Unit : Agents)
		{
			const UCombatStatsComponent* Stats = Unit ? Unit->CombatStats.Get() : nullptr;
			if (!Stats)
			{
				continue;
			}

			ShotsFiredTotal += Stats->ShotsFiredAccum;
			ShotsHitTotal   += Stats->ShotsHitAccum;

			// 역할별 지표 — 상대 혼합에서 어느 아레나가 학습을 끌어내리는지 가른다(TODO 1B.14).
			{
				const int32 RoleIndex = static_cast<int32>(GetArenaRole(Cast<ASH_Arena>(Unit->CachedGym.Get())));
				RoleShots[RoleIndex]       += Stats->ShotsFiredAccum;
				RoleHits[RoleIndex]        += Stats->ShotsHitAccum;
				RoleDamageTaken[RoleIndex] += Stats->DamageTakenAccum;
				RoleDamageDealt[RoleIndex] += Stats->DamageDealtAccum;
				if (Stats->IsAlive() && Unit->bEntityEnabled)
				{
					++RoleAliveDecisions[RoleIndex];
				}
			}

			if (Stats->IsAlive() && Unit->bEntityEnabled)
			{
				++Alive;
				if (const USH_VisionComponent* Vision = Unit->Vision.Get())
				{
					if (Vision->IsHostileVisible())
					{
						++Visible;
					}
				}

				// 최근접 적 거리 — 아레나의 유닛 캐시만 훑는다(적 10명).
				if (const ASH_Arena* Arena = Cast<ASH_Arena>(Unit->CachedGym.Get()))
				{
					const int32 Other = 1 - static_cast<int32>(Stats->TeamId);
					const FVector MyLocation = Unit->GetActorLocation();
					float BestSq = TNumericLimits<float>::Max();
					for (const TWeakObjectPtr<ABase_Character>& Weak : Arena->GetTeamUnits(Other))
					{
						const ABase_Character* Enemy = Weak.Get();
						if (Enemy && Enemy->bEntityEnabled && Enemy->CombatStats && Enemy->CombatStats->IsAlive())
						{
							BestSq = FMath::Min(BestSq, FVector::DistSquaredXY(MyLocation, Enemy->GetActorLocation()));
						}
					}
					if (BestSq < TNumericLimits<float>::Max())
					{
						NearestEnemyDistanceSum += FMath::Sqrt(BestSq);
						++NearestEnemySamples;
					}
				}
			}
		}

		const int32 Total = FMath::Max(1, Agents.Num());
		AliveAccumulator   += static_cast<double>(Alive)   / Total;
		VisibleAccumulator += static_cast<double>(Visible) / Total;
		++MetricSampleCount;
	}

	// --- 학습 한 스텝 --------------------------------------------------------
	// ★ 이 한 줄이 전부다. RunTraining 이 내부에서 GatherCompletions / GatherRewards /
	//   ProcessExperience / RunInference(= GatherObservations + MakeActionModifiers +
	//   EncodeObservations + EvaluatePolicy + DecodeAndSampleActions + PerformActions)
	//   를 전부 부른다. 바깥에서 또 부르면 스텝이 이중 수집된다.
	if (bEvalMode)
	{
		TickEvaluation();
	}
	else
	{
		Trainer->RunTraining(TrainingSettings, GameSettings, /*bResetAgentsOnBegin=*/true, bResetAgentsOnUpdate);
		bTrainingStarted = true;

		ScanForNonFinite();

		// --- 버전별 스냅샷 ------------------------------------------------------
		if (SnapshotIntervalSeconds > 0.0f && (RealNow - LastSnapshotRealTime) >= SnapshotIntervalSeconds)
		{
			SaveVersionedSnapshot(/*bFinal=*/false);
			LastSnapshotRealTime = RealNow;
		}
	}

	// ★ 학습 정책의 스텝(리셋 포함) 뒤에 부른다 — 이 프레임에 리셋된 아레나를 보고
	//   상대 메모리를 지운 다음 상대 행동을 뽑아야 한다.
	TickOpponents(RealNow);

	// --- 리포트 -------------------------------------------------------------
	if ((RealNow - LastReportRealTime) >= ReportInterval)
	{
		Report(RealNow - LastReportRealTime);
		LastReportRealTime = RealNow;
	}
}

bool ASH_CombatTrainer::AssembleOpponent(const TArray<ABase_Character*>& OpponentUnits)
{
	if (!OpponentManager || !Policy)
	{
		return false;
	}

	OpponentManager->SetMaxAgentNum(FMath::Max(OpponentUnits.Num(), 1));

	// 학습 정책과 같은 스키마의 Interactor/Policy. 학습기·크리틱·환경은 없다 —
	// 리셋은 학습 쪽 환경이 아레나 전체를 리셋할 때 같이 되고, 메모리만 여기서 지운다(TickOpponents).
	ULearningAgentsManager* OpponentManagerPtr = OpponentManager;
	ULearningAgentsInteractor* OpponentInteractorPtr = ULearningAgentsInteractor::MakeInteractor(
		OpponentManagerPtr, USH_ShooterInteractor::StaticClass(), TEXT("OpponentInteractor"));
	OpponentInteractor = Cast<USH_ShooterInteractor>(OpponentInteractorPtr);
	if (!OpponentInteractor)
	{
		return false;
	}
	OpponentInteractor->MemoryHorizonSeconds = Interactor->MemoryHorizonSeconds;
	OpponentInteractor->HitMemoryHorizonSeconds = Interactor->HitMemoryHorizonSeconds;

	OpponentPolicy = ULearningAgentsPolicy::MakePolicy(
		OpponentManagerPtr, OpponentInteractorPtr, ULearningAgentsPolicy::StaticClass(), TEXT("OpponentPolicy"),
		nullptr, nullptr, nullptr, true, true, true, FLearningAgentsPolicySettings(), Seed + 1);
	if (!OpponentPolicy)
	{
		return false;
	}

	OpponentArenaAgentIds.Reset();
	for (ABase_Character* Unit : OpponentUnits)
	{
		const int32 AgentId = OpponentManager->AddAgent(Unit);
		if (ASH_Arena* Arena = Cast<ASH_Arena>(Unit->CachedGym.Get()))
		{
			OpponentArenaAgentIds.FindOrAdd(Arena).Add(AgentId);
		}
	}
	bHasOpponents = true;

	// 첫 상대.
	if (bEvalMode)
	{
		if (!LoadOpponentSnapshot(EvalOpponentDir))
		{
			return false;
		}
		CurrentOpponentName = FPaths::GetCleanFilename(EvalOpponentDir);
	}
	else
	{
		SwapOpponent();
	}

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 상대 정책 조립 — 유닛 %d, 아레나 %d, 첫 상대 '%s', 교체 주기 %.0f초."),
		OpponentUnits.Num(), OpponentArenaAgentIds.Num(), *CurrentOpponentName, OpponentSwapSeconds);
	return true;
}

bool ASH_CombatTrainer::LoadOpponentSnapshot(const FString& Directory)
{
	if (!OpponentPolicy)
	{
		return false;
	}
	const TCHAR* Files[] = { TEXT("policy.bin"), TEXT("encoder.bin"), TEXT("decoder.bin") };
	for (const TCHAR* File : Files)
	{
		if (!FPaths::FileExists(SnapshotFile(Directory, File)))
		{
			UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] 상대 스냅샷 파일이 없다: %s"), *SnapshotFile(Directory, File));
			return false;
		}
	}
	auto Load = [&Directory](ULearningAgentsNeuralNetwork* Network, const TCHAR* FileName)
	{
		FFilePath Path;
		Path.FilePath = SnapshotFile(Directory, FileName);
		Network->LoadNetworkFromSnapshot(Path);
	};
	Load(OpponentPolicy->GetPolicyNetworkAsset(),  TEXT("policy.bin"));
	Load(OpponentPolicy->GetEncoderNetworkAsset(), TEXT("encoder.bin"));
	Load(OpponentPolicy->GetDecoderNetworkAsset(), TEXT("decoder.bin"));
	return true;
}

void ASH_CombatTrainer::RefreshOpponentPool()
{
	// 이 실행이 저장한 스냅샷 + 지정한 풀 폴더.
	TArray<FString> Candidates = RunSnapshotDirs;
	if (!OpponentPoolDir.IsEmpty())
	{
		// 풀 폴더 자체, 하위 폴더, 하위의 하위 폴더까지 본다
		// (Snapshots/<날짜>/cum..., Snapshots/<날짜> 를 직접 지정, 옛 형식 Saved/SHSnapshots/<실행>/<경과>).
		TFunction<void(const FString&, int32)> Visit = [&Visit, &Candidates](const FString& Dir, int32 Depth)
		{
			if (HasPolicyFile(Dir))
			{
				Candidates.Add(Dir);
				return;
			}
			if (Depth >= 2)
			{
				return;
			}
			TArray<FString> SubDirs;
			IFileManager::Get().FindFiles(SubDirs, *FPaths::Combine(Dir, TEXT("*")), /*Files=*/false, /*Directories=*/true);
			SubDirs.Sort();
			for (const FString& Sub : SubDirs)
			{
				Visit(FPaths::Combine(Dir, Sub), Depth + 1);
			}
		};
		Visit(OpponentPoolDir, 0);
	}

	for (const FString& Dir : Candidates)
	{
		if (!OpponentPool.ContainsByPredicate([&Dir](const FSHOpponentEntry& Entry) { return Entry.Directory == Dir; }))
		{
			FSHOpponentEntry& Entry = OpponentPool.AddDefaulted_GetRef();
			Entry.Directory = Dir;
		}
	}
}

void ASH_CombatTrainer::SwapOpponent()
{
	if (!OpponentPolicy || !Policy)
	{
		return;
	}

	RefreshOpponentPool();

	if (OpponentPool.Num() == 0)
	{
		// ★ 풀이 비어 있으면(첫 스냅샷 전) 학습 정책의 지금 가중치를 복사해 얼린다.
		//   "OpponentSwapSeconds 전의 나" 와 싸우는 셈이다.
		OpponentPolicy->GetPolicyNetworkAsset()->LoadNetworkFromAsset(Policy->GetPolicyNetworkAsset());
		OpponentPolicy->GetEncoderNetworkAsset()->LoadNetworkFromAsset(Policy->GetEncoderNetworkAsset());
		OpponentPolicy->GetDecoderNetworkAsset()->LoadNetworkFromAsset(Policy->GetDecoderNetworkAsset());
		CurrentOpponent = INDEX_NONE;
		CurrentOpponentName = FString::Printf(TEXT("학습 정책 복사본(%.0f초)"), FPlatformTime::Seconds() - StartRealTime);
	}
	else
	{
		// ★ 상대 선택: OpponentLatestProbability 로 가장 최근 스냅샷, 나머지는 PFSP.
		//   PFSP(AlphaStar) 가중치 (1 − p)², p = 그 상대에 대한 학습 정책 승률(사전 1승 1패).
		//   지는 상대를 자주 만나게 해서 약점을 메우고, 이미 이기는 상대에 시간을 덜 쓴다.
		int32 Pick = OpponentPool.Num() - 1;
		if (MixRandom.FRand() >= OpponentLatestProbability)
		{
			TArray<double> Weights;
			double Total = 0.0;
			for (const FSHOpponentEntry& Entry : OpponentPool)
			{
				const double P = (Entry.LearnerWins + 1.0) / (Entry.Games + 2.0);
				const double W = FMath::Square(1.0 - P) + 1e-3;
				Weights.Add(W);
				Total += W;
			}
			double Roll = MixRandom.FRand() * Total;
			for (int32 Index = 0; Index < Weights.Num(); ++Index)
			{
				Roll -= Weights[Index];
				if (Roll <= 0.0)
				{
					Pick = Index;
					break;
				}
			}
		}

		if (!LoadOpponentSnapshot(OpponentPool[Pick].Directory))
		{
			return;
		}
		CurrentOpponent = Pick;
		const FString Leaf = FPaths::GetCleanFilename(OpponentPool[Pick].Directory);
		const FString Parent = FPaths::GetCleanFilename(FPaths::GetPath(OpponentPool[Pick].Directory));
		CurrentOpponentName = Parent + TEXT("/") + Leaf;
	}

	// 상대 메모리를 지운다 — 다른 네트워크의 은닉 상태를 이어 쓰면 안 된다.
	TArray<int32> Ids;
	for (const TPair<TWeakObjectPtr<ASH_Arena>, TArray<int32>>& Pair : OpponentArenaAgentIds)
	{
		Ids.Append(Pair.Value);
	}
	if (Ids.Num() > 0 && OpponentManager)
	{
		OpponentManager->ResetAgents(Ids);
	}

	UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] 상대 교체 → '%s' (풀 %d개)"), *CurrentOpponentName, OpponentPool.Num());
}

void ASH_CombatTrainer::CollectRounds()
{
	for (const TWeakObjectPtr<ASH_Arena>& Weak : Arenas)
	{
		ASH_Arena* Arena = Weak.Get();
		if (!Arena)
		{
			continue;
		}
		const int32 Before = PendingRounds.Num();
		Arena->ConsumeRoundRecords(PendingRounds);

		const ESHArenaRole ArenaRole = GetArenaRole(Arena);
		for (int32 Index = Before; Index < PendingRounds.Num(); ++Index)
		{
			const FSHRoundRecord& Round = PendingRounds[Index];
			const bool bLearnerWin = (Round.End == ESHRoundEnd::Team0Wins);

			++TotalRounds;
			switch (Round.End)
			{
			case ESHRoundEnd::Team0Wins:  ++TotalWins[0]; break;
			case ESHRoundEnd::Team1Wins:  ++TotalWins[1]; break;
			case ESHRoundEnd::Timeout:    ++TotalTimeouts; break;
			case ESHRoundEnd::MutualWipe: ++TotalMutual; break;
			default: break;
			}

			++RoleRounds[static_cast<int32>(ArenaRole)];
			RoleLearnerWins[static_cast<int32>(ArenaRole)] += bLearnerWin ? 1 : 0;
			RoleRoundDuration[static_cast<int32>(ArenaRole)] += Round.Duration;

			// PFSP 전적: 지금 상대가 풀 항목일 때만 (복사본은 기록하지 않는다).
			if (ArenaRole == ESHArenaRole::Snapshot && OpponentPool.IsValidIndex(CurrentOpponent))
			{
				++OpponentPool[CurrentOpponent].Games;
				OpponentPool[CurrentOpponent].LearnerWins += bLearnerWin ? 1 : 0;
			}
		}
	}
}

void ASH_CombatTrainer::TickOpponents(const double RealNow)
{
	CollectRounds();

	// --- 새 라운드가 시작된 아레나 ------------------------------------------------
	TArray<int32> ResetIds;
	for (TPair<TWeakObjectPtr<ASH_Arena>, int32>& Pair : LastRoundIndex)
	{
		ASH_Arena* Arena = Pair.Key.Get();
		if (!Arena || Arena->GetRoundIndex() == Pair.Value)
		{
			continue;
		}
		Pair.Value = Arena->GetRoundIndex();

		const ESHArenaRole ArenaRole = GetArenaRole(Arena);
		if (ArenaRole == ESHArenaRole::Snapshot)
		{
			// 상대 정책의 메모리(GRU)를 지운다. 학습 정책 쪽은 PPO 트레이너/평가 리셋이 지운다.
			if (const TArray<int32>* Ids = OpponentArenaAgentIds.Find(Arena))
			{
				ResetIds.Append(*Ids);
			}
		}
		else if (ArenaRole == ESHArenaRole::Bot && bOpponentMix)
		{
			// 봇 반응 지연을 라운드마다 다시 뽑는다 — 고정 0.25초를 공략하는 전략을 막는다.
			for (const TWeakObjectPtr<ABase_Character>& Weak : Arena->GetTeamUnits(1))
			{
				const ABase_Character* Bot = Weak.Get();
				if (ASH_ScriptBotController* Controller = Bot ? Cast<ASH_ScriptBotController>(Bot->GetController()) : nullptr)
				{
					Controller->ReactionDelay = MixRandom.FRandRange(BotReactionDelayRange.X, BotReactionDelayRange.Y);
				}
			}
		}
	}

	if (!bHasOpponents || !OpponentPolicy)
	{
		return;
	}

	if (ResetIds.Num() > 0)
	{
		OpponentManager->ResetAgents(ResetIds);
	}

	// --- 상대 교체 (학습 중에만) ------------------------------------------------
	if (!bEvalMode && (RealNow - LastOpponentSwapRealTime) >= OpponentSwapSeconds)
	{
		SwapOpponent();
		LastOpponentSwapRealTime = RealNow;
	}

	OpponentPolicy->RunInference(1.0f);

	// 상대 쪽 행동 통계는 쓰지 않는다 — 비워서 쌓이지 않게 한다.
	USH_ShooterInteractor::FActionStats Discard;
	OpponentInteractor->ConsumeActionStats(Discard);
}

void ASH_CombatTrainer::TickEvaluation()
{
	if (!Policy || !TrainingEnvironment || !Manager)
	{
		return;
	}

	// 첫 결정: 학습의 bResetAgentsOnBegin 과 같이 모든 아레나를 새 라운드로 시작한다.
	if (!bEvalStarted)
	{
		TArray<int32> AllIds;
		for (const TPair<TWeakObjectPtr<ASH_Arena>, TArray<int32>>& Pair : ArenaAgentIds)
		{
			AllIds.Append(Pair.Value);
		}
		Manager->ResetAgents(AllIds);
		bEvalStarted = true;
	}

	// ★ 보상을 계산해 버린다. 학습은 하지 않지만 GatherAgentReward 가 유닛의 사격·피해 누산기를
	//   비우는 유일한 곳이다. 안 부르면 누산기가 쌓여 Tick 의 사격 수 집계가 매 결정 중복된다.
	//   준/받은 피해 지표도 여기서 모인다.
	TrainingEnvironment->GatherRewards();

	// 라운드가 끝난 아레나만 리셋한다. 학습에서는 PPO 트레이너가 완료(Termination)를 보고 하는 일이다.
	// Manager->ResetAgents 는 정책 메모리를 지우고 환경의 ResetAgentEpisode → Gym->Reset 을 부른다.
	for (const TPair<TWeakObjectPtr<ASH_Arena>, TArray<int32>>& Pair : ArenaAgentIds)
	{
		const ASH_Arena* Arena = Pair.Key.Get();
		if (Arena && Arena->IsRoundOver())
		{
			Manager->ResetAgents(Pair.Value);
		}
	}

	Policy->RunInference(EvalActionNoise);
}

void ASH_CombatTrainer::LogEvaluationSummary() const
{
	// RL 팀 = 팀 0 (self-play 면 두 팀 모두 같은 정책이라 50% 근처가 정상이다).
	const int32 N = TotalRounds;
	const double P = N > 0 ? static_cast<double>(TotalWins[0]) / N : 0.0;

	// 95% 윌슨 구간 — 표본이 작거나 p 가 0/1 에 가까워도 범위를 벗어나지 않는다.
	double Low = 0.0;
	double High = 0.0;
	if (N > 0)
	{
		const double Z = 1.96;
		const double Z2 = Z * Z;
		const double Denominator = 1.0 + Z2 / N;
		const double Center = (P + Z2 / (2.0 * N)) / Denominator;
		const double Half = Z * FMath::Sqrt(P * (1.0 - P) / N + Z2 / (4.0 * N * N)) / Denominator;
		Low = FMath::Max(0.0, Center - Half);
		High = FMath::Min(1.0, Center + Half);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] ===== 평가 결과 — 스냅샷 '%s' ====="), *EvalSnapshotDir);
	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 라운드 %d | 팀0(RL) 승 %d / 팀1 승 %d / 시간초과 %d / 동시전멸 %d | ")
		TEXT("팀0 승률 %.1f%% (95%% 구간 %.1f ~ %.1f%%) | 행동 잡음 x%.2f | 상대 %s"),
		N, TotalWins[0], TotalWins[1], TotalTimeouts, TotalMutual,
		100.0 * P, 100.0 * Low, 100.0 * High, EvalActionNoise,
		bHasOpponents ? *FString::Printf(TEXT("스냅샷 '%s'"), *CurrentOpponentName)
			: (bSelfPlay ? TEXT("같은 정책") : TEXT("스크립트 봇")));
}

double ASH_CombatTrainer::GetCumulativeSeconds() const
{
	return CumulativeBaseSeconds + (FPlatformTime::Seconds() - StartRealTime);
}

void ASH_CombatTrainer::AppendDailyLog(const FString& Line)
{
	const FString Date = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
	const FString DailyLogPath = FPaths::Combine(SnapshotRootDir, Date + TEXT(".md"));
	IFileManager::Get().MakeDirectory(*SnapshotRootDir, /*Tree=*/true);

	FString Text;
	if (!FPaths::FileExists(DailyLogPath))
	{
		// ★ 수정사항은 코드가 알 수 없다 — 사람이(또는 작업한 사람이) 직접 적는 칸을 만들어 둔다.
		//   실행 기록은 파일 끝에 덧붙이므로 반드시 마지막 절이어야 한다.
		Text += FString::Printf(
			TEXT("# %s 학습 기록\n\n")
			TEXT("## 수정사항\n\n")
			TEXT("(이 날 학습 코드·설정·환경에서 바뀐 점을 직접 적는다. 트레이너는 이 절을 건드리지 않는다.)\n\n")
			TEXT("## 실행 기록\n\n")
			TEXT("(아래는 트레이너가 자동으로 덧붙인다. 스냅샷은 %s/ 아래에 있다.)\n"),
			*Date, *Date);
	}
	if (LastLogDate != Date)
	{
		// 이 실행이 이 날짜 파일에 처음 쓴다(실행 시작, 또는 자정을 넘김).
		FString Header = RunLogHeader;
		if (!LastLogDate.IsEmpty())
		{
			// 머리말 끝은 "- 스냅샷:" 이고 그 아래에 스냅샷 줄이 이어진다 — 표시는 그 앞에 넣는다.
			Header.ReplaceInline(TEXT("- 스냅샷:\n"),
				*FString::Printf(TEXT("- (%s 에서 이어지는 실행)\n- 스냅샷:\n"), *LastLogDate));
		}
		Text += Header;
		LastLogDate = Date;
	}
	if (!Line.IsEmpty())
	{
		Text += Line + TEXT("\n");
	}

	FFileHelper::SaveStringToFile(Text, *DailyLogPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
}

void ASH_CombatTrainer::SaveVersionedSnapshot(bool bFinal)
{
	if (!Policy)
	{
		return;
	}

	const double Elapsed = FPlatformTime::Seconds() - StartRealTime;
	const double Cumulative = CumulativeBaseSeconds + Elapsed;
	const FString Date = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));

	// 폴더 이름 = 파일 이름 앞부분 = 누적 시간. 같은 이름이 이미 있으면(같은 스냅샷에서 두 번 이어 학습)
	// 뒤에 _2, _3 을 붙인다 — 덮어쓰면 다른 실행의 결과가 사라진다.
	const FString BaseName = TEXT("cum") + FormatHoursMinutes(Cumulative);
	FString Name = BaseName;
	FString Directory = FPaths::Combine(SnapshotRootDir, Date, Name);
	for (int32 Suffix = 2; IFileManager::Get().DirectoryExists(*Directory); ++Suffix)
	{
		Name = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
		Directory = FPaths::Combine(SnapshotRootDir, Date, Name);
	}
	IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);

	auto Save = [&Directory, &Name](ULearningAgentsNeuralNetwork* Network, const TCHAR* FileName)
	{
		if (Network)
		{
			FFilePath Path;
			Path.FilePath = FPaths::Combine(Directory, Name + TEXT("_") + FileName);
			Network->SaveNetworkToSnapshot(Path);
		}
	};

	Save(Policy->GetPolicyNetworkAsset(),  TEXT("policy.bin"));
	Save(Policy->GetEncoderNetworkAsset(), TEXT("encoder.bin"));
	Save(Policy->GetDecoderNetworkAsset(), TEXT("decoder.bin"));
	if (Critic)
	{
		Save(Critic->GetCriticNetworkAsset(), TEXT("critic.bin"));
	}

	// 무엇으로 만든 스냅샷인지 남긴다. 평가·상대 풀에서 버전을 고를 때 쓴다.
	// total_reward_steps 는 이어서 학습한 실행까지 합친 누적값이다(-SHResume 이 복원한다).
	// cumulative_seconds 는 다음 -SHResume 이 누적 시간을 이어 세는 데 쓴다.
	const FString Meta = FString::Printf(
		TEXT("label=%s\nrun=%s\nsaved_at=%s\ncumulative_seconds=%.0f\nelapsed_seconds=%.0f\ndecisions=%d\nagents=%d\nselfplay=%d\noutcome_reward=%.3f\nteam_spirit=%.3f\nobs_size=%d\ntotal_reward_steps=%lld\nresumed_from=%s\n"),
		bFinal ? TEXT("final") : TEXT("interval"), *SnapshotRunStamp,
		*FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")),
		Cumulative, Elapsed, DecisionCount, Agents.Num(), bSelfPlay ? 1 : 0,
		TrainingEnvironment ? TrainingEnvironment->OutcomeReward : -1.0f,
		TrainingEnvironment ? TrainingEnvironment->TeamSpirit : -1.0f,
		Interactor ? Interactor->GetObservationVectorSize() : -1,
		TrainingEnvironment ? TrainingEnvironment->GetRewardStepCount() : -1LL,
		ResumeDir.IsEmpty() ? TEXT("-") : *ResumeDir);
	FFileHelper::SaveStringToFile(Meta, *FPaths::Combine(Directory, Name + TEXT("_meta.txt")));
	RunSnapshotDirs.Add(Directory);

	AppendDailyLog(FString::Printf(
		TEXT("  - %s — 누적 **%s** → `%s/%s/` (이번 실행 %s, 결정 %d, 보상 스텝 누적 %lld)%s"),
		*FDateTime::Now().ToString(TEXT("%H:%M")), *FormatHoursMinutes(Cumulative), *Date, *Name,
		*FormatHoursMinutes(Elapsed), DecisionCount,
		TrainingEnvironment ? TrainingEnvironment->GetRewardStepCount() : -1LL,
		bFinal ? TEXT(" — 실행 종료") : TEXT("")));

	UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] 스냅샷 저장 — %s (누적 %s)"), *Directory, *FormatHoursMinutes(Cumulative));
}

bool ASH_CombatTrainer::LoadResumeSnapshot(const FString& Directory)
{
	if (!LoadPolicySnapshot(Directory))
	{
		return false;
	}

	// 크리틱이 없으면 가치 예측이 무작위에서 시작해 어드밴티지가 엉망이 된다 — 정책이 초반에 망가질 수 있다.
	const FString CriticPath = SnapshotFile(Directory, TEXT("critic.bin"));
	if (Critic && FPaths::FileExists(CriticPath))
	{
		FFilePath Path;
		Path.FilePath = CriticPath;
		Critic->GetCriticNetworkAsset()->LoadNetworkFromSnapshot(Path);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SH_CombatTrainer] critic.bin 이 없다 — 크리틱은 무작위에서 시작한다: %s"), *CriticPath);
	}

	// 보상 수집 누적 횟수 복원. meta.txt 의 total_reward_steps, 없으면 decisions x agents.
	int64 Steps = 0;
	TMap<FString, FString> Meta;
	if (ReadSnapshotMeta(Directory, Meta))
	{
		const FString* Total = Meta.Find(TEXT("total_reward_steps"));
		const FString* Decisions = Meta.Find(TEXT("decisions"));
		const FString* AgentCount = Meta.Find(TEXT("agents"));
		Steps = Total ? FCString::Atoi64(**Total)
			: (Decisions && AgentCount ? FCString::Atoi64(**Decisions) * FCString::Atoi64(**AgentCount) : 0);
	}
	if (TrainingEnvironment)
	{
		TrainingEnvironment->SetRewardStepCount(Steps);
	}

	// 누적 학습 시간 — 스냅샷 이름(cum<누적>)과 날짜별 기록에 쓴다.
	CumulativeBaseSeconds = ReadCumulativeSeconds(Directory);

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] 이어서 학습 준비 — 정책·인코더·디코더%s 불러옴, 누적 학습 %s, 보상 누적 %lld 회 복원 (셰이핑 x%.2f, 라운드 보상 x%.2f). 옵티마이저 상태는 새로 시작."),
		FPaths::FileExists(CriticPath) ? TEXT("·크리틱") : TEXT(""), *FormatHoursMinutes(CumulativeBaseSeconds), Steps,
		TrainingEnvironment ? TrainingEnvironment->GetShapingScale() : 0.0f,
		TrainingEnvironment ? TrainingEnvironment->GetRoundRewardScale() : 0.0f);
	return true;
}

bool ASH_CombatTrainer::LoadPolicySnapshot(const FString& Directory)
{
	if (!Policy)
	{
		return false;
	}

	const TCHAR* Files[] = { TEXT("policy.bin"), TEXT("encoder.bin"), TEXT("decoder.bin") };
	for (const TCHAR* File : Files)
	{
		if (!FPaths::FileExists(SnapshotFile(Directory, File)))
		{
			UE_LOG(LogTemp, Error, TEXT("[SH_CombatTrainer] 스냅샷 파일이 없다: %s"), *SnapshotFile(Directory, File));
			return false;
		}
	}

	// ★ 스키마가 다르면(예: Phase 2 이전 스냅샷) 엔진이 "Invalid Format" 오류를 낸다.
	//   네트워크 입력 크기가 관측 인코딩 크기와 같아야 하기 때문이다.
	auto Load = [&Directory](ULearningAgentsNeuralNetwork* Network, const TCHAR* FileName)
	{
		FFilePath Path;
		Path.FilePath = SnapshotFile(Directory, FileName);
		Network->LoadNetworkFromSnapshot(Path);
	};
	Load(Policy->GetPolicyNetworkAsset(),  TEXT("policy.bin"));
	Load(Policy->GetEncoderNetworkAsset(), TEXT("encoder.bin"));
	Load(Policy->GetDecoderNetworkAsset(), TEXT("decoder.bin"));

	UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] 스냅샷을 불러왔다 — %s"), *Directory);
	return true;
}

void ASH_CombatTrainer::ScanForNonFinite()
{
	if (!Policy || !Interactor || NonFiniteReports >= 40)
	{
		return;
	}

	if (AgentIds.Num() == 0 && Manager)
	{
		TArray<UObject*> Objects;
		Manager->GetAllAgents(Objects, AgentIds, ASH_RLCharacter::StaticClass());
	}

	const TLearningArrayView<2, const float> Encoded = Policy->GetObservationVectorsEncoded();
	const TLearningArrayView<2, const float> Memory = Policy->GetMemoryState();
	const TLearningArrayView<2, const float> Observed = Interactor->GetObservationVectorsArrayView();

	auto Check = [this](const TCHAR* What, const int32 AgentId, const TLearningArrayView<1, const float> Row) -> bool
	{
		for (int32 Index = 0; Index < Row.Num(); ++Index)
		{
			const float Value = Row[Index];
			if (!FMath::IsFinite(Value) || FMath::Abs(Value) > 1.0e6f)
			{
				const ASH_RLCharacter* Unit = Cast<ASH_RLCharacter>(Manager->GetAgent(AgentId));
				const bool bAlive = Unit && Unit->bEntityEnabled && Unit->CombatStats && Unit->CombatStats->IsAlive();
				const ASH_Arena* Arena = Unit ? Cast<ASH_Arena>(Unit->CachedGym.Get()) : nullptr;
				UE_LOG(LogTemp, Error,
					TEXT("[SH_CombatTrainer] 비정상 값 — %s[%d] = %g | 에이전트 %d (%s, 팀 %d, %s, 라운드 %.1f초%s) | 결정 %d"),
					What, Index, Value, AgentId,
					Unit ? *Unit->GetName() : TEXT("?"),
					(Unit && Unit->CombatStats) ? static_cast<int32>(Unit->CombatStats->TeamId) : -1,
					bAlive ? TEXT("생존") : TEXT("사망"),
					Arena ? Arena->GetRoundElapsed() : -1.0f,
					(Arena && Arena->IsRoundOver()) ? TEXT(", 종료됨") : TEXT(""),
					DecisionCount);
				++NonFiniteReports;
				return true;
			}
		}
		return false;
	};

	for (const int32 AgentId : AgentIds)
	{
		if (NonFiniteReports >= 40)
		{
			break;
		}
		if (AgentId < Observed.Num<0>()) { Check(TEXT("관측"), AgentId, Observed[AgentId]); }
		if (AgentId < Encoded.Num<0>())  { Check(TEXT("인코딩"), AgentId, Encoded[AgentId]); }
		if (AgentId < Memory.Num<0>())   { Check(TEXT("메모리"), AgentId, Memory[AgentId]); }
	}
}

void ASH_CombatTrainer::Report(double RealElapsed)
{
	if (RealElapsed <= 0.0)
	{
		return;
	}

	const int32 Decisions = DecisionCount - DecisionsAtLastReport;
	const double DecisionsPerSecond = static_cast<double>(Decisions) / RealElapsed;
	const double StepsPerSecond = DecisionsPerSecond * Agents.Num();

	const double AliveRate   = (MetricSampleCount > 0) ? (AliveAccumulator   / MetricSampleCount) : 0.0;
	const double VisibleRate = (MetricSampleCount > 0) ? (VisibleAccumulator / MetricSampleCount) : 0.0;

	const int64 Fired = ShotsFiredTotal - ShotsFiredAtLastReport;
	const int64 Hit   = ShotsHitTotal   - ShotsHitAtLastReport;
	const double HitRate = (Fired > 0) ? (100.0 * static_cast<double>(Hit) / static_cast<double>(Fired)) : 0.0;

	// --- 라운드 지표 (대치 감지) ----------------------------------------------
	// ★ 시간 초과 비율, 첫 피해까지 시간, 최근접 적 거리가 같이 오르면 대치다.
	// 라운드 기록은 CollectRounds 가 매 결정 모아 둔다(상대별 전적을 정확히 나누기 위해).
	CollectRounds();
	TArray<FSHRoundRecord> Rounds = MoveTemp(PendingRounds);
	PendingRounds.Reset();

	double ZoneDamageOnAgents = 0.0;
	for (const TWeakObjectPtr<ASH_Arena>& Weak : Arenas)
	{
		if (ASH_Arena* Arena = Weak.Get())
		{
			for (int32 Team = 0; Team < 2; ++Team)
			{
				const float Zone = Arena->ConsumeZoneDamage(Team);
				// 학습 정책이 조종하는 팀만 — 스냅샷/봇 아레나의 팀 1 은 받은 피해 분모에 없다.
				if (IsLearnerTeam(Arena, Team))
				{
					ZoneDamageOnAgents += Zone;
					RoleZoneDamage[static_cast<int32>(GetArenaRole(Arena))] += Zone;
				}
			}
		}
	}
	int32 RoundWins[2] = { 0, 0 };
	int32 RoundTimeouts = 0;
	int32 RoundMutual = 0;
	double RoundDurationSum = 0.0;
	double FirstDamageSum = 0.0;
	int32 FirstDamageCount = 0;
	for (const FSHRoundRecord& Round : Rounds)
	{
		switch (Round.End)
		{
		case ESHRoundEnd::Team0Wins:  ++RoundWins[0]; break;
		case ESHRoundEnd::Team1Wins:  ++RoundWins[1]; break;
		case ESHRoundEnd::Timeout:    ++RoundTimeouts; break;
		case ESHRoundEnd::MutualWipe: ++RoundMutual; break;
		default: break;
		}
		RoundDurationSum += Round.Duration;
		if (Round.FirstDamageTime >= 0.0f)
		{
			FirstDamageSum += Round.FirstDamageTime;
			++FirstDamageCount;
		}
	}
	const double MeanRoundDuration = Rounds.Num() > 0 ? RoundDurationSum / Rounds.Num() : 0.0;
	const double MeanFirstDamage = FirstDamageCount > 0 ? FirstDamageSum / FirstDamageCount : 0.0;
	const double TimeoutRate = Rounds.Num() > 0 ? 100.0 * RoundTimeouts / Rounds.Num() : 0.0;
	const double MeanNearestEnemy = NearestEnemySamples > 0 ? NearestEnemyDistanceSum / NearestEnemySamples : 0.0;
	NearestEnemyDistanceSum = 0.0;
	NearestEnemySamples = 0;

	// --- 진단 지표 3개 --------------------------------------------------------
	// ★ 명중률만으로는 **이동과 발사 타이밍, 엄폐가 학습됐는지 알 수 없다.**
	//   세 지표가 각각 그 셋을 담당한다. 상세한 근거는 각 Consume 함수의 주석에 있다.

	// ① 받은 피해 / 결정 1,000회 — 엄폐 효과를 재는 유일한 직접 지표
	double DamageTaken = 0.0;
	double DamageDealt = 0.0;
	int64 Gathers = 0;
	if (TrainingEnvironment)
	{
		TrainingEnvironment->ConsumeDamageStats(DamageTaken, DamageDealt, Gathers);
	}
	const double TakenPerK = (Gathers > 0) ? (1000.0 * DamageTaken / static_cast<double>(Gathers)) : 0.0;
	// 받은 피해 중 구역이 준 몫. 높으면 정책이 구역을 피하지 못하는 것이다.
	const double ZoneShare = (DamageTaken > 0.0) ? (100.0 * ZoneDamageOnAgents / DamageTaken) : 0.0;
	const double DealtPerK = (Gathers > 0) ? (1000.0 * DamageDealt / static_cast<double>(Gathers)) : 0.0;

	// ② 이동 행동 분포 — 각 20% 에 가까우면 정책은 이동을 무작위로 뽑고 있다
	// ③ 발사 기회 사용률 — 쿨다운이 끝난 결정 중 실제로 쏜 비율
	USH_ShooterInteractor::FActionStats ActionStats;
	if (Interactor)
	{
		Interactor->ConsumeActionStats(ActionStats);
	}

	double MovePercent[SHRL::MoveBins] = {};
	if (ActionStats.Decisions > 0)
	{
		for (int32 Bin = 0; Bin < SHRL::MoveBins; ++Bin)
		{
			MovePercent[Bin] = 100.0 * static_cast<double>(ActionStats.MoveCounts[Bin])
				/ static_cast<double>(ActionStats.Decisions);
		}
	}

	const double FireUseRate = (ActionStats.FireOpportunities > 0)
		? (100.0 * static_cast<double>(ActionStats.FiresUsed)
			/ static_cast<double>(ActionStats.FireOpportunities))
		: 0.0;

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer] %5.0f초 | 결정 %d (%.1f/s, %.0f step/s) | ")
		TEXT("생존 %.1f%% | 시야에 적 %.1f%% | 사격 %lld 명중 %lld (%.1f%%) | 셰이핑 x%.2f | 라운드 보상 x%.2f"),
		FPlatformTime::Seconds() - StartRealTime,
		DecisionCount, DecisionsPerSecond, StepsPerSecond,
		100.0 * AliveRate, 100.0 * VisibleRate,
		Fired, Hit, HitRate,
		TrainingEnvironment ? TrainingEnvironment->GetShapingScale() : 0.0f,
		TrainingEnvironment ? TrainingEnvironment->GetRoundRewardScale() : 0.0f);

	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer]        라운드 %d (팀0 승 %d / 팀1 승 %d / 시간초과 %d (%.0f%%) / 동시전멸 %d) | ")
		TEXT("평균 길이 %.1f초 | 첫 피해 %.1f초 | 최근접 적 %.0f cm"),
		Rounds.Num(), RoundWins[0], RoundWins[1], RoundTimeouts, TimeoutRate, RoundMutual,
		MeanRoundDuration, MeanFirstDamage, MeanNearestEnemy);

	// 역할별 전적 (학습 정책 = 팀 0 의 승). 혼합이나 평가 상대가 있을 때만.
	if (bOpponentMix || bHasOpponents)
	{
		auto Rate = [this](const int32 ArenaRole) -> double
		{
			return RoleRounds[ArenaRole] > 0 ? 100.0 * RoleLearnerWins[ArenaRole] / RoleRounds[ArenaRole] : 0.0;
		};
		UE_LOG(LogTemp, Display,
			TEXT("[SH_CombatTrainer]        상대별 — self-play %d라운드 | 스냅샷 '%s' %d라운드 승률 %.0f%% | 봇 %d라운드 승률 %.0f%%"),
			RoleRounds[0], *CurrentOpponentName, RoleRounds[1], Rate(1), RoleRounds[2], Rate(2));
		// 역할별 전투 지표. 받은/준 피해는 "살아 있는 결정 1,000회당" — 전체 지표와 같은 단위.
		static const TCHAR* RoleNames[3] = { TEXT("self-play"), TEXT("스냅샷"), TEXT("봇") };
		FString Line;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (RoleAliveDecisions[Index] == 0 && RoleRounds[Index] == 0)
			{
				continue;
			}
			const double PerK = RoleAliveDecisions[Index] > 0 ? 1000.0 / RoleAliveDecisions[Index] : 0.0;
			Line += FString::Printf(TEXT("%s%s: 명중 %.1f%% 받음 %.0f(구역 %.0f%%) 줌 %.0f 길이 %.1f초"),
				Line.IsEmpty() ? TEXT("") : TEXT(" | "),
				RoleNames[Index],
				RoleShots[Index] > 0 ? 100.0 * RoleHits[Index] / RoleShots[Index] : 0.0,
				RoleDamageTaken[Index] * PerK,
				RoleDamageTaken[Index] > 0.0 ? 100.0 * RoleZoneDamage[Index] / RoleDamageTaken[Index] : 0.0,
				RoleDamageDealt[Index] * PerK,
				RoleRounds[Index] > 0 ? RoleRoundDuration[Index] / RoleRounds[Index] : 0.0);
		}
		UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer]        역할별 — %s"), *Line);

		for (int32 ArenaRole = 0; ArenaRole < 3; ++ArenaRole)
		{
			RoleRounds[ArenaRole] = 0;
			RoleLearnerWins[ArenaRole] = 0;
			RoleShots[ArenaRole] = 0;
			RoleHits[ArenaRole] = 0;
			RoleDamageTaken[ArenaRole] = 0.0;
			RoleDamageDealt[ArenaRole] = 0.0;
			RoleZoneDamage[ArenaRole] = 0.0;
			RoleRoundDuration[ArenaRole] = 0.0;
			RoleAliveDecisions[ArenaRole] = 0;
		}
	}

	// 발사 가능한 결정이 전체의 몇 퍼센트인지. 고정값이 아니다 —
	// ★ 정책이 기회를 건너뛰면 쿨다운이 안 걸려서 이 비율이 **올라간다.**
	//   즉 사용률과 이 값은 반대로 움직여야 정상이다. 같은 방향으로 가면 카운터가 틀린 것이다.
	const double FireOpportunityShare = (ActionStats.Decisions > 0)
		? (100.0 * static_cast<double>(ActionStats.FireOpportunities)
			/ static_cast<double>(ActionStats.Decisions))
		: 0.0;

	// ★ 한 줄에 다 넣으면 로그가 읽히지 않는다. 진단 지표는 둘째 줄로 분리한다.
	UE_LOG(LogTemp, Display,
		TEXT("[SH_CombatTrainer]        피해 받음 %.1f (구역 %.0f%%) / 줌 %.1f (결정 1k당, 생존결정 %lld) | ")
		TEXT("발사기회 사용 %.1f%% (%lld/%lld, 기회비율 %.1f%%) | ")
		TEXT("이동 정지 %.0f%% 전진 %.0f%% 후진 %.0f%% 좌 %.0f%% 우 %.0f%% (균등=20%%)"),
		TakenPerK, ZoneShare, DealtPerK, ActionStats.Decisions,
		FireUseRate, ActionStats.FiresUsed, ActionStats.FireOpportunities, FireOpportunityShare,
		MovePercent[0], MovePercent[1], MovePercent[2], MovePercent[3], MovePercent[4]);

	// 구간 기준선을 갱신한다. 누산기를 비우는 것이 아니라 차분의 기준을 옮긴다.
	DecisionsAtLastReport = DecisionCount;
	ShotsFiredAtLastReport = ShotsFiredTotal;
	ShotsHitAtLastReport = ShotsHitTotal;
	AliveAccumulator = 0.0;
	VisibleAccumulator = 0.0;
	MetricSampleCount = 0;
}

void ASH_CombatTrainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ★ 반드시 EndTraining() 을 부른다. 안 부르면 python.exe 가 공유메모리를 잡은 채
	//   고아 프로세스로 남고, 다음 실행이 같은 공유메모리 이름으로 붙지 못한다.
	if (Trainer && Trainer->IsTraining())
	{
		UE_LOG(LogTemp, Display, TEXT("[SH_CombatTrainer] EndPlay — EndTraining() 호출."));
		Trainer->EndTraining();
	}

	if (bEvalMode && bAssembled && !bEvalRealtime)
	{
		UE::Learning::Agents::RevertGameSettings(EvalGameSettingsState, GetWorld());
	}

	bAssembled = false;

	Super::EndPlay(EndPlayReason);
}
