/**
 * EnemyActorPool.h
 *
 * Phase 2 최적화: Actor Object Pooling (UWorldSubsystem).
 *
 * ■ 이 파일이 뭔가요? (팀원용)
 *   적 Actor를 미리 60개 만들어놓고, 필요할 때 꺼내 쓰고 다 쓰면 돌려놓는 "대여소"입니다.
 *   매번 새로 만들고(SpawnActor ~2ms) 부수는(Destroy → GC 스파이크) 대신,
 *   숨겼다 보여주는 방식(~0.1ms)으로 전환하여 프레임 드랍을 방지합니다.
 *
 * ■ 시스템 내 위치
 *   - 의존: AHellunaEnemyCharacter (스폰 대상), UHellunaHealthComponent (HP 복원),
 *           UStateTreeComponent (AI 시작/정지), AHellunaAIController (Controller Tick)
 *   - 피의존: UEnemyActorSpawnProcessor (TrySpawnActor/DespawnActorToEntity에서 호출)
 *   - 접근: World->GetSubsystem<UEnemyActorPool>()
 *
 * ■ 작동 원리
 *   1. Processor 첫 틱: InitializePool() → PoolSize(60)개 Actor 사전 생성
 *      - Hidden + TickOff + CollisionOff + ReplicateOff 상태로 대기
 *   2. Entity→Actor 전환: ActivateActor()
 *      - Pool에서 꺼내기 → 위치/HP 설정 → 보이기 → AI RestartLogic → 리플리케이션 On
 *   3. Actor→Entity 복귀: DeactivateActor()
 *      - AI StopLogic → 숨기기 → TickOff + CollisionOff + ReplicateOff → Pool 반납
 *   4. 전투 사망: Actor가 외부에서 Destroy됨 → CleanupAndReplenish()가 Pool 보충
 *
 * ■ Phase 1과의 관계
 *   Phase 1: SpawnActor/Destroy로 생성/파괴 (비용 높음, ~2ms/회)
 *   Phase 2: Pool의 Activate/Deactivate로 교체 (비용 거의 0, ~0.1ms/회)
 *   → DespawnActorToEntity/TrySpawnActor 내부 호출만 교체, 외부 흐름 동일
 *
 * ■ 디버깅 팁
 *   - LogECSPool 카테고리로 모든 Pool 이벤트 확인
 *   - 콘솔: `Log LogECSPool Verbose`
 *   - GetActiveCount() / GetInactiveCount()로 Pool 상태 확인
 *   - 문제별 대응은 파일 하단 [디버깅 가이드] 참조
 *
 * ■ 에디터 사용법
 *   Pool 크기는 MassEntityConfig → EnemyMassTrait → PoolSize에서 설정.
 *   코드 수정 없이 에디터에서 60→80 등으로 변경 가능.
 */

// File: Source/Helluna/Public/ECS/Pool/EnemyActorPool.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyActorPool.generated.h"

// 전방선언 (헤더 경량화)
class AHellunaEnemyCharacter;

