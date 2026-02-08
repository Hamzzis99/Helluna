/**
 * EnemyActorSpawnProcessor.h
 *
 * 하이브리드 ECS 핵심 Processor (Phase 1 최적화 적용).
 *
 * [매 틱 실행 흐름]
 * 1. 플레이어 위치 수집
 * 2. 엔티티 순회:
 *    a) 스폰된 Actor 중 파괴된 것 → 상태 정리
 *    b) 스폰된 Actor가 DespawnThreshold 밖 → 역변환 (HP/위치 보존 후 Destroy)
 *    c) 스폰된 Actor가 범위 내 → 거리별 Tick 빈도 조절
 *    d) 미스폰 Entity가 SpawnThreshold 내 → Actor 스폰 (MaxConcurrentActors 미만일 때)
 * 3. Soft Cap (30프레임마다): 초과분 중 가장 먼 Actor부터 Destroy
 *
 * [UE 5.7.2 API]
 * - ConfigureQueries(const TSharedRef<FMassEntityManager>&)
 * - Execute(FMassEntityManager&, FMassExecutionContext&)
 * - ForEachEntityChunk(Context, lambda)
 * - RegisterQuery(생성자), RegisterWithProcessor(ConfigureQueries)
 * - bRequiresGameThreadExecution = true
 */

// File: Source/Helluna/Public/ECS/Processors/EnemyActorSpawnProcessor.h

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "EnemyActorSpawnProcessor.generated.h"

// 전방선언 (헤더 경량화)
struct FEnemySpawnStateFragment;
struct FEnemyDataFragment;
struct FTransformFragment;

UCLASS()
class HELLUNA_API UEnemyActorSpawnProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UEnemyActorSpawnProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;

	// === 헬퍼 함수 ===

	/** 주어진 위치에서 모든 플레이어까지의 최소 제곱 거리 */
	static float CalcMinDistSq(const FVector& Location, const TArray<FVector>& PlayerLocations);

	/** Actor->Entity 역변환: HP/위치 보존 후 Controller+Actor 파괴 */
	static void DespawnActorToEntity(
		FEnemySpawnStateFragment& SpawnState,
		FEnemyDataFragment& Data,
		FTransformFragment& Transform,
		AActor* Actor);

	/** 거리별 Actor/Controller Tick 빈도 조절 */
	static void UpdateActorTickRate(AActor* Actor, float Distance, const FEnemyDataFragment& Data);

	/** Entity->Actor 스폰. HP 복원 포함. 성공 시 true */
	static bool TrySpawnActor(
		FEnemySpawnStateFragment& SpawnState,
		FEnemyDataFragment& Data,
		const FTransformFragment& Transform,
		UWorld* World);
};
