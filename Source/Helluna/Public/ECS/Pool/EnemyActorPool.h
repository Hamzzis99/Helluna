/**
 * EnemyActorPool.h
 *
 * Phase 2 최적화: Actor Object Pooling (UWorldSubsystem).
 *
 * ■ 이 파일이 뭔가요? (팀원용)
 *   적 Actor를 미리 만들어놓고, 필요할 때 꺼내 쓰고 다 쓰면 돌려놓는 "대여소"입니다.
 *   매번 새로 만들고(SpawnActor ~2ms) 부수는(Destroy → GC 스파이크) 대신,
 *   숨겼다 보여주는 방식(~0.1ms)으로 전환하여 프레임 드랍을 방지합니다.
 *
 * ■ [버그 수정] EnemyClass별 분리 Pool (멀티 스포너 지원)
 *   이전 구조: Pool이 단 하나 → InitializePool()이 처음 호출된 EnemyClass로 고정됨
 *              → 두 번째 스포너(원거리 등)의 InitializePool() 호출은 bInitialized=true로 무시됨
 *              → 원거리 엔티티가 ActivateActor()를 호출해도 Pool에는 근거리 Actor만 있어
 *                 근거리 Actor가 2마리 나오는 버그 발생
 *
 *   수정 구조: EnemyClass → FActorPoolData 매핑(TMap)으로 변경
 *              → 근거리/원거리 클래스마다 독립된 Pool 유지
 *              → IsPoolInitialized(EnemyClass)로 클래스별 초기화 여부 확인
 *              → ActivateActor/DeactivateActor도 EnemyClass 기준으로 올바른 Pool에서 동작
 *
 * ■ 시스템 내 위치
 *   - 의존: AHellunaEnemyCharacter (스폰 대상), UHellunaHealthComponent (HP 복원),
 *           UStateTreeComponent (AI 시작/정지), AHellunaAIController (Controller Tick)
 *   - 피의존: UEnemyActorSpawnProcessor (TrySpawnActor/DespawnActorToEntity에서 호출)
 *   - 접근: World->GetSubsystem<UEnemyActorPool>()
 *
 * ■ 작동 원리
 *   1. Processor 첫 틱: InitializePool(EnemyClass, PoolSize) → 클래스별 Actor 사전 생성
 *      - 근거리 Processor → InitializePool(MeleeClass, 60)
 *      - 원거리 Processor → InitializePool(RangeClass, 60)  ← 별도 Pool에 정상 등록됨
 *   2. Entity→Actor 전환: ActivateActor(EnemyClass, ...)
 *      - EnemyClass에 해당하는 Pool에서 꺼내기 → 위치/HP → 보이기
 *   3. Actor→Entity 복귀: DeactivateActor(Actor)
 *      - Actor의 클래스를 키로 해당 Pool에 반납
 *   4. 전투 사망: CleanupAndReplenish() → 클래스별 Pool을 모두 순회하여 보충
 *
 * ■ 디버깅 팁
 *   - LogECSPool 카테고리로 모든 Pool 이벤트 확인
 *   - 콘솔: `Log LogECSPool Verbose`
 *   - GetActiveCount(EnemyClass) / GetInactiveCount(EnemyClass)로 클래스별 Pool 상태 확인
 */

// File: Source/Helluna/Public/ECS/Pool/EnemyActorPool.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnemyActorPool.generated.h"

// 전방선언 (헤더 경량화)
class AHellunaEnemyCharacter;

// ============================================================================
// FActorPoolData — 단일 EnemyClass에 대한 Pool 데이터
// ============================================================================
// ■ 역할: 하나의 적 클래스에 대한 비활성/활성 Actor 목록과 메타데이터를 묶은 구조체.
// ■ 왜 USTRUCT: UPROPERTY로 GC(가비지 컬렉터) 추적이 가능해야 Actor가 실수로 파괴되지 않음.
// ■ 멀티 Pool 구조에서 TMap의 Value 타입으로 사용됨.
// ============================================================================
USTRUCT()
struct FActorPoolData
{
	GENERATED_BODY()

	/** 대기 중인 비활성 Actor 배열 (Hidden, TickOff 상태) */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaEnemyCharacter>> InactiveActors;