// ============================================================================
// UEnemyActorPool
// ============================================================================
// ■ 역할: 적 Actor의 Object Pooling을 관리하는 UWorldSubsystem.
// ■ 왜 필요: SpawnActor/Destroy 비용(~2ms)을 Activate/Deactivate(~0.1ms)로 대체.
// ■ 왜 UWorldSubsystem: 월드당 하나 자동 생성, World->GetSubsystem<T>()로 어디서든 접근.
// ■ 생명주기:
//   World 생성 → ShouldCreateSubsystem(true) → 서브시스템 자동 생성
//   → Processor 첫 틱 → InitializePool() 호출 (PoolSize개 Actor 사전 생성)
//   → 게임 중: Activate/Deactivate 반복
//   → World 소멸 → Deinitialize() → 모든 Pool Actor 파괴
// ============================================================================
UCLASS()
class HELLUNA_API UEnemyActorPool : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// === UWorldSubsystem 인터페이스 ===

	/** 서브시스템 생성 여부. 항상 true (모든 월드에서 사용) */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 월드 소멸 시 Pool의 모든 Actor 파괴 */
	virtual void Deinitialize() override;

	// === Pool 초기화 ===

	/**
	 * Pool을 초기화하고 PoolSize개의 Actor를 사전 생성한다.
	 * Processor의 첫 틱에서 Fragment 데이터를 읽어 호출.
	 * 중복 호출 시 무시된다 (로그 Warning).
	 *
	 * @param EnemyClass  스폰할 적 블루프린트 클래스 (Trait에서 설정)
	 * @param InPoolSize  사전 생성할 Actor 수 (기본 60 = MaxActors 50 + 버퍼 10)
	 */
	void InitializePool(TSubclassOf<AHellunaEnemyCharacter> EnemyClass, int32 InPoolSize);

	/** Pool이 초기화되었는지 여부 */
	bool IsPoolInitialized() const { return bInitialized; }

	// === Actor 활성화/비활성화 ===

	/**
	 * Pool에서 비활성 Actor를 꺼내 활성화한다.
	 * 위치 설정 → 보이기 → Collision/Tick On → AI RestartLogic → HP 복원 → 리플리케이션 On.
	 *
	 * @param SpawnTransform  Actor를 배치할 위치/회전 (Fragment의 Transform)
	 * @param CurrentHP       복원할 HP (-1이면 풀 HP 사용 = 첫 스폰)
	 * @param MaxHP           최대 HP (로그용)
	 * @return                활성화된 Actor. Pool이 비어있으면 nullptr.
	 */
	AHellunaEnemyCharacter* ActivateActor(
		const FTransform& SpawnTransform,
		float CurrentHP,
		float MaxHP);

	/**
	 * Actor를 비활성화하고 Pool에 반납한다.
	 * AI StopLogic → Tick/Collision Off → 숨김 → 리플리케이션 Off → 숨김 위치 이동.
	 *
	 * @param Actor  비활성화할 Actor (nullptr이거나 유효하지 않으면 무시)
	 */
	void DeactivateActor(AHellunaEnemyCharacter* Actor);

	// === Pool 유지보수 ===

	/**
	 * 전투 사망 등으로 파괴된 Pool Actor를 정리하고 새로 보충한다.
	 * Processor의 Execute()에서 60프레임마다 호출.
	 * 파괴된 Actor가 없으면 아무것도 하지 않는다.
	 */
	void CleanupAndReplenish();

	// === 상태 조회 ===

	/** 현재 활성 (월드에 보이는) Actor 수 */
	int32 GetActiveCount() const { return ActiveActors.Num(); }

	/** 현재 비활성 (Pool 대기 중) Actor 수 */
	int32 GetInactiveCount() const { return InactiveActors.Num(); }

private:
	/** 비활성 Actor Pool (대기 중, Hidden 상태) */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaEnemyCharacter>> InactiveActors;

	/** 활성 Actor 목록 (월드에 보이는 상태) */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaEnemyCharacter>> ActiveActors;

	/** 캐싱된 적 블루프린트 클래스 (Pool 보충 시 사용) */
	UPROPERTY()
	TSubclassOf<AHellunaEnemyCharacter> CachedEnemyClass;

	/** 목표 Pool 크기 (Active + Inactive 합계) */
	int32 DesiredPoolSize = 0;

	/** 초기화 완료 여부 */
	bool bInitialized = false;

	/**
	 * 단일 Actor를 사전 생성한다.
	 * Hidden + TickOff + CollisionOff + ReplicateOff 상태로 생성.
	 * AutoPossessAI 완료 후(0.3초 타이머) AI Controller + StateTree 정지.
	 */
	AHellunaEnemyCharacter* CreatePooledActor();

	/** Pool Actor 보관용 숨김 위치 (맵 아래 Z=-50000) */
	static const FVector PoolHiddenLocation;
};

// ============================================================================
// [사용법 — 에디터 설정 가이드]
// ============================================================================
//
// ■ 이 기능이 뭔가요?
//   적 Actor를 미리 만들어놓고 재활용하는 Object Pooling 시스템입니다.
//   SpawnActor/Destroy 대신 Activate/Deactivate로 전환하여 프레임 드랍을 방지합니다.
//
// ■ 설정 방법
//   1. 에디터에서 MassEntityConfig 에셋 열기
//   2. Traits → Enemy Mass Trait 선택
//   3. "Actor 제한" 카테고리에서 PoolSize 설정
//      - 기본값: 60 (= MaxConcurrentActors 50 + 버퍼 10)
//
// ■ 설정값 권장
//   | 설정         | 기본값 | 권장        | 설명                              |
//   |-------------|--------|-------------|-----------------------------------|
//   | PoolSize    | 60     | Max+10~20   | MaxConcurrentActors + 버퍼        |
//   | (Max=50)    |        | 60~70       | 버퍼가 클수록 Pool 소진 가능성 ↓   |
//   | (Max=100)   |        | 110~120     | 고사양 서버용                      |
//
// ■ 테스트 방법
//   1. PIE 실행 (서버 + 클라이언트)
//   2. 로그 확인: `Log LogECSPool Log` → "[Pool] 초기화 완료!" 확인
//   3. 적에게 접근하여 Actor 활성화 확인 → "[Pool] Activate!" 로그
//   4. 적에게서 멀어져 Despawn 확인 → "[Pool] Deactivate!" 로그
//   5. `stat unit` → Game Thread 시간이 Phase 1보다 감소했는지 확인
//   6. 300프레임마다 상태 로그: "[Status] ... Pool(Active: N, Inactive: M)"
//
// ■ FAQ
//   Q: Pool 크기를 어떻게 정하나요?
//   A: MaxConcurrentActors + 10~20. 버퍼는 Soft Cap 발동 전 순간적 초과분 흡수용.
//
//   Q: 전투 중 Actor가 사망하면 Pool은 어떻게 되나요?
//   A: 외부에서 Destroy된 Actor는 CleanupAndReplenish()가 60프레임마다 감지.
//      파괴된 Actor를 목록에서 제거하고 새 Actor를 Pool에 보충합니다.
//
//   Q: ActivateActor 시 0.2초 타이머가 필요한가요?
//   A: 아닙니다. Pool Actor는 초기 생성 시 이미 AutoPossessAI가 완료되어
//      Controller가 Pawn을 Possess한 상태입니다. RestartLogic을 즉시 호출해도 안전합니다.
//
//   Q: Pool이 비어서 nullptr가 반환되면?
//   A: 로그 "[Pool] 비활성 Actor 없음! Pool 소진."이 출력됩니다.
//      PoolSize를 늘리거나, MaxConcurrentActors를 줄이세요.
//
//   Q: 초기 로딩이 느려졌는데요?
//   A: 60개 Actor를 한 번에 생성하므로 첫 틱에서 ~120ms 소요될 수 있습니다.
//      PoolSize를 줄이거나, 향후 분할 생성(10개/프레임 × 6프레임) 적용 예정.
// ============================================================================

