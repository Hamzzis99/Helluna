/**
 * STTask_ChaseTarget_Test.h
 *
 * File: Source/Helluna/Public/AI/StateTree/Tasks/STTask_ChaseTarget_Test.h
 *
 * ─── 개요 ────────────────────────────────────────────────────
 * SlotManager를 제거하고 단순화한 우주선/플레이어 추적 Task.
 *
 * ─── 우주선 이동 방식 (거리 기반) ───────────────────────────
 *  1. MoveToActor(SpaceShip, AttackRange) 로 우주선을 향해 직진
 *  2. DistanceToTarget <= AttackRange 이면 Succeeded 반환 → Attack State 전환
 *  3. 분산은 UCrowdFollowingComponent(RVO/DetourCrowd)에 위임
 *  4. 슬롯 예약/반납/재배정 없음
 *
 * ─── 플레이어 이동 방식 ──────────────────────────────────────
 *  - RepathInterval마다 MoveToActor 재발행 (기존과 동일)
 *  - AttackPositionQuery 설정 시 EQS로 공격 위치 계산 후 이동
 *  - Stuck 감지 시 즉시 재발행
 *
 * @author 김민우
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "STTask_ChaseTarget_Test.generated.h"

class AAIController;
class UEnvQuery;

USTRUCT()
struct FSTTask_ChaseTarget_TestInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AAIController> AIController = nullptr;

	UPROPERTY(EditAnywhere, Category = "Input")
	FHellunaAITargetData TargetData;

	UPROPERTY()
	TWeakObjectPtr<AActor> LastMoveTarget = nullptr;

	UPROPERTY()
	float TimeSinceRepath = 0.f;

	/** EQS 재실행까지 남은 시간 */
	UPROPERTY()
	float TimeUntilNextEQS = 0.f;

	/** Stuck 누적 시간 */
	UPROPERTY()
	float StuckAccumTime = 0.f;
};

USTRUCT(meta = (DisplayName = "Helluna: Chase Target (Test)", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ChaseTarget_Test : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ChaseTarget_TestInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

public:
	// ─── 공통 설정 ─────────────────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "재경로 탐색 간격 (초)",
			ToolTip = "플레이어 타겟을 다시 추적하는 간격입니다.\n우주선 타겟에는 Stuck/Idle 시에만 재발행합니다.",
			ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "설정",
		meta = (DisplayName = "도착 허용 반경 (cm)",
			ToolTip = "MoveToActor의 AcceptanceRadius입니다.",
			ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	// ─── 우주선 전용 설정 ──────────────────────────────────────

	/**
	 * 이 거리 이하면 공격 가능 판정 → Succeeded 반환.
	 * StateTree에서 Attack State로의 전환 조건에 사용.
	 */
	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "공격 범위 (cm)",
			ToolTip = "우주선과의 거리가 이 값 이하이면 Succeeded를 반환하여\nAttack State로 전환합니다.",
			ClampMin = "50.0"))
	float AttackRange = 250.f;

	// ─── 플레이어 전용 설정 ────────────────────────────────────

	/**
	 * 플레이어 타겟 추적 시 사용할 EQS 에셋.
	 * 설정하면 타겟 직접 추적 대신 EQS로 최적 공격 위치를 찾아 이동.
	 * 비워두면 기존 MoveToActor 방식으로 동작.
	 */
	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "공격 위치 EQS",
			ToolTip = "EQ_HellunaAttackPosition 에셋을 연결하세요.\n비워두면 타겟에게 직접 달려갑니다."))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "EQS 재실행 간격 (초)",
			ClampMin = "0.1"))
	float EQSInterval = 1.0f;

	/** 플레이어 공격 범위 (cm). 이 거리 이하면 Succeeded 반환. */
	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "플레이어 공격 범위 (cm)",
			ClampMin = "50.0"))
	float PlayerAttackRange = 200.f;
};
