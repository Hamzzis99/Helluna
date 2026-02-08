/**
 * EnemyMassFragments.h
 *
 * 하이브리드 ECS 시스템에서 사용하는 Mass Entity Fragment 정의.
 *
 * 1) FEnemySpawnStateFragment - 각 Entity의 Actor 전환 상태 추적
 * 2) FEnemyDataFragment - 스폰/디스폰/틱 최적화 설정 + HP 보존 데이터
 *
 * 모든 설정값은 UEnemyMassTrait에서 에디터로 설정하고,
 * BuildTemplate에서 AddFragment_GetRef<T>()로 Fragment에 복사된다.
 */

// File: Source/Helluna/Public/ECS/Fragments/EnemyMassFragments.h

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "EnemyMassFragments.generated.h"

class AHellunaEnemyCharacter;

// ============================================================================
// FEnemySpawnStateFragment
// 각 Mass Entity의 Actor 전환 상태를 추적하는 per-entity Fragment.
// ============================================================================
USTRUCT()
struct HELLUNA_API FEnemySpawnStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Actor로 전환 완료 여부. true이면 AHellunaEnemyCharacter가 스폰된 상태 */
	UPROPERTY()
	bool bHasSpawnedActor = false;

	/** 이 Entity의 적이 사망했는지 여부. true이면 다시는 Actor로 전환하지 않는다 */
	UPROPERTY()
	bool bDead = false;

	/** 스폰된 Actor에 대한 약한 참조. 역전환/파괴 추적 시 사용 */
	UPROPERTY()
	TWeakObjectPtr<AActor> SpawnedActor;
};

// ============================================================================
// FEnemyDataFragment
// 스폰/디스폰 설정 + 거리별 틱 최적화 + HP 보존 데이터.
// Trait에서 설정하는 값과 런타임 상태가 공존한다.
// ============================================================================
USTRUCT()
struct HELLUNA_API FEnemyDataFragment : public FMassFragment
{
	GENERATED_BODY()

	// === 스폰 설정 (Trait에서 복사) ===

	/** 스폰할 적 블루프린트 클래스 */
	UPROPERTY()
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass;

	// === 거리 설정 (Trait에서 복사) ===

	/** Entity->Actor 전환 거리 (cm). 기본 50m */
	UPROPERTY()
	float SpawnThreshold = 5000.f;

	/** Actor->Entity 복귀 거리 (cm). 기본 60m. 반드시 SpawnThreshold보다 커야 함 (히스테리시스) */
	UPROPERTY()
	float DespawnThreshold = 6000.f;

	// === Actor 제한 (Trait에서 복사) ===

	/** 동시 최대 Actor 수 (Soft Cap). 초과 시 먼 Actor부터 Entity로 복귀 */
	UPROPERTY()
	int32 MaxConcurrentActors = 50;

	/** Actor Pool 크기. MaxConcurrentActors + 버퍼. Phase 2 최적화용 */
	UPROPERTY()
	int32 PoolSize = 60;

	// === 거리별 Tick 빈도 (Trait에서 복사) ===

	/** 근거리 기준 (cm). 이 이내 = NearTickInterval 적용 */
	UPROPERTY()
	float NearDistance = 2000.f;

	/** 중거리 기준 (cm). Near~Mid = MidTickInterval 적용 */
	UPROPERTY()
	float MidDistance = 4000.f;

	/** 근거리 Tick 간격 (초). 0 = 매 틱 */
	UPROPERTY()
	float NearTickInterval = 0.f;

	/** 중거리 Tick 간격 (초). ~12Hz */
	UPROPERTY()
	float MidTickInterval = 0.08f;

	/** 원거리 Tick 간격 (초). ~4Hz. Mid~Despawn 구간 */
	UPROPERTY()
	float FarTickInterval = 0.25f;

	// === HP 보존 (런타임 상태 - Trait에서 설정하지 않음) ===

	/** 현재 HP. -1 = 아직 스폰 안 됨. Actor->Entity 복귀 시 저장, 재스폰 시 복원 */
	UPROPERTY()
	float CurrentHP = -1.f;

	/** 최대 HP. Actor에서 읽어서 저장 */
	UPROPERTY()
	float MaxHP = 100.f;
};

// ============================================================================
// TMassFragmentTraits 특수화
// TSubclassOf, TWeakObjectPtr는 non-trivially copyable이므로 명시적 opt-out 필요.
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
