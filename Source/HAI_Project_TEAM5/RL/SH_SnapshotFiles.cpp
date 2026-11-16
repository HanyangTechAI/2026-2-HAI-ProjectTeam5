// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_SnapshotFiles.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString SHSnapshot::ResolveFile(const FString& Directory, const TCHAR* FileName)
{
	const FString Prefixed = FPaths::Combine(Directory, FPaths::GetCleanFilename(Directory) + TEXT("_") + FileName);
	return FPaths::FileExists(Prefixed) ? Prefixed : FPaths::Combine(Directory, FileName);
}

TMap<FString, FString> SHSnapshot::ReadMeta(const FString& Directory)
{
	TMap<FString, FString> Meta;
	TArray<FString> Lines;
	if (FFileHelper::LoadFileToStringArray(Lines, *ResolveFile(Directory, TEXT("meta.txt"))))
	{
		for (const FString& Line : Lines)
		{
			FString Key, Value;
			if (Line.Split(TEXT("="), &Key, &Value))
			{
				Meta.Add(Key.TrimStartAndEnd(), Value.TrimStartAndEnd());
			}
		}
	}
	return Meta;
}

bool SHSnapshot::HasPolicyFiles(const FString& Directory)
{
	for (const TCHAR* File : { TEXT("policy.bin"), TEXT("encoder.bin"), TEXT("decoder.bin") })
	{
		if (!FPaths::FileExists(ResolveFile(Directory, File)))
		{
			return false;
		}
	}
	return true;
}
