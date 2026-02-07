/**
 * EnemyMassFragments.h
 *
 * 하이브리드 ECS 시스템에서 사용하는 Mass Entity Fragment 정의.
 *
 * 1) FEnemySpawnStateFragment (FMassFragment)
 *    - 각 Mass Entity별로 Actor 전환 여부를 추적하는 per-entity 상태 데이터.
 *    - Processor가 매 틱 읽어서 이미 전환된 엔티티는 스킵한다.
 *
 * 2) FEnemyDataFragment (FMassFragment)
 *    - Mass Entity가 Actor로 전환될 때 필요한 설정 데이터.
 *    - 스폰할 적 클래스, 전환 거리 임계값 등을 담는다.
 *    - UEnemyMassTrait::BuildTemplate에서 AddFragment_GetRef<T>()로 초기값 설정.
 *
 * ※ AIControllerClass 필드는 의도적으로 생략.
 *   AHellunaEnemyCharacter 생성자에 AutoPossessAI = PlacedInWorldOrSpawned가 설정되어 있어
 *   SpawnActor만 하면 엔진이 자동으로 AIController를 스폰+Possess 해준다.
 */

// File: Source/Helluna/Public/ECS/Fragments/EnemyMassFragments.h

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "EnemyMassFragments.generated.h"

// 전방선언 - 실제 include는 Processor의 cpp에서 수행
class AHellunaEnemyCharacter;

/**
 * FEnemySpawnStateFragment
 *
 * 각 Mass Entity의 Actor 전환 상태를 추적하는 per-entity Fragment.
 * Processor가 매 틱 이 Fragment를 읽어서 이미 전환된 엔티티는 스킵한다.
 */
USTRUCT()
struct HELLUNA_API FEnemySpawnStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Actor로 전환 완료 여부. true이면 이미 AHellunaEnemyCharacter가 스폰된 상태 */
	UPROPERTY()
	bool bHasSpawnedActor = false;

	/** 스폰된 Actor에 대한 약한 참조. 역전환(Actor->Mass) 또는 Actor 파괴 추적 시 사용 */
	UPROPERTY()
	TWeakObjectPtr<AActor> SpawnedActor;
};

/**
 * FEnemyDataFragment
 *
 * Mass Entity가 Actor로 전환될 때 필요한 설정 데이터를 담는 per-entity Fragment.
 * UEnemyMassTrait의 BuildTemplate에서 AddFragment_GetRef<T>()를 통해 초기값이 설정된다.
 *
 * ※ AIControllerClass 필드 미포함 이유:
 *   AHellunaEnemyCharacter 생성자에 AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned
 *   가 이미 설정되어 있어, SpawnActor 호출만으로 엔진이 자동으로
 *   BP/CDO에 지정된 AIController를 스폰하고 Possess한다.
 *   PossessedBy()에서 InitEnemyStartUpData()가 호출되어 GAS도 자동 초기화된다.
 */
USTRUCT()
struct HELLUNA_API FEnemyDataFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 스폰할 적 블루프린트 클래스 (예: BP_HellunaEnemyCharacter) */
	UPROPERTY()
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass;

	/** Actor 전환 거리 임계값 (단위: cm). 80m = 8000cm */
	UPROPERTY()
	float SpawnThreshold = 8000.f;
};

// ============================================================================
// TMassFragmentTraits 특수화
//
// UE 5.x에서 TSubclassOf<T>는 내부적으로 TObjectPtr<UClass>를 사용하고,
// TWeakObjectPtr<T>도 non-trivial 복사 생성자를 가진다.
// Mass Entity Fragment는 기본적으로 trivially copyable을 요구하므로,
// 이를 충족하지 못하는 Fragment에 대해 명시적으로 opt-out 해야 한다.
// ============================================================================

template<>
struct TMassFragmentTraits<FEnemySpawnStateFragment>
{
	enum { AuthorAcceptsItsNotTriviallyCopyable = true };
};

template<>
struct TMassFragmentTraits<FEnemyDataFragment>
{
	enum { AuthorAcceptsItsNotTriviallyCopyable = true };
};
