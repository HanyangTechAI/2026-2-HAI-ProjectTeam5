// Fill out your copyright notice in the Description page of Project Settings.

#include "SH_SnapshotFiles.h"

#include "Misc/Paths.h"

FString SHSnapshot::ResolveFile(const FString& Directory, const TCHAR* FileName)
{
	const FString Prefixed = FPaths::Combine(Directory, FPaths::GetCleanFilename(Directory) + TEXT("_") + FileName);
	return FPaths::FileExists(Prefixed) ? Prefixed : FPaths::Combine(Directory, FileName);
}
