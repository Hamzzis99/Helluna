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
 *   - 실행: 서버 + 스탠드얼론만 (클라이언트 X), 게임 스레드 필수
 *
 * ■ 매 틱 실행 흐름
 *   0. Pool 초기화 (첫 틱만): Fragment에서 EnemyClass/PoolSize 읽어 Pool 사전 생성
 *   1. 플레이어 위치 수집
 *   1.5. Pool 유지보수 (60프레임마다): 전투 사망 Actor 정리 + 보충
 *   2. 엔티티 순회 (ForEachEntityChunk):
 *      A) 이미 Actor: 파괴됨(bDead) / 멀어짐(역변환) / 범위 내(Tick 조절)
 *      B) 아직 Entity: 가까우면 Pool에서 Actor 꺼내기
 *   3. Soft Cap (30프레임마다): 초과분 중 가장 먼 Actor부터 Pool 반납
 *
 * ■ Phase 2 변경점
 *   - SpawnActor() → Pool->ActivateActor() (비용 ~2ms → ~0.1ms)
 *   - Destroy() → Pool->DeactivateActor() (GC 스파이크 제거)
 *   - Soft Cap: Fragment 포인터 직접 접근 → HP/위치 보존 후 Pool 반납
 *   - StateTree 0.2초 타이머 제거 (Pool Actor는 이미 Possess 완료 상태)
 *
 * ■ 디버깅 팁
 *   - LogECSEnemy 카테고리로 모든 스폰/디스폰/Soft Cap 이벤트 로깅
 *   - 300프레임마다 상태 로그: "[Status] 활성 Actor: N/M, Pool(Active: X, Inactive: Y)"
 *   - 문제별 대응은 .cpp 파일 하단 참조
 *
 * ■ UE 5.7.2 API (버전 고정)
 *   - ConfigureQueries(const TSharedRef<FMassEntityManager>&)
 *   - Execute(FMassEntityManager&, FMassExecutionContext&)
 *   - ForEachEntityChunk(Context, lambda)
 *   - RegisterQuery(생성자), RegisterWithProcessor(ConfigureQueries)
 *   - bRequiresGameThreadExecution = true
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
class UEnemyActorPool;

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
