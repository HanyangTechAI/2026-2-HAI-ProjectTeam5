// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsGymSimple.h"
#include "SH_Arena.generated.h"

class UStaticMeshComponent;
class ULearningAgentsEntitiesManagerComponent;
class ABase_Character;

/**
 *  전투 에피소드 하나가 격리되어 진행되는 아레나.
 *
 *  ALearningAgentsGymSimple 로부터 시드 난수 스트림, 바닥 메시 기반 GetGymExtents,
 *  Initialize/Reset 델리게이트, ALearningAgentsGymsManager 호환성을 물려받는다.
 *
 *  형상(바닥 + 팔각형 벽 8개)을 전부 C++ 에서 만들고 OnConstruction 에서 배치하므로,
 *  에디터에서 컴포넌트를 손으로 놓을 필요가 없다. 모든 치수는 ArenaRadius 하나에서 파생된다.
 *
 *  ★ 베이스의 위치 생성 함수 두 개를 반드시 덮어쓴다.
 *
 *  ALearningAgentsGymSimple::ProjectPointToGym 은 NavigationSystem 이 존재하면
 *  ProjectPointToNavigation 의 실패 여부(bool 반환값)를 검사하지 않고 FNavLocation 을
 *  그대로 반환한다. FNavLocation 의 기본값은 FVector::ZeroVector 이므로,
 *  "NavigationSystem 은 있는데 NavMesh 가 없는" 상태에서는 모든 스폰 좌표가
 *  월드 원점 (0,0,0) 이 된다. 에러도 경고도 없다.
 *  이 프로젝트는 NavMesh 를 쓰지 않으므로 반드시 이 경로에 걸린다.
 *
 *  또 GenerateRandomLocationInGym 의 원본은 반지름을 균등 추출하므로 점이 중심에 몰린다
 *  (VRand() 가 3D 구면 벡터라 XY 투영 길이가 sqrt(1-z^2) 만큼 더 줄어드는 효과까지 겹친다).
 *  면적 균등을 원하면 반지름에 sqrt 를 씌워야 한다.
 */
UCLASS(Blueprintable, ClassGroup = (SH))
class HAI_PROJECT_TEAM5_API ASH_Arena : public ALearningAgentsGymSimple
{
	GENERATED_BODY()

public:
	ASH_Arena();

	/** 팀별 유닛을 에피소드마다 재사용해 주는 엔진 풀링 컴포넌트.
	 *  Entities 배열은 OnConstruction 이 아래 UnitClass/UnitsPerTeam 에서 채운다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SH|Arena")
	TObjectPtr<ULearningAgentsEntitiesManagerComponent> EntitiesManager;

	//~ 형상 ------------------------------------------------------------------

	/**
	 *  켜면 ArenaRadius 에서 팔각형 벽·바닥을 계산해 배치한다 (기본 동작).
	 *  끄면 C++ 벽 8개를 숨기고 콜리전을 끈다 — BP 에서 손으로 놓은 형상만 남는다.
	 *
	 *  ★ "지우기" 가 아니라 "숨기기 + 콜리전 끄기" 인 이유: 아래 Walls 는
	 *  CreateDefaultSubobject 산물이라 BP 컴포넌트 트리에 **Inherited** 로 나오고,
	 *  BP 에서는 삭제도 부모 변경도 할 수 없다.
	 *
	 *  ★ 기본값 true. BP_SHArena 를 처음 만들면 이 팔각형이 그대로 보이고,
	 *  거기서부터 수작업으로 고쳐 나간다.
	 */
	UPROPERTY(EditAnywhere, Category = "SH|Arena")
	bool bAutoBuildBoundary = true;

