/**
 * EnemyMassTrait.cpp
 *
 * UEnemyMassTrait의 BuildTemplate 구현.
 * Mass Entity 아키타입에 FEnemySpawnStateFragment와 FEnemyDataFragment를 등록하고,
 * AddFragment_GetRef<T>()를 사용하여 에디터에서 설정한 값을 Fragment에 주입한다.
 *
 * ※ AddFragment_GetRef<T>()는 UE 5.7.2 FMassEntityTemplateBuildContext의 실제 API.
 *   (GetMutableFragmentTemplate<T>() 같은 함수는 존재하지 않는다!)
 * ※ FMassEntityTemplateBuildContext의 실제 정의는 "MassEntityTemplateRegistry.h"에 있다.
 */

// File: Source/Helluna/Private/ECS/Traits/EnemyMassTrait.cpp

#include "ECS/Traits/EnemyMassTrait.h"
#include "ECS/Fragments/EnemyMassFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"        // FTransformFragment
#include "Character/HellunaEnemyCharacter.h"

void UEnemyMassTrait::BuildTemplate(
	FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World) const
{
	// ------------------------------------------------------------------
	// 0단계: FTransformFragment 추가 (필수!)
	// MassSpawnLocationProcessor가 스폰 위치를 기록하고,
	// EnemyActorSpawnProcessor가 위치를 읽기 위해 반드시 필요하다.
	// 이것이 없으면 Processor 쿼리가 매칭되지 않아 Execute가 호출되지 않는다!
	// ------------------------------------------------------------------
	BuildContext.AddFragment<FTransformFragment>();

	// ------------------------------------------------------------------
	// 1단계: 스폰 상태 추적용 per-entity Fragment 추가
	// ------------------------------------------------------------------
	BuildContext.AddFragment<FEnemySpawnStateFragment>();

	// ------------------------------------------------------------------
	// 2단계: 적 데이터 Fragment 추가 + 에디터 설정값으로 초기화
	// ------------------------------------------------------------------
	FEnemyDataFragment& DataFragment = BuildContext.AddFragment_GetRef<FEnemyDataFragment>();
	DataFragment.EnemyClass = EnemyClass;
	DataFragment.SpawnThreshold = SpawnThreshold;

	UE_LOG(LogTemp, Log, TEXT("[EnemyMassTrait] BuildTemplate - EnemyClass: %s, SpawnThreshold: %.1f"),
		EnemyClass ? *EnemyClass->GetName() : TEXT("None"), SpawnThreshold);
}
