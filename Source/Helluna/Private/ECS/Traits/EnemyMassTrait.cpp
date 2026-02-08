/**
 * EnemyMassTrait.cpp
 *
 * BuildTemplate에서 모든 Trait UPROPERTY 값을 FEnemyDataFragment에 복사한다.
 * CurrentHP/MaxHP는 런타임 상태이므로 기본값(-1/100) 유지.
 */

// File: Source/Helluna/Private/ECS/Traits/EnemyMassTrait.cpp

#include "ECS/Traits/EnemyMassTrait.h"
#include "ECS/Fragments/EnemyMassFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "Character/HellunaEnemyCharacter.h"

void UEnemyMassTrait::BuildTemplate(
	FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World) const
{
	// FTransformFragment (Processor가 위치를 읽고 역변환 시 갱신)
	BuildContext.AddFragment<FTransformFragment>();

	// 스폰 상태 추적용 (bHasSpawnedActor, SpawnedActor)
	BuildContext.AddFragment<FEnemySpawnStateFragment>();

	// 설정 데이터 Fragment + 에디터 값 복사
	FEnemyDataFragment& Data = BuildContext.AddFragment_GetRef<FEnemyDataFragment>();

	// --- 스폰 설정 ---
	Data.EnemyClass = EnemyClass;

	// --- 거리 설정 ---
	Data.SpawnThreshold = SpawnThreshold;
	Data.DespawnThreshold = DespawnThreshold;

	// --- Actor 제한 ---
	Data.MaxConcurrentActors = MaxConcurrentActors;
	Data.PoolSize = PoolSize;

	// --- Tick 최적화 ---
	Data.NearDistance = NearDistance;
	Data.MidDistance = MidDistance;
	Data.NearTickInterval = NearTickInterval;
	Data.MidTickInterval = MidTickInterval;
	Data.FarTickInterval = FarTickInterval;

	// CurrentHP(-1), MaxHP(100)은 런타임 상태이므로 기본값 유지

	UE_LOG(LogTemp, Log,
		TEXT("[EnemyMassTrait] BuildTemplate - Class: %s, Spawn: %.0f, Despawn: %.0f, MaxActors: %d"),
		EnemyClass ? *EnemyClass->GetName() : TEXT("None"),
		SpawnThreshold, DespawnThreshold, MaxConcurrentActors);
}