	/** 벽 안쪽 면까지의 반경(cm). bAutoBuildBoundary 가 켜져 있으면
	 *  바닥 크기·벽 배치·클램프 반경이 전부 이 값에서 나온다.
	 *
	 *  ★ 수작업 형상으로 넘어가면(bAutoBuildBoundary=false) 의미가 바뀐다 —
	 *  "벽까지의 거리" 가 아니라 **안전 내접 반경**이다. ProjectPointToGym 이
	 *  이 반경의 원판으로 클램프하므로 직접 그린 벽의 내접원보다 작게 유지할 것.
	 *
	 *  상한은 할인 지평이 정한다: gamma=0.995 @ 20Hz 이면 1/(1-gamma) = 200스텝 = 10초이고,
	 *  MaxWalkSpeed 500cm/s 로 아레나를 가로지르는 시간이 그보다 길면 정책이
	 *  "적에게 접근한다"의 가치를 원리적으로 볼 수 없다. 1750 이면 횡단 7.0초. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Arena", meta = (ClampMin = "200.0"))
	float ArenaRadius = 1750.0f;

	/** 유닛 캡슐이 벽을 파고들지 않도록 클램프를 안쪽으로 당기는 여유(cm). 캡슐 반경 42 + 여유. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Arena", meta = (ClampMin = "0.0"))
	float ClampMargin = 60.0f;

	/** 벽 두께(cm). 안쪽 면이 ArenaRadius 에 오도록 배치된다. */
	UPROPERTY(EditAnywhere, Category = "SH|Arena", meta = (ClampMin = "10.0"))
	float WallThickness = 50.0f;

	/** 벽 높이(cm). */
	UPROPERTY(EditAnywhere, Category = "SH|Arena", meta = (ClampMin = "50.0"))
	float WallHeight = 400.0f;

	//~ 유닛 ------------------------------------------------------------------

	/** 풀에 넣을 유닛 클래스 A. ILearningAgentsEntityInterface 를 구현해야 한다.
	 *
	 *  ★ 이 필드는 "어떤 클래스를 풀링할지"만 정한다. **팀을 정하지 않는다** —
	 *  팀은 각 BP 의 CombatStats.TeamId 가 정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Units")
	TSubclassOf<AActor> Team0UnitClass;

	/** 풀에 넣을 유닛 클래스 B.
	 *
	 *  ★ A 와 반드시 **다른 클래스**여야 한다. 팀 때문이 아니라 풀링 때문이다 —
	 *  엔진 풀이 EntityClass->GetFName() 으로 키잉되므로(...ManagerComponent.cpp:138)
	 *  같은 클래스를 두 번 넣으면 한 풀을 공유해 팀별 스폰 수를 따로 정할 수 없다.
	 *  상속 관계에는 제약이 없다 — B 가 A 를 상속해도 된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Units")
	TSubclassOf<AActor> Team1UnitClass;

	/** 팀당 스폰 수. Min/Max 에 같은 값이 들어간다.
	 *
	 *  ★ 일부러 값 하나로 묶었다. EpisodeEntitySpawnCountMin != Max 이면
	 *  ResetLearningComponent 가 에피소드마다 개수를 새로 뽑고, 모자라면 에피소드 도중에
	 *  새 액터를 스폰한다. 그렇게 생긴 폰은 Manager->AddAgent 를 거치지 않았으므로
	 *  정책이 영원히 무시한다. 살아있는 수를 줄이려면 DisableEntity 를 쓸 것.
	 *
	 *  ★ 스키마 동결 때문에 처음부터 최종 규모(10)로 두는 것을 권한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Units", meta = (ClampMin = "1"))
	int32 UnitsPerTeam = 1;

	//~ 스폰 배치 --------------------------------------------------------------

	/** 팀 스폰 원의 반경(cm). 1v1 이면 두 유닛이 2 * SpawnRadius 만큼 떨어져 마주본다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Spawn", meta = (ClampMin = "0.0"))
	float SpawnRadius = 1200.0f;

	/** 같은 팀 슬롯 사이의 각도 간격(도). 10명이면 108도 호에 늘어선다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Spawn", meta = (ClampMin = "0.0"))
	float ArcStepDegrees = 12.0f;

	/** 바닥 위로 띄우는 높이(cm). 캡슐 half-height 96 + 여유. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Spawn")
	float SpawnZOffset = 100.0f;

	/** 커리큘럼 손잡이 — 스폰 각도 흔들림(도, ±). 0단계에서는 0으로 두고,
	 *  조준이 풀린 1단계부터 20도 정도를 준다. 처음부터 흔들면 학습이 느려진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Spawn", meta = (ClampMin = "0.0"))
	float SpawnAngleJitterDegrees = 0.0f;

	/** 커리큘럼 손잡이 — 스폰 반경 흔들림(cm, ±). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SH|Spawn", meta = (ClampMin = "0.0"))
	float SpawnRadiusJitter = 0.0f;

	//~ 디버그 ----------------------------------------------------------------

	/** BeginPlay 에서 아레나 경계와 스폰 슬롯을 그린다. Gate 2 확인용. */
	UPROPERTY(EditAnywhere, Category = "SH|Debug")
	bool bDrawDebugOnBeginPlay = true;