// ============================================================================
// [디버깅 가이드]
// ============================================================================
//
// ■ 증상: "Pool 소진" 경고가 자주 발생
//   1. PoolSize가 MaxConcurrentActors보다 충분히 큰지 확인
//   2. CleanupAndReplenish가 주기적으로 호출되는지 로그 확인
//   → 해결: PoolSize를 MaxConcurrentActors + 20 이상으로 설정
//
// ■ 증상: Activate 후 AI가 동작하지 않음
//   1. Controller가 Pawn을 Possess하고 있는지 확인 (GetController() != nullptr)
//   2. StateTreeComponent가 Controller에 있는지 확인
//   3. RestartLogic() 후 IsRunning() 확인
//   → 해결: CreatePooledActor의 0.3초 타이머가 올바르게 StopLogic 호출했는지 확인
//           ActivateActor에서 RestartLogic 호출 순서 확인
//
// ■ 증상: Deactivate 후에도 Actor가 보임
//   1. SetActorHiddenInGame(true) 호출 확인
//   2. SetReplicates(false) 호출 확인 (클라이언트에서 안 보이는지)
//   → 해결: DeactivateActor 함수 호출 순서 확인
//
// ■ 증상: Pool Actor 생성 시 클라이언트에 순간적으로 보임
//   1. CreatePooledActor에서 SetReplicates(false)가 SpawnActor 직후 호출되는지 확인
//   2. SetActorHiddenInGame(true)가 즉시 호출되는지 확인
//   → 해결: SpawnActor 직후 즉시 Hidden+ReplicateOff 설정 (현재 구현 확인)
//
// ■ 증상: 전투 사망 후 Pool이 계속 줄어듦
//   1. CleanupAndReplenish 로그 "[Pool] Replenish!" 확인
//   2. CreatePooledActor가 정상 동작하는지 확인
//   → 해결: 60프레임 주기 확인, CachedEnemyClass가 유효한지 확인
//
// ■ 로그 확인 명령어
//   콘솔: Log LogECSPool Log        → 일반 Pool 이벤트
//   콘솔: Log LogECSPool Verbose    → 상세 Pool 이벤트
//   콘솔: Log LogECSEnemy Log       → Processor 이벤트 (Pool 연동 포함)
// ============================================================================

// ============================================================================
// [TODO] GameMode 연동 — RegisterAliveMonster / NotifyMonsterDied
// ============================================================================
//
// 현재: Pool Actor는 BeginPlay에서 RegisterAliveMonster()가 한 번만 호출됨.
// 문제: DeactivateActor 시 GameMode에서 제거되지 않고,
//       ActivateActor 시 다시 등록되지 않음.
//
// [해결 방안]
// - ActivateActor 끝에: GameMode->RegisterAliveMonster(Actor) 호출
// - DeactivateActor 시작에: GameMode에서 해당 Actor 제거 (죽음이 아닌 비활성화)
// - 전투 사망 시: 기존 OnMonsterDeath → NotifyMonsterDied() 그대로 유지
//
// [주의] GameMode의 MonsterCount 관리와 충돌하지 않도록 주의.
//        Pool 비활성화 ≠ 사망이므로 별도 처리 필요.
// ============================================================================
