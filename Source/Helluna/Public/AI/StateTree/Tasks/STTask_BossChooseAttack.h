/**
 * STTask_BossChooseAttack.h
 *
 * StateTree Task: 보스 전용 거리 기반 공격 선택.
 *
 * ─── 동작 ────────────────────────────────────────────────────
 *  - AttackPool의 각 엔트리는 MinRange~MaxRange / Weight / AttackAbility 를 가짐
 *  - 매 쿨다운 해제 시점에 현재 타겟 거리로 후보를 필터링
 *  - 선택 모드 Sequential / Random / WeightedRandom 중 택1
 *  - WeightedRandom은 연속 사용 감쇠(RepeatPenalty)와 연속 상한(MaxSameAttackStreak) 적용
 *  - AttackAbility가 null인 엔트리가 뽑히면 "걷기/대기" 구간 — 공격 스킵 + 쿨다운만 소모
 *
 * ─── 교체 지점 ───────────────────────────────────────────────
 *  기존 STTask_BossAttack을 대체. 사거리 단일 컷오프 대신 엔트리별 구간으로 동작.
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "AI/StateTree/HellunaStateTreeBossTypes.h"
#include "STTask_BossChooseAttack.generated.h"

class AAIController;
class UHellunaEnemyGameplayAbility;

/** 공격 선택 방식 */
UENUM()
enum class EBossChooseAttackMode : uint8
{
	Sequential      UMETA(DisplayName = "순서대로"),
	Random          UMETA(DisplayName = "균등 랜덤"),
	WeightedRandom  UMETA(DisplayName = "가중치 랜덤"),
};