	//~ ALearningAgentsGymSimple -----------------------------------------------

	/**
	 *  NavMesh 를 쓰지 않는 순수 수학 클램프. 원판 안으로 당기고 Z 는 아레나 평면에 고정.
	 *
	 *  ★ 이것과 아래 GenerateRandomLocationInGym 은 **안전망일 뿐이다.**
	 *  엔진에서 둘을 쓰는 곳은 엔티티 스폰의 랜덤 배치뿐이고
	 *  (LearningAgentsEntitiesManagerComponent.cpp:227, 239),
	 *  그 결과를 ApplySpawnSlots() 가 곧바로 고정 슬롯으로 덮어쓴다.
	 *  수작업으로 직사각형 아레나를 그려도 내접원은 항상 그 안쪽이므로 여전히 유효하다.
	 */
	virtual FVector ProjectPointToGym(const FVector& InLocation) const override;

	/** 원판 위 면적 균등 난수. */
	virtual FVector GenerateRandomLocationInGym() const override;

	/**
	 *  격자 간격의 근거. **수작업 형상까지 포함한** 로컬 바운딩 박스를 돌려준다.
	 *
	 *  ★ 상속받은 구현은 SimpleGymFloor 하나의 로컬 바운드 x 스케일만 본다
	 *  (LearningAgentsGymSimple.cpp:21-26). GymsManager 는 **첫 번째 Gym** 의 이 값으로
	 *  아레나 사이 피치를 정하므로(LearningAgentsGymsManager.cpp:75, 80-82),
	 *  바닥보다 넓게 벽을 그리면 아레나끼리 겹치고 AGun::Range(6000) 가 피치를 넘어
	 *  총알이 옆 아레나로 샌다. 콜리전이 있는 컴포넌트 전체를 감싸서 그걸 막는다.
	 */
	virtual void GetGymExtents(FVector& OutMinExtents, FVector& OutMaxExtents) const override;

	// NOTE: GenerateRandomRotationInGym 은 덮어쓰지 않는다.
	//       인자로 위치를 받지 않아 "중심을 바라보게" 만들 수 없다.
	//       고정 슬롯의 방향은 GetSpawnTransforms 가 직접 계산한다.

	//~ 스폰 슬롯 --------------------------------------------------------------

	/**
	 *  팀의 고정 스폰 슬롯. 학습 아레나와 출고 보드의 유일한 차이가 이 함수다 —
	 *  보드는 이것만 오버라이드해서 그리드 셀을 반환한다.
	 *
	 *  팀 0 은 180도 방향 호, 팀 1 은 0도 방향 호에 배치되고 항상 중심을 바라본다.
	 *
	 *  엔진의 SpawnEntitiesAtRandomLocations 를 쓰지 않는 이유:
	 *   1) 랜덤 트랜스폼을 한 번만 계산해 N 개 전부 같은 자리에 스폰한다
	 *      (LearningAgentsEntitiesManagerComponent.cpp:89-92) — 캡슐이 겹쳐 서로 밀어낸다.
	 *   2) 시작 거리가 매 판 달라지면 조준 학습 단계의 분산이 커진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "SH|Spawn")
	virtual void GetSpawnTransforms(int32 Team, int32 LiveCount, TArray<FTransform>& OutTransforms) const;

	/** 슬롯 하나만 필요할 때. */
	UFUNCTION(BlueprintCallable, Category = "SH|Spawn")
	FTransform GetSpawnTransformForSlot(int32 Team, int32 SlotIndex, int32 LiveCount) const;