	/** 현재 활성화된 Actor 배열 (월드에 보이는 상태) */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaEnemyCharacter>> ActiveActors;

	/** 목표 Pool 크기 (Active + Inactive 합계 유지 목표) */
	int32 DesiredPoolSize = 0;

	/** 이 Pool의 초기화 완료 여부 */
	bool bInitialized = false;
};

// ============================================================================
// UEnemyActorPool
// ============================================================================
// ■ 역할: EnemyClass별 독립 Pool을 관리하는 UWorldSubsystem.
// ■ 핵심 변경: 단일 Pool → TMap<EnemyClass, FActorPoolData> 멀티 Pool
//   → 근거리/원거리 등 서로 다른 클래스를 사용하는 여러 스포너를 동시에 지원.
// ■ 왜 UWorldSubsystem: 월드당 하나 자동 생성, GetSubsystem<T>()로 어디서든 접근.
// ============================================================================
UCLASS()
class HELLUNA_API UEnemyActorPool : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// === UWorldSubsystem 인터페이스 ===

	/** 서브시스템 생성 여부. 항상 true (모든 월드에서 사용) */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 월드 소멸 시 모든 EnemyClass의 Pool Actor 전부 파괴 */
	virtual void Deinitialize() override;

	// =========================================================================
	// Pool 초기화
	// =========================================================================

	/**
	 * 특정 EnemyClass에 대한 Pool을 초기화하고 Actor를 사전 생성한다.
	 * Processor의 첫 틱에서 Fragment의 EnemyClass를 읽어 호출.
	 *
	 * [기존 단일 Pool 방식의 문제]
	 *   InitializePool(근거리Class) 이후 InitializePool(원거리Class)가 호출되면
	 *   bInitialized=true 체크로 두 번째 호출이 완전히 무시됨.
	 *   → 원거리 Pool이 생성되지 않아 원거리 엔티티가 근거리 Actor를 꺼내 쓰는 버그.
	 *
	 * [수정된 동작]
	 *   EnemyClass가 다르면 별도의 FActorPoolData를 생성하여 독립 Pool을 만든다.
	 *   같은 클래스로 중복 호출 시에만 무시(Warning 로그).
	 *
	 * @param EnemyClass  이 Pool에서 관리할 적 블루프린트 클래스
	 * @param InPoolSize  사전 생성할 Actor 수 (기본 60)
	 */
	void InitializePool(TSubclassOf<AHellunaEnemyCharacter> EnemyClass, int32 InPoolSize);

	/**
	 * 특정 EnemyClass의 Pool이 초기화되었는지 확인한다.
	 * Processor에서 "이 클래스의 Pool이 준비됐는가"를 매 틱 체크하는 데 사용.
	 *
	 * @param EnemyClass  확인할 적 클래스
	 * @return            해당 클래스의 Pool이 초기화되어 있으면 true
	 */
	bool IsPoolInitialized(TSubclassOf<AHellunaEnemyCharacter> EnemyClass) const;

	// =========================================================================
	// Actor 활성화 / 비활성화
	// =========================================================================

	/**
	 * 지정한 EnemyClass의 Pool에서 비활성 Actor를 꺼내 활성화한다.
	 * [EnemyClass 파라미터 추가 이유]
	 *   단일 Pool일 때는 Pool이 하나뿐이라 클래스 구분이 불필요했으나,
	 *   멀티 Pool에서는 어떤 Pool에서 꺼낼지를 반드시 지정해야 한다.
	 *
	 * @param EnemyClass      꺼낼 Actor의 클래스 (근거리/원거리 구분)
	 * @param SpawnTransform  배치할 위치/회전
	 * @param CurrentHP       복원할 HP (-1이면 MaxHP 사용)
	 * @param MaxHP           최대 HP (로그용)
	 * @return                활성화된 Actor. 해당 클래스 Pool이 없거나 비어있으면 nullptr.
	 */
	AHellunaEnemyCharacter* ActivateActor(
		TSubclassOf<AHellunaEnemyCharacter> EnemyClass,
		const FTransform& SpawnTransform,
		float CurrentHP,
		float MaxHP);

	/**
	 * Actor를 비활성화하고 해당 클래스의 Pool에 반납한다.
	 * Actor 자신의 GetClass()를 키로 사용하여 올바른 Pool을 자동으로 찾는다.
	 *
	 * @param Actor  반납할 Actor (nullptr 또는 유효하지 않으면 무시)
	 */
	void DeactivateActor(AHellunaEnemyCharacter* Actor);

	// =========================================================================
	// Pool 유지보수
	// =========================================================================

	/**
	 * 전투 사망 등으로 파괴된 Pool Actor를 정리하고 새로 보충한다.
	 * 모든 EnemyClass의 Pool을 순회하여 IsValid가 false인 Actor를 제거하고 보충.
	 * Processor의 Execute()에서 60프레임마다 호출.
	 */
	void CleanupAndReplenish();

	// =========================================================================
	// 상태 조회
	// =========================================================================

	/**
	 * 특정 EnemyClass의 활성(월드에 보이는) Actor 수 반환.
	 * Pool이 없으면 0 반환.
	 */
	int32 GetActiveCount(TSubclassOf<AHellunaEnemyCharacter> EnemyClass) const;

	/**
	 * 특정 EnemyClass의 비활성(대기 중) Actor 수 반환.
	 * Pool이 없으면 0 반환.
	 */
	int32 GetInactiveCount(TSubclassOf<AHellunaEnemyCharacter> EnemyClass) const;

	/** 전체 EnemyClass를 합산한 활성 Actor 총 수 (디버그/로그용) */
	int32 GetTotalActiveCount() const;

	/** 전체 EnemyClass를 합산한 비활성 Actor 총 수 (디버그/로그용) */
	int32 GetTotalInactiveCount() const;

