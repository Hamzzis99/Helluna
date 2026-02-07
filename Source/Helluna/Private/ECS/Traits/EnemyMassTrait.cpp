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
#include "Character/HellunaEnemyCharacter.h"

void UEnemyMassTrait::BuildTemplate(
	FMassEntityTemplateBuildContext& BuildContext,
	const UWorld& World) const
{
	// ------------------------------------------------------------------
	// 1단계: 스폰 상태 추적용 per-entity Fragment 추가
	// 각 Mass Entity마다 독립적인 스폰 상태(bHasSpawnedActor 등)를 추적한다.
	// 초기값은 기본 생성자로 충분 (bHasSpawnedActor = false)
	// ------------------------------------------------------------------
	BuildContext.AddFragment<FEnemySpawnStateFragment>();

	// ------------------------------------------------------------------
	// 2단계: 적 데이터 Fragment 추가 + 에디터 설정값으로 초기화
	// AddFragment_GetRef<T>(): Fragment를 아키타입에 등록하고 동시에
	// 참조를 반환하여 초기값을 설정할 수 있게 해주는 UE 5.7.2 API.
	// 이 값들은 이 MassEntityConfig에서 생성되는 모든 엔티티에 복사된다.
	// ------------------------------------------------------------------
	FEnemyDataFragment& DataFragment = BuildContext.AddFragment_GetRef<FEnemyDataFragment>();
	DataFragment.EnemyClass = EnemyClass;
	DataFragment.SpawnThreshold = SpawnThreshold;

	// ------------------------------------------------------------------
	// 빌드 결과 로그 출력 (에디터/쿠킹 시 확인용)
	// ------------------------------------------------------------------
	UE_LOG(LogTemp, Log, TEXT("[EnemyMassTrait] BuildTemplate - EnemyClass: %s, SpawnThreshold: %.1f"),
		EnemyClass ? *EnemyClass->GetName() : TEXT("None"), SpawnThreshold);
}
