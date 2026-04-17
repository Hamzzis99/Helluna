/**
 * STTask_ShipJump.h
 *
 * [ShipTopV1] StateTree Task — 우주선 상단 점프 어택 분기.
 *
 * 동작:
 *  1. EnterState: 타겟이 우주선인지 확인 + SlotManager에 상단 슬롯 예약 시도.
 *     - 실패 → Failed 반환 (상위 State의 OnFailed 트랜지션으로 일반 공격 분기 fallback).
 *     - 성공 → 우주선 방향 스냅 + Ship Jump GA 발동.
 *  2. Tick: GA가 끝나고(착지 완료) PostLandingHoldTime 경과 후 Succeeded.
 *     - 착지 이후엔 사용자가 별도 공격 GA/상태를 추가해 상단 공격 수행 예정.
 *  3. ExitState: bReleaseSlotOnExit가 true면 상단 슬롯 반납.
 *
 * 직렬화 주의(feedback_struct_serialization):
 *  - 이 파일은 완전 신규 USTRUCT. 기존 자산엔 영향 없음.
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STTask_ShipJump.generated.h"

class AAIController;
class UHellunaEnemyGameplayAbility;

USTRUCT()
struct FSTTask_ShipJumpInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	UPROPERTY()
	bool bReservedTopSlot = false;

	UPROPERTY()
	bool bActivatedGA = false;

	UPROPERTY()
	float PostLandingTimer = 0.f;
};

USTRUCT(meta = (DisplayName = "Helluna: Ship Jump", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ShipJump : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ShipJumpInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	/** 점프 GA 클래스 (기본값은 EnemyGameplayAbility_ShipJump). */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "점프 어빌리티 클래스",
			ToolTip = "우주선 상단으로 점프하는 GA 클래스. 비워두면 Task가 실패한다."))
	TSubclassOf<UHellunaEnemyGameplayAbility> JumpAbilityClass;

	/** 착지 후 Task를 유지할 시간 (초). 그 후 Succeeded 반환. */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "착지 후 유지 시간 (초)",
			ClampMin = "0.0", ClampMax = "3.0"))
	float PostLandingHoldTime = 0.3f;

	/** Task 종료 시 상단 슬롯을 반납할지 여부. */
	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "종료 시 슬롯 반납",
			ToolTip = "체크 해제 시 슬롯을 유지 → 다음 Task(상단 공격 등)가 같은 슬롯을 이어받음."))
	bool bReleaseSlotOnExit = true;
};
