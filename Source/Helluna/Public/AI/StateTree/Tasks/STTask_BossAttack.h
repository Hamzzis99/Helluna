/**
 * STTask_BossAttack.h
 *
 * StateTree Task: 보스 전용 일반 공격.
 *
 * ─── 동작 ────────────────────────────────────────────────────
 *  - 플레이어가 AttackRange 안에 들어오면 공격 GA 발동
 *  - AttackAbilities 배열에서 순서대로 또는 랜덤으로 선택
 *  - 한 공격이 끝날 때마다 Evaluator의 리타겟을 존중
 *  - GA 활성 중이면 대기, 끝나면 쿨다운 후 다음 공격
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "AI/StateTree/HellunaStateTreeBossTypes.h"
#include "STTask_BossAttack.generated.h"

class AAIController;
class UHellunaEnemyGameplayAbility;

/** 공격 선택 방식 */
UENUM()
enum class EBossAttackSelection : uint8
{
	Sequential  UMETA(DisplayName = "순서대로"),
	Random      UMETA(DisplayName = "랜덤"),
};

USTRUCT()
struct FSTTask_BossAttackInstanceData
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

	UPROPERTY()
	int32 NextAttackIndex = 0;
};

USTRUCT(meta = (DisplayName = "Helluna: Boss Attack", Category = "Helluna|AI|Boss"))
struct HELLUNA_API FSTTask_BossAttack : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_BossAttackInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	/**
	 * 보스 일반 공격 GA 목록 (3~4종).
	 * 이 배열에서 순서대로 또는 랜덤으로 선택하여 발동한다.
	 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 어빌리티 목록"))
	TArray<TSubclassOf<UHellunaEnemyGameplayAbility>> AttackAbilities;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 선택 방식"))
	EBossAttackSelection SelectionMode = EBossAttackSelection::Sequential;

	/** 플레이어가 이 범위(cm) 안에 있어야 공격 시도 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 범위 (cm)", ClampMin = "50.0"))
	float AttackRange = 300.f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 쿨다운 (초)", ClampMin = "0.0"))
	float AttackCooldown = 2.0f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "첫 공격 딜레이 (초)", ClampMin = "0.0"))
	float InitialDelay = 0.3f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "대기 중 회전 속도", ClampMin = "0.0"))
	float RotationSpeed = 30.f;
};