	/** 아레나 경계와 스폰 슬롯을 그린다. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SH|Debug")
	void DrawDebugArena(float Duration = 60.0f) const;

	/**
	 *  이 아레나에 속한 유닛을 팀별로 모은다.
	 *
	 *  ★ 팀의 근거는 클래스가 아니라 유닛의 CombatStats.TeamId 다.
	 *  각 팀 BP 가 그 값을 저장하고 아레나는 읽기만 한다. 덕분에 두 유닛 BP 사이의
	 *  상속 관계에 아무 제약이 없다 — 한쪽이 다른 쪽을 상속해도 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "SH|Arena")
	void CollectUnits(int32 Team, TArray<ABase_Character*>& OutUnits) const;

protected:
	/** Gym 초기화 직후 — 팀 구성 검사 + 고정 슬롯 배치. */
	UFUNCTION()
	void HandleGymInitialized();

	/** 에피소드 리셋 직후 — 엔진이 깔아 둔 랜덤 트랜스폼을 고정 슬롯으로 덮어쓴다. */
	UFUNCTION()
	void HandlePostGymReset();

	/**
	 *  유닛을 고정 스폰 슬롯에 놓는다. 팀 id 는 건드리지 않는다 — 유닛이 들고 온다.
	 *
	 *  ★ 엔진의 SpawnEntitiesAtRandomLocations 는 랜덤 트랜스폼을 한 번만 계산해
	 *  N 개를 전부 같은 자리에 스폰한다 (...ManagerComponent.cpp:89-92). 10명이 한 점에
	 *  겹쳐서 캡슐이 서로 밀어낸다. 그래서 스폰 직후 우리가 다시 깔아 준다.
	 */
	void ApplySpawnSlots();

	/** 양 팀에 유닛이 있는지 확인한다. 팀을 데이터로 두면 "둘 다 TeamId=0" 같은
	 *  실수가 가능해지므로, 조용히 안 싸우는 대신 크게 알린다. */
	void ValidateTeamAuthoring() const;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** 바닥 스케일 · 벽 8개 배치를 ArenaRadius 에서 다시 계산한다.
	 *  bAutoBuildBoundary 가 꺼져 있으면 C++ 벽을 숨기고 아무것도 계산하지 않는다. */
	void RebuildArenaGeometry();

	/**
	 *  유닛 풀(EntitiesManager->Entities)을 UnitClass/UnitsPerTeam 에서 다시 채운다.
	 *
	 *  ★ RebuildArenaGeometry 와 분리해 둔 이유: 경계 생성을 껐을 때도 **반드시** 돌아야 한다.
	 *  한 함수에 두면 조기 반환에 같이 걸려서 유닛이 하나도 스폰되지 않고,
	 *  그런데 에러도 경고도 나지 않는다.
	 */
	void RebuildEntityPool();

	/**
	 *  스폰 슬롯이 엄폐물·벽에 막혔는지 검사해 Error 로그를 남긴다.
	 *
	 *  엄폐물을 넣으면 유닛이 블록 안에 스폰될 수 있다. 캡슐이 밀려나거나 끼는데
	 *  화면으로는 잘 보이지 않고 학습만 조용히 망가진다.
	 *
	 *  ★ 한계: 스폰 지터(SpawnAngleJitterDegrees / SpawnRadiusJitter)는 검사하지 않는다.
	 *  BeginPlay 시점에는 GymsManager 가 아직 RandomStream 을 주입하지 않았고
	 *  (LearningAgentsGymsManager.cpp:86-89 순서), 지터 범위를 전부 훑는 것은 과하다.
	 *  지터를 쓰면 DrawDebugArena 로 눈으로 확인할 것.
	 */
	void ValidateSpawnSlotsClear() const;

	/** 팔각형 벽. 개수는 고정(생성자에서 만들어야 하므로). */
	static constexpr int32 NumWalls = 8;

	UPROPERTY(VisibleAnywhere, Category = "SH|Arena")
	TArray<TObjectPtr<UStaticMeshComponent>> Walls;
};
