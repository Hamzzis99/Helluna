/**
 * EnemyActorSpawnProcessor.h
 *
 * 하이브리드 ECS 시스템의 핵심 Processor.
 * Mass Entity를 서버에서 시뮬레이션하다가, 플레이어가 가까이 오면
 * AHellunaEnemyCharacter(기존 GAS/BT/AI 탑재)를 스폰하여
 * 일반 UE 리플리케이션으로 클라이언트에 보이게 한다.
 *
 * 서버 전용 실행. 클라이언트에서는 동작하지 않는다.
 *
 * [실행 흐름]
 * 1. 매 틱: 서버의 모든 PlayerController에서 Pawn 위치를 수집
 * 2. 모든 Mass Entity를 순회하며 플레이어와의 최소 거리(제곱) 계산
 * 3. 거리 < SpawnThreshold이면:
 *    - SpawnActor<AHellunaEnemyCharacter> 호출
 *    - AutoPossessAI = PlacedInWorldOrSpawned 이므로 엔진이 자동으로:
 *      AIController 스폰 → Possess → PossessedBy → InitEnemyStartUpData (GAS 초기화)
 *    - bReplicates=true (ACharacter 기본) → 클라이언트에 자동 복제
 * 4. 이미 전환된 엔티티(bHasSpawnedActor=true)는 스킵
 *
 * [UE 5.7.2 API 주의사항]
 * - ConfigureQueries: const TSharedRef<FMassEntityManager>& 파라미터 필수 (파라미터 없는 버전은 final)
 * - Execute: FMassEntityManager& + FMassExecutionContext& 둘 다 받음
 * - ForEachEntityChunk: Context만 넘긴다 (EntityManager 넘기는 버전은 deprecated)
 * - RegisterQuery: 생성자에서 호출해야 함 (CallInitialize가 OwnedQueries를 먼저 Initialize하므로)
 * - GetWorld(): Context.GetWorld() 사용 (UObject::GetWorld()는 불안정)
 * - bRequiresGameThreadExecution = true (SpawnActor는 게임 스레드에서만 가능)
 */

// File: Source/Helluna/Public/ECS/Processors/EnemyActorSpawnProcessor.h

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "EnemyActorSpawnProcessor.generated.h"

/**
 * UEnemyActorSpawnProcessor
 *
 * Mass Entity <-> Actor 전환을 담당하는 서버 전용 Processor.
 * EProcessorExecutionFlags::Server | Standalone으로 설정되어
 * 데디케이티드 서버와 Standalone(에디터 테스트) 모두에서 실행된다.
 */
UCLASS()
class HELLUNA_API UEnemyActorSpawnProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	/** 생성자: 실행 플래그, 게임 스레드 실행 요구, RegisterQuery 설정 */
	UEnemyActorSpawnProcessor();

protected:
	/**
	 * UE 5.7.2 시그니처: EntityManager 참조를 받는 버전.
	 * (파라미터 없는 ConfigureQueries()는 UE 5.6에서 final + deprecated 처리됨)
	 * Fragment 요구사항 등록 + RegisterQuery 호출.
	 */
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;

	/**
	 * UE 5.7.2 시그니처: EntityManager + Context 둘 다 받는다.
	 * 매 틱 실행: 플레이어 거리 체크 -> Actor 스폰 판단.
	 */
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	/** 이 Processor가 처리할 엔티티 조건을 정의하는 쿼리 */
	FMassEntityQuery EntityQuery;
};
