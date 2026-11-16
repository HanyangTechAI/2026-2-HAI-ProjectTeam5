// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 *  고를 수 있는 학습된 정책(스냅샷) 목록. UObject 가 아니다.
 *
 *  훑는 곳(프로젝트 폴더 기준):
 *    Training/<학습 이름>/cum017h06m/   ← Training/train.py 자동 학습
 *    Snapshots/<날짜>/cum016h00m/       ← 예전 손 실행 학습
 *  policy · encoder · decoder 세 파일이 모두 있는 폴더만 넣는다.
 *
 *  ★ 디스크를 직접 훑으므로 에디터 · 개발 빌드용이다. 패키징하면 이 폴더들이 없다 — 그때는 NN 에셋으로 바꿔야 한다.
 */
namespace SHPolicyCatalog
{
	struct FEntry
	{
		FString Directory;            // 프로젝트 폴더 기준 상대 경로 — 유닛 정의 · 게임모드에 그대로 넘긴다
		FString Label;                // 화면 표시: "START_11_13 · 017h06m", "2026-10-25 · 016h00m"
		FString Group;                // 학습 이름 또는 날짜
		double CumulativeSeconds = 0.0;
		int32 ObservationSize = -1;   // meta 의 obs_size. 없으면 −1(옛 스냅샷)
	};

	/** 전체 목록. Training 이 먼저(이름순), 다음 Snapshots(최근 날짜 먼저). 같은 묶음 안에서는 학습 시간이 긴 것부터. */
	[[nodiscard]] TArray<FEntry> Scan();

	/** 경로 하나의 표시 이름. 목록에 없는 경로(유닛 정의의 기본값 등)에도 쓴다. */
	[[nodiscard]] FString MakeLabel(const FString& Directory);

	/** 결과 화면용 짧은 이름: "017h06m". */
	[[nodiscard]] FString MakeShortLabel(const FString& Directory);
}
