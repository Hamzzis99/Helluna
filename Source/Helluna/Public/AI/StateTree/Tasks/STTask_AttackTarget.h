/**
 * STTask_AttackTarget.h
 *
 * StateTree Task: 타겟 공격 실행
 *
 * ─── 실제 공격 주기 ───────────────────────────────────────────
 *  AttackCooldown 소진
 *   → GA 활성화 → 몽타주 재생 (몽타주 길이)
 *   → AttackRecoveryDelay 대기 (GA 내부)
 *   → GA 종료 → 다음 프레임에 AttackCooldown 재시작
 *
 *  체감 공격 주기 = 몽타주 길이 + AttackRecoveryDelay + AttackCooldown
 *
 *  AttackCooldown은 "GA가 끝난 후부터 다음 공격 시도까지의 대기"이므로
 *  실제 공격 빈도를 줄이려면 AttackCooldown을 줄이는 것보다
 *  AttackRecoveryDelay를 조정하는 것이 더 직관적이다.
 *
 * ─── RotationSpeed ───────────────────────────────────────────
 *  GA가 비활성(AttackCooldown 대기 중)일 때 타겟 방향으로
 *  RInterpTo로 서서히 회전하는 속도.
 *  GA 활성 중(몽타주 재생 / AttackRecoveryDelay)에는 이동이 잠겨있어
 *  이 회전은 동작하지 않는다.
 * ─────────────────────────────────────────────────────────────
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Attack.h"
#include "STTask_AttackTarget.generated.h"

class AAIController;
class APawn;

USTRUCT()
struct FSTTask_AttackTargetInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	// 런타임 쿨다운 상태 (에디터 노출 불필요)
	UPROPERTY()
	float CooldownRemaining = 0.f;
};

USTRUCT(meta = (DisplayName = "Helluna: Attack Target", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_AttackTarget : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_AttackTargetInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	/**
	 * 발동할 공격 GA 클래스.
	 * 몬스터 종류마다 다른 GA를 사용할 수 있도록 에디터에서 선택 가능.
	 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 어빌리티 클래스",
			ToolTip = "이 Task가 발동할 공격 GA를 선택합니다.\n몬스터 종류마다 다른 GA를 지정할 수 있습니다."))
	TSubclassOf<UEnemyGameplayAbility_Attack> AttackAbilityClass;

	/**
	 * GA 종료 후 다음 공격까지 대기 시간 (초).
	 * GA EndAbility 시점부터 정확하게 카운트 시작.
	 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "공격 쿨다운 (초)",
			ToolTip = "공격이 완전히 끝난 후 다음 공격까지 기다리는 시간입니다.\n체감 공격 주기 = 공격 모션 길이 + 경직 시간(GA 설정) + 이 값",
			ClampMin = "0.0"))
	float AttackCooldown = 1.5f;

	/**
	 * 쿨다운 대기 중 타겟 방향으로 회전하는 속도.
	 * GA 활성 중(몽타주 + 경직)에는 동작하지 않음.
	 */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "대기 중 회전 속도",
			ToolTip = "공격 쿨다운 대기 중에 타겟을 바라보는 회전 속도입니다.\n값이 클수록 빠르게 회전합니다. (RInterpTo Speed)",
			ClampMin = "0.0"))
	float RotationSpeed = 10.f;
};
