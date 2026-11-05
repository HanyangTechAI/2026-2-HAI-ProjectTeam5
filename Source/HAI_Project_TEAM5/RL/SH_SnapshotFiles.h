// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/** 학습기와 인게임 정책 실행(ASH_BattleBrain)이 함께 쓰는 스냅샷 파일 규칙. */
namespace SHSnapshot
{
	/**
	 *  스냅샷 폴더 안의 파일 경로.
	 *  새 형식은 파일 이름 앞에 폴더 이름(= 누적 시간)이 붙는다: cum005h00m/cum005h00m_policy.bin.
	 *  그 파일이 없으면 옛 형식(Saved/SHSnapshots/.../policy.bin)으로 본다.
	 */
	HAI_PROJECT_TEAM5_API FString ResolveFile(const FString& Directory, const TCHAR* FileName);
}
