// Fill out your copyright notice in the Description page of Project Settings.

#include "Battle/SH_PolicyCatalog.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "RL/SH_SnapshotFiles.h"

namespace
{
	/** "cum017h06m" / "cum017h06m_2" → 초. 형식이 아니면 −1. */
	double SecondsFromFolderName(const FString& Name)
	{
		int32 Hours = 0, Minutes = 0;
		if (Name.StartsWith(TEXT("cum")) && Name.Len() >= 10
			&& LexTryParseString(Hours, *Name.Mid(3, 3)) && LexTryParseString(Minutes, *Name.Mid(7, 2)))
		{
			return Hours * 3600.0 + Minutes * 60.0;
		}
		return -1.0;
	}

	FString FormatHoursMinutes(const double Seconds)
	{
		const int32 TotalMinutes = FMath::FloorToInt32(Seconds / 60.0);
		return FString::Printf(TEXT("%03dh%02dm"), TotalMinutes / 60, TotalMinutes % 60);
	}

	/** Root/<Group>/<Snapshot>/ 두 단계만 본다(로그 폴더 등 깊은 곳까지 훑지 않는다). */
	void ScanRoot(const FString& RootName, TArray<SHPolicyCatalog::FEntry>& Out)
	{
		IFileManager& Files = IFileManager::Get();
		const FString Root = FPaths::Combine(FPaths::ProjectDir(), RootName);
		TArray<FString> Groups;
		Files.FindFiles(Groups, *FPaths::Combine(Root, TEXT("*")), /*Files=*/false, /*Directories=*/true);
		for (const FString& Group : Groups)
		{
			TArray<FString> Snapshots;
			Files.FindFiles(Snapshots, *FPaths::Combine(Root, Group, TEXT("cum*")), false, true);
			for (const FString& Snapshot : Snapshots)
			{
				const FString Relative = FPaths::Combine(RootName, Group, Snapshot);
				const FString Absolute = FPaths::Combine(FPaths::ProjectDir(), Relative);
				if (!SHSnapshot::HasPolicyFiles(Absolute))
				{
					continue;
				}
				SHPolicyCatalog::FEntry& Entry = Out.AddDefaulted_GetRef();
				Entry.Directory = Relative;
				Entry.Group = Group;
				Entry.Label = SHPolicyCatalog::MakeLabel(Relative);

				const TMap<FString, FString> Meta = SHSnapshot::ReadMeta(Absolute);
				const FString* Cumulative = Meta.Find(TEXT("cumulative_seconds"));
				Entry.CumulativeSeconds = Cumulative ? FCString::Atod(**Cumulative) : SecondsFromFolderName(Snapshot);
				const FString* ObsSize = Meta.Find(TEXT("obs_size"));
				Entry.ObservationSize = ObsSize ? FCString::Atoi(**ObsSize) : -1;
			}
		}
	}
}

TArray<SHPolicyCatalog::FEntry> SHPolicyCatalog::Scan()
{
	TArray<FEntry> Training, Snapshots;
	ScanRoot(TEXT("Training"), Training);
	ScanRoot(TEXT("Snapshots"), Snapshots);

	Training.Sort([](const FEntry& A, const FEntry& B)
	{
		return A.Group != B.Group ? A.Group < B.Group : A.CumulativeSeconds > B.CumulativeSeconds;
	});
	Snapshots.Sort([](const FEntry& A, const FEntry& B)
	{
		return A.Group != B.Group ? A.Group > B.Group : A.CumulativeSeconds > B.CumulativeSeconds;   // 날짜는 최근 먼저
	});

	TArray<FEntry> All = MoveTemp(Training);
	All.Append(MoveTemp(Snapshots));
	return All;
}

FString SHPolicyCatalog::MakeLabel(const FString& Directory)
{
	FString Normalized = Directory;
	FPaths::NormalizeDirectoryName(Normalized);
	TArray<FString> Parts;
	Normalized.ParseIntoArray(Parts, TEXT("/"));
	if (Parts.Num() >= 2)
	{
		return FString::Printf(TEXT("%s · %s"), *Parts[Parts.Num() - 2], *MakeShortLabel(Normalized));
	}
	return Normalized;
}

FString SHPolicyCatalog::MakeShortLabel(const FString& Directory)
{
	const FString Name = FPaths::GetCleanFilename(Directory);
	const double Seconds = SecondsFromFolderName(Name);
	// "cum015h06m_2" 처럼 같은 시간에 두 번 저장된 폴더는 접미사를 붙여야 목록에서 구별된다.
	return Seconds >= 0.0 ? FormatHoursMinutes(Seconds) + Name.Mid(10) : Name;
}
