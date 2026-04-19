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

	/** [ShipJumpSpreadV1] 슬롯 매니저로부터 받은 인덱스 — GA 의 부채꼴 분산에 사용. */
	UPROPERTY()
	int32 AssignedTopSlotIndex = INDEX_NONE;

	UPROPERTY()
	float PostLandingTimer = 0.f;

	/** 이 인스턴스에 한해 Enter → GA Activate 사이에 적용할 스태거 지연(초). */
	UPROPERTY()
	float StaggerDelay = 0.f;

	/** Enter 이후 경과 시간 (스태거 지연 체크용). */
	UPROPERTY()
	float StaggerElapsed = 0.f;
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

	// ────────────────────────────────────────────────────────────
	// [ShipTopV1.Tune] 점프 튜닝 (GA 인스턴스에 매 발동마다 주입)
	// ────────────────────────────────────────────────────────────

	/** 우주선 최상단 위로 얼마나 더 높이 정점까지 올라갈지 (cm). */
	UPROPERTY(EditAnywhere, Category = "점프 튜닝",
		meta = (DisplayName = "오버슛 높이 (cm)",
			ToolTip = "값이 클수록 더 높게 점프. 우주선 시각적 상단 + 이 값이 정점.",
			ClampMin = "20.0", ClampMax = "2000.0"))
	float OvershootHeight = 400.f;

	/** 착지 후 GA를 유지할 시간 (초). */
	UPROPERTY(EditAnywhere, Category = "점프 튜닝",
		meta = (DisplayName = "착지 후 GA 딜레이 (초)",
			ToolTip = "착지 감지 후 이 시간만큼 대기하다 GA 종료.",
			ClampMin = "0.0", ClampMax = "3.0"))
	float AttackRecoveryDelay = 0.8f;

	/** 착지 감지가 실패할 경우 강제 종료까지의 최대 체공 시간 (초). */
	UPROPERTY(EditAnywhere, Category = "점프 튜닝",
		meta = (DisplayName = "최대 체공 시간 (초)",
			ToolTip = "이 시간 내에 착지 이벤트가 안 뜨면 강제 종료.",
			ClampMin = "1.0", ClampMax = "15.0"))
	float MaxAirborneTime = 6.f;

	/** 타겟 정보가 없을 때 폴백으로 사용할 수평 속도 (cm/s). */
	UPROPERTY(EditAnywhere, Category = "점프 튜닝",
		meta = (DisplayName = "폴백 수평 속도 (cm/s)",
			ClampMin = "100.0", ClampMax = "3000.0"))
	float FallbackHorizontalSpeed = 800.f;

	/** 타겟 정보가 없을 때 폴백으로 사용할 수직 속도 (cm/s). */
	UPROPERTY(EditAnywhere, Category = "점프 튜닝",
		meta = (DisplayName = "폴백 수직 속도 (cm/s)",
			ClampMin = "100.0", ClampMax = "2000.0"))
	float FallbackVerticalSpeed = 900.f;

	// ────────────────────────────────────────────────────────────
	// [ShipJumpStaggerV1] 동시 다발 점프 시 LaunchCharacter burst 분산
	// ────────────────────────────────────────────────────────────

	/** 스태거 최소 지연 (초). EnterState → GA Activate 사이 대기 최소값. */
	UPROPERTY(EditAnywhere, Category = "스태거",
		meta = (DisplayName = "스태거 최소 지연 (초)",
			ToolTip = "동시 점프 몬스터들의 활성화를 시간축으로 분산시키는 최소 지연.",
			ClampMin = "0.0", ClampMax = "2.0"))
	float StaggerMin = 0.0f;

	/** 스태거 최대 지연 (초). 0으로 두면 스태거 비활성화. */
	UPROPERTY(EditAnywhere, Category = "스태거",
		meta = (DisplayName = "스태거 최대 지연 (초)",
			ToolTip = "이 구간 내에서 몬스터별로 무작위 지연 → LaunchCharacter 동시 호출 완화.",
			ClampMin = "0.0", ClampMax = "2.0"))
	float StaggerMax = 0.5f;
};
