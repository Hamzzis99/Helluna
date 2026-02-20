/**
 * EnemyActorSpawnProcessor.h
 *
 * 하이브리드 ECS 핵심 Processor (Phase 1 + Phase 2 최적화 적용).
 *
 * ■ 이 파일이 뭔가요? (팀원용)
 *   매 틱마다 "이 적, Actor로 만들어야 하나? Entity로 돌려야 하나?"를 판단하는 두뇌입니다.
 *   플레이어와 적 사이 거리를 측정하여, 가까우면 Pool에서 Actor를 꺼내고,
 *   멀어지면 HP/위치를 저장한 뒤 Pool에 반납합니다.
 *
 * ■ 시스템 내 위치
 *   - 의존: FEnemySpawnStateFragment, FEnemyDataFragment, FTransformFragment (Fragment),
 *           UEnemyActorPool (Pool), AHellunaEnemyCharacter, UHellunaHealthComponent
 *   - 피의존: MassSimulation 서브시스템이 매 틱 자동 호출
 *   - 실행: Server | Standalone | Client (ExecutionFlags::All)
 *     → 스폰/디스폰은 서버 전용, 시각화는 서버/클라 공통
 *
 * ■ 매 틱 실행 흐름
 *   0. Pool 초기화 (첫 틱만): Fragment에서 EnemyClass/PoolSize 읽어 Pool 사전 생성
 *   1. 플레이어 위치 수집
 *   1.5. Pool 유지보수 (60프레임마다): 전투 사망 Actor 정리 + 보충
 *   2. 엔티티 순회 (ForEachEntityChunk):
 *      A) 이미 Actor: 파괴됨(bDead) / 멀어짐(역변환) / 범위 내(Tick 조절)
 *      B) 아직 Entity: 가까우면 Pool에서 Actor 꺼내기
 *   3. Soft Cap (30프레임마다): 초과분 중 가장 먼 Actor부터 Pool 반납
 *   4. 시각화: 서버/클라 공통으로 Entity ISMC 갱신
 *
 * ■ 디버깅 팁
 *   - LogECSEnemy 카테고리로 모든 스폰/디스폰/Soft Cap 이벤트 로깅
 *   - 300프레임마다 상태 로그: "[Status] 활성 Actor: N/M, Pool(Active: X, Inactive: Y)"
 *   - 문제별 대응은 .cpp 파일 하단 참조
 */

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "EnemyActorSpawnProcessor.generated.h"

// 전방선언
struct FEnemySpawnStateFragment;
struct FEnemyDataFragment;
struct FTransformFragment;
class UEnemyActorPool;
class USceneComponent;
class UInstancedStaticMeshComponent;

// ============================================================================
// Entity별 ISMC 인스턴스 참조 (스왑 삭제 대응)
// ============================================================================
struct FEntityInstanceRef
{
	TObjectPtr<UStaticMesh> Mesh = nullptr;
	int32 Index = INDEX_NONE;
};


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

	// =========================================================
	// 서버 전용 헬퍼
	// =========================================================

	/** 주어진 위치에서 모든 플레이어까지의 최소 제곱 거리 */
	static float CalcMinDistSq(const FVector& Location, const TArray<FVector>& PlayerLocations);

	/** Actor->Entity 역변환: HP/위치 보존 후 Pool에 반납 */
	static void DespawnActorToEntity(
		FEnemySpawnStateFragment& SpawnState,
		FEnemyDataFragment& Data,
		FTransformFragment& Transform,
		AActor* Actor,
		UEnemyActorPool* Pool);

	/** 거리별 Actor/Controller Tick 빈도 조절 */
	static void UpdateActorTickRate(AActor* Actor, float Distance, const FEnemyDataFragment& Data);

	/** Pool에서 Actor 꺼내기. HP 복원 포함. 성공 시 true */
	static bool TrySpawnActor(
		FEnemySpawnStateFragment& SpawnState,
		FEnemyDataFragment& Data,
		const FTransformFragment& Transform,
		UEnemyActorPool* Pool);

	// =========================================================
	// 시각화용 멤버 변수 (서버/클라 공통)
	// =========================================================

	/** ISMC를 붙이는 Root Actor */
	UPROPERTY(Transient)
	TObjectPtr<AActor> EntityVisualizationRoot;

	/** Root의 SceneComponent (ISMC Attach 대상) */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> EntityVisualizationRootComp;

	/** Mesh별 ISMC 맵 */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UInstancedStaticMeshComponent>> MeshToISMC;

	/** ISMC 인스턴스 인덱스 → Entity 역추적 (스왑 삭제 대응) */
	TMap<TObjectPtr<UStaticMesh>, TArray<FMassEntityHandle>> MeshToInstanceEntities;

	/** Entity → 인스턴스 참조 */
	TMap<FMassEntityHandle, FEntityInstanceRef> EntityToInstanceRef;

	// =========================================================
	// 시각화 헬퍼
	// =========================================================

	/** Root Actor + SceneComponent 보장 (없으면 생성) */
	void EnsureVisualizationRoot(UWorld* World);

	/** Mesh에 대한 ISMC 반환 (없으면 생성 후 Root에 Attach) */
	UInstancedStaticMeshComponent* GetOrCreateISMC(UStaticMesh* Mesh);

	/** Entity 시각화 업데이트 (서버/클라 공통) */
	void UpdateEntityVisualization(
		const FMassEntityHandle Entity,
		const FTransformFragment& Transform,
		const FEnemyDataFragment& Data,
		const FEnemySpawnStateFragment& SpawnState);

	/** ISMC 인스턴스 제거 (스왑 방식으로 인덱스 매핑 유지) */
	void CleanupEntityVisualization(const FMassEntityHandle Entity);
};