/**
 * 보스의 공격 후보 한 줄.
 *
 *  AttackAbility  : 발동할 GA 클래스. nullptr이면 "이번 턴은 Chase로 복귀" (걷기).
 *  MinRange       : 이 공격을 사용할 최소 거리(cm, 포함).
 *  MaxRange       : 이 공격을 사용할 최대 거리(cm, 포함).
 *  Weight         : WeightedRandom에서의 가중치 (기본 1.0).
 *  Cooldown       : 이 공격을 다시 사용할 때까지의 개별 쿨타임(초).
 *                   0이면 AttackCooldown(글로벌)만 적용. >0이면 이 값이 개별로 추가 적용.
 *  DebugTag       : 로그/디버그용 식별자 (없으면 GA 클래스명 사용).
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FBossAttackEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "공격",
		meta = (DisplayName = "공격 어빌리티 (null=Chase 복귀)"))
	TSubclassOf<UHellunaEnemyGameplayAbility> AttackAbility = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "거리",
		meta = (DisplayName = "최소 거리 (cm)", ClampMin = "0.0"))
	float MinRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "거리",
		meta = (DisplayName = "최대 거리 (cm)", ClampMin = "0.0"))
	float MaxRange = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "가중치",
		meta = (DisplayName = "가중치", ClampMin = "0.0"))
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "쿨타임",
		meta = (DisplayName = "개별 쿨타임 (초, 0=글로벌만)", ClampMin = "0.0"))
	float Cooldown = 0.f;

	/**
	 * [HpRatioFilterV1] 이 엔트리를 후보로 삼는 보스 HP 비율 하한 (0.0=0%, 1.0=100%).
	 *   현재 HP 비율이 [HpRatioMin, HpRatioMax] 안에 있어야 후보에 포함.
	 *   기본 0.0 → HP 무관.
	 *   예) GA_Boss_Time 를 "HP 50% 이하 전용" 으로 만들고 싶으면 Min=0, Max=0.5.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HP 조건",
		meta = (DisplayName = "HP 비율 하한 (0~1)", ClampMin = "0.0", ClampMax = "1.0"))
	float HpRatioMin = 0.f;

	/**
	 * [HpRatioFilterV1] 이 엔트리를 후보로 삼는 보스 HP 비율 상한 (0.0=0%, 1.0=100%).
	 *   기본 1.0 → HP 무관.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HP 조건",
		meta = (DisplayName = "HP 비율 상한 (0~1)", ClampMin = "0.0", ClampMax = "1.0"))
	float HpRatioMax = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "디버그",
		meta = (DisplayName = "디버그 태그"))
	FName DebugTag = NAME_None;

	/**
	 * [ParallelPatternV1] 이 엔트리의 GA 가 실행 중일 때 보스가 다른 공격을 계속 선택/실행할 수 있는지.
	 *   false (기본) = 이 GA 가 active 이면 다른 공격 불가 (기존 동작).
	 *   true          = 이 GA 가 active 이어도 BossChooseAttack 은 다른 엔트리를 계속 선택 가능.
	 *                   "백그라운드 패턴" — 시간 왜곡 존 같이 오래 지속되는 소환 공격에 적합.
	 *                   병행 실행 시에도 엔트리별 cooldown 은 여전히 적용 → 같은 패턴 중복 스폰 방지.
	 *                   주의: 이 플래그를 켜면 해당 GA 는 이동 잠금(LockMovement) 을 하지 않아야 의도대로 동작.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "병행 실행",
		meta = (DisplayName = "병행 실행 허용 (다른 공격 블록하지 않음)"))
	bool bParallelToOtherAttacks = false;
};

USTRUCT()
struct FSTTask_BossChooseAttackInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	/** 기본 타겟 데이터 (Evaluator에서 바인딩) */
	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	/** 패턴 데이터 (Evaluator에서 바인딩 — bPatternActive 확인용) */
	UPROPERTY(EditAnywhere, Category = "Input")
	FBossPatternData PatternData;

	UPROPERTY()
	float CooldownRemaining = 0.f;

	/** Sequential 모드의 다음 인덱스 (필터링된 후보 중에서 순환) */
	UPROPERTY()
	int32 NextSequentialIndex = 0;

	/** 가장 최근에 선택된 풀 인덱스 (-1 = 없음) */
	UPROPERTY()
	int32 LastSelectedIndex = -1;

	/** 같은 풀 인덱스를 연속으로 선택한 횟수 */
	UPROPERTY()
	int32 SameAttackStreak = 0;

	/**
	 * 엔트리별 개별 쿨다운 남은 시간. AttackPool 인덱스와 1:1.
	 * 0 이하 = 사용 가능. 양수 = 대기 필요.
	 * AttackPool 크기가 변하면 Tick 시작에서 동기화됨.
	 */
	UPROPERTY()
	TArray<float> PerEntryCooldowns;

	/** 최초 Enter 감지용 (InitialDelay를 첫 진입에만 강제 적용) */
	UPROPERTY()
	bool bFirstEnter = true;
};