private:
	// =========================================================================
	// 내부 데이터
	// =========================================================================

	/**
	 * EnemyClass → FActorPoolData 매핑.
	 *
	 * [핵심 수정 포인트]
	 * 이전: InactiveActors/ActiveActors를 멤버로 직접 보유 (단일 Pool)
	 * 이후: TMap으로 클래스별 독립 Pool 관리
	 *
	 * 예시:
	 *   PerClassPools[BP_MeleeEnemy]  → FActorPoolData { Inactive:[60개], Active:[] }
	 *   PerClassPools[BP_RangeEnemy]  → FActorPoolData { Inactive:[60개], Active:[] }
	 *
	 * UPROPERTY 필요 이유: TMap 내부의 UPROPERTY TArray가 GC 추적되려면
	 *   외부 컨테이너도 UPROPERTY여야 함.
	 */
	UPROPERTY()
	TMap<TSubclassOf<AHellunaEnemyCharacter>, FActorPoolData> PerClassPools;

	/**
	 * 단일 Actor를 사전 생성한다.
	 * Hidden + TickOff + CollisionOff + ReplicateOff 상태로 생성.
	 * AutoPossessAI = Disabled → Controller 없음 (StateTree 에러 방지).
	 * Controller는 ActivateActor 첫 호출 시 생성.
	 *
	 * @param EnemyClass  생성할 적 블루프린트 클래스
	 * @return            생성된 Actor. 실패 시 nullptr.
	 */
	AHellunaEnemyCharacter* CreatePooledActor(TSubclassOf<AHellunaEnemyCharacter> EnemyClass);

	/** Pool Actor 보관용 숨김 위치 (맵 아래 Z=-50000, 렌더링/물리/Perception 범위 밖) */
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
//   A: 아닙니다. 첫 활성화 시 SpawnDefaultController()가 동기적으로 Controller를 생성하고
//      Possess합니다. Pawn이 이미 올바른 위치에 있고 보이므로 StateTree가 즉시 시작됩니다.
//      두 번째 이후: DeactivateActor가 Controller를 유지하므로 RestartLogic만 호출합니다.
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
//   2. 첫 활성화: SpawnDefaultController()가 호출되었는지 확인
//   3. StateTreeComponent가 Controller에 있는지 확인
//   4. RestartLogic() 후 IsRunning() 확인
//   → 해결: ActivateActor 로그 확인, Controller가 null이면 SpawnDefaultController 호출 확인
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
