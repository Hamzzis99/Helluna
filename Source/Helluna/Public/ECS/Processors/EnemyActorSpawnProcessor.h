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

// ============================================================================
// [TODO] 웨이브 시스템 연동 - GameMode 기반 밤/웨이브 스폰
// ============================================================================
//
// 현재: HellunaEnemyMassSpawner가 BeginPlay 시 고정 N마리를 한번에 스폰.
// 추후: GameMode가 밤 시작 시 웨이브별로 스폰 트리거.
//
// [웨이브 구조 (킬링플로어 + 하이브리드 ECS)]
//
//   밤 시작 → GameMode::StartNightWave(WaveIndex)
//     │
//     ▼
//   [웨이브 1] 총 60마리
//     → HellunaEnemyMassSpawner::DoSpawning() 호출 (60 Entity 한번에 생성)
//     → Entity들이 기지 방향으로 이동 (EntityMovementProcessor)
//     → 50m 이내 진입 시 Actor 전환 (Soft Cap 50마리 제한)
//     → 플레이어가 전투 → 적 사망 → GameMode::NotifyMonsterDied()
//     → 60마리 전멸 확인 → 다음 웨이브
//     │
//     ▼
//   [웨이브 2] 총 100마리
//     → 더 강한 적, 더 많은 수
//     → 동일 흐름
//
// [핵심 포인트]
// - Entity 60마리 한번에 생성해도 메모리 ~10KB (Actor 대비 300배 가벼움)
// - Soft Cap이 자동으로 "몰려오는 느낌" 연출:
//   앞의 적이 죽으면 → Actor 슬롯 빈다 → 뒤의 Entity가 자연스럽게 Actor 전환
//   = 킬링플로어의 "다음 투입" 효과가 코드 없이 자동 발생!
// - 웨이브별 스폰 설정은 DataTable 또는 DataAsset으로 관리 예정
//
// [GameMode 연동 인터페이스]
// - HellunaEnemyMassSpawner::DoSpawning()  // 이미 존재 (부모 AMassSpawner)
// - HellunaEnemyMassSpawner::DoDespawning() // 이미 존재
// - GameMode::RegisterAliveMonster()  // 이미 존재 (AHellunaEnemyCharacter::BeginPlay)
// - GameMode::NotifyMonsterDied()     // 이미 존재 (AHellunaEnemyCharacter::OnMonsterDeath)
//
// [웨이브 관리에 필요한 추가 데이터]
// - FEnemySpawnStateFragment에 추가:
//   int32 WaveIndex = 0;        // 이 Entity가 속한 웨이브 번호
//   bool bDead = false;         // Entity 대응 Actor가 사망했는지 (전멸 체크용)
// - GameMode에 추가:
//   int32 CurrentWave, TotalEnemiesThisWave, KilledEnemiesThisWave
//   TArray<FWaveConfig> WaveConfigs (DataAsset에서 로드)
// ============================================================================

// ============================================================================
// [TODO] Actor Pooling - Phase 2 최적화
// ============================================================================
//
// 현재: SpawnActor() / Destroy()로 Actor 생성/파괴 (비용 높음)
// 추후: 미리 생성된 Pool에서 Activate/Deactivate (비용 거의 0)
//
// [구현 계획]
// - EnemyActorPool 클래스 신규 생성 (UActorComponent 또는 독립 UObject)
// - BeginPlay 시 PoolSize(60)개 Actor 사전 생성 → Hidden + Tick Off + Collision Off
// - Entity→Actor 전환 시: Pool에서 꺼내기 (Activate)
//   SetActorHiddenInGame(false), SetActorEnableCollision(true),
//   SetActorTickInterval(0), AIController->StartLogic(), SetReplicates(true)
// - Actor→Entity 복귀 시: Pool에 반납 (Deactivate)
//   SetActorHiddenInGame(true), SetActorEnableCollision(false),
//   SetActorTickEnabled(false), AIController->StopLogic(), SetReplicates(false)
//
// [Pool 크기]
// PoolSize = MaxConcurrentActors + 버퍼 = 50 + 10 = 60
//
// [역변환(Phase 1)과의 관계]
// Phase 1: SpawnActor/Destroy 사용 (현재 구현)
// Phase 2: Pool의 Activate/Deactivate로 대체 (성능 업그레이드)
// → Phase 1의 스폰/디스폰 호출부만 Pool 호출로 교체하면 됨
// ============================================================================