USTRUCT(meta = (DisplayName = "Helluna: Boss Choose Attack", Category = "Helluna|AI|Boss"))
struct HELLUNA_API FSTTask_BossChooseAttack : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_BossChooseAttackInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	/** 보스 공격 후보 배열. 엔트리별 거리/가중치로 필터+선택. */
	UPROPERTY(EditAnywhere, Category = "공격 풀",
		meta = (DisplayName = "공격 풀", TitleProperty = "DebugTag"))
	TArray<FBossAttackEntry> AttackPool;

	UPROPERTY(EditAnywhere, Category = "공격 풀",
		meta = (DisplayName = "선택 방식"))
	EBossChooseAttackMode SelectionMode = EBossChooseAttackMode::WeightedRandom;

	/**
	 * 글로벌 최소 쿨다운(초). 어떤 공격이든 발동 직후 이 시간 동안은 다른 공격 금지 (스팸 방지).
	 * 각 엔트리의 Cooldown은 이 값에 "추가"되는 개별 제약.
	 *  - 엔트리.Cooldown = 0  → 이 글로벌 값만 적용
	 *  - 엔트리.Cooldown > 0  → 글로벌 + 개별 (겹쳐서 더 길게 대기)
	 */
	UPROPERTY(EditAnywhere, Category = "타이밍",
		meta = (DisplayName = "글로벌 최소 쿨타임 (초)", ClampMin = "0.0"))
	float AttackCooldown = 0.3f;

	UPROPERTY(EditAnywhere, Category = "타이밍",
		meta = (DisplayName = "첫 공격 딜레이 (초)", ClampMin = "0.0"))
	float InitialDelay = 0.3f;

	UPROPERTY(EditAnywhere, Category = "회전",
		meta = (DisplayName = "대기 중 회전 속도", ClampMin = "0.0"))
	float RotationSpeed = 30.f;

	/**
	 * 연속 선택 상한. 같은 엔트리가 여기까지 연속 뽑히면 다음 선택에서 제외.
	 * WeightedRandom / Random 모드에 적용 (Sequential은 영향 없음).
	 */
	UPROPERTY(EditAnywhere, Category = "가중치",
		meta = (DisplayName = "연속 상한", ClampMin = "1", ClampMax = "10"))
	int32 MaxSameAttackStreak = 2;

	/**
	 * 연속 사용 시 자신의 가중치에 곱할 감쇠 (0..1).
	 * WeightedRandom에서만 적용. 1이면 감쇠 없음, 0.3이면 연속 사용 시 가중치 30%로.
	 */
	UPROPERTY(EditAnywhere, Category = "가중치",
		meta = (DisplayName = "연속 사용 감쇠", ClampMin = "0.0", ClampMax = "1.0"))
	float RepeatPenalty = 0.35f;

	/**
	 * 현재 거리에서 사용 가능한 공격이 하나도 없을 때:
	 *  true  = Task를 Failed로 종료해 StateTree 전환을 트리거 (보통 Chase로 복귀)
	 *  false = 현재 상태를 유지하고 대기 (기본, 기존 STTask_BossAttack과 동일 동작)
	 */
	UPROPERTY(EditAnywhere, Category = "실패 처리",
		meta = (DisplayName = "후보 없으면 Task 실패"))
	bool bEndTaskIfNoValidCandidate = true;

	/**
	 * "걷기" 슬롯 (AttackAbility=null) 선택 시의 동작.
	 *  true  = Task가 Succeeded 반환 → OnStateCompleted로 Chase 복귀 (권장)
	 *  false = 공격 없이 글로벌 쿨다운만 소모하고 Attack 상태 유지 (조용히 대기)
	 */
	UPROPERTY(EditAnywhere, Category = "걷기 슬롯",
		meta = (DisplayName = "걷기 선택 시 Chase로 전환"))
	bool bWalkExitsToChase = true;

	/**
	 * 걷기 전환 직후 재진입 방지용 글로벌 쿨다운(초).
	 * Walk이 선택된 후 Chase가 몇 초간 움직일 수 있도록 글로벌 쿨다운을 길게 설정.
	 *  - 너무 짧으면 Chase→Attack→Walk→Chase 무한 루프
	 *  - 권장 1.5~3.0초
	 */
	UPROPERTY(EditAnywhere, Category = "걷기 슬롯",
		meta = (DisplayName = "걷기 후 재공격 대기 (초)", ClampMin = "0.0"))
	float WalkReentryDelay = 2.0f;

	/**
	 * [BossWalkPriorityV1] GA 후보가 있으면 GA 우선, Walk 는 폴백.
	 *
	 *  true  (기본): 현재 거리에서 GA 슬롯 후보가 1개 이상이면 그 중에서만 선택.
	 *                Walk 슬롯(GA=null) 은 GA 후보가 0 개일 때만 폴백으로 사용.
	 *                → 사용자 의도 "GA 거리 안 닿을 때만 걷기" 정확히 구현.
	 *  false : GA 와 Walk 슬롯을 같은 풀에서 가중치/랜덤으로 섞어 선택 (이전 동작).
	 *
	 *  AttackPool 의 슬롯들이 거리 범위가 겹치게 셋업된 경우(예: GA 0~500, Walk 400~2000)
	 *  false 로 두면 400~500 구간에서 Walk 가 무작위 선택되어 핑퐁 발생.
	 */
	UPROPERTY(EditAnywhere, Category = "걷기 슬롯",
		meta = (DisplayName = "GA 후보 우선 (Walk 는 폴백)",
			ToolTip = "true: GA 후보 있으면 GA 만 선택, Walk 는 GA 0개일 때 폴백."))
	bool bPreferAbilityOverWalk = true;
};
