/**
 * STTask_ChaseTarget_Test.h
 *
 * ─── 개요 ────────────────────────────────────────────────────
 * 우주선/플레이어 통합 추적 Task (v2).
 *
 * ─── 우주선 이동 설계 (지형에 묻힌 우주선 대응) ──────────────
 *
 *  우주선이 경사면/지형에 반쯤 묻혀 있어서:
 *   - 우주선 중심점이 NavMesh 밖에 있을 수 있음
 *   - MoveToActor(Ship)은 경로 실패할 확률이 높음
 *   - NavMesh는 우주선 주변 지형에만 존재
 *
 *  따라서 3단계 접근 전략을 사용한다:
 *
 *  [Phase 1: 원거리 접근] 거리 > SlotEngageRadius
 *    NavMesh 위 중간 경유점(ComputeNavGoalTowardShip)으로 이동.
 *    우주선 방향으로 점진적 접근. 경유점은 NavMesh에 투영하여
 *    항상 도달 가능한 위치로 보장.
 *
 *  [Phase 2: 근거리 슬롯 배정] 거리 <= SlotEngageRadius, 슬롯 시스템 ON
 *    SpaceShipAttackSlotManager에서 슬롯 배정 → 슬롯 위치로 이동.
 *    슬롯은 BeginPlay에서 NavMesh + 지면 검증된 위치이므로
 *    도달 가능성 보장. 슬롯 도착 시 OccupySlot → Running 유지.
 *
 *  [Phase 2': 근거리 자유 이동] 슬롯 시스템 OFF 또는 슬롯 없음
 *    우주선 방향 NavMesh 경유점으로 계속 접근.
 *    NavMesh 경계 도달 시 pathfinding OFF 직접 이동.
 *
 *  [Phase 3: Stuck 처리]
 *    위치 변화량 기반 Stuck 감지 → 좌/우 랜덤 우회.
 *    연속 Stuck 시 우회 강도 점진 증가 (최대 3배).
 *    슬롯 모드에서는 슬롯 반납 후 재배정.
 *
 * ─── 플레이어 이동 흐름 ──────────────────────────────────────
 *  RepathInterval마다 MoveToActor 재발행 (EQS 옵션)
 *  위치 기반 Stuck 감지 → 랜덤 우회 후 재추적
 *  DistanceToTarget <= PlayerAttackRange → Succeeded
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

	// ─── Stuck 감지 (위치 변화량 기반) ───────────────────────

	/** 마지막 Stuck 체크 시점의 위치 */
	UPROPERTY()
	FVector LastCheckedLocation = FVector::ZeroVector;

	/** Stuck 체크 타이머 */
	UPROPERTY()
	float StuckCheckTimer = 0.f;

	/** 연속 Stuck 횟수 (우회 강도 조절용) */
	UPROPERTY()
	int32 ConsecutiveStuckCount = 0;

	// ─── 슬롯 시스템 (우주선 근거리) ────────────────────────

	/** 현재 배정된 슬롯 인덱스 (-1 = 미배정) */
	UPROPERTY()
	int32 AssignedSlotIndex = -1;

	/** 배정된 슬롯 월드 위치 */
	UPROPERTY()
	FVector AssignedSlotLocation = FVector::ZeroVector;

	/** 슬롯 재배정 시도까지 남은 쿨다운 */
	UPROPERTY()
	float SlotRetryTimer = 0.f;

	/** 슬롯 위치에 도착했는지 */
	UPROPERTY()
	bool bSlotArrived = false;
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

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "재경로 탐색 간격 (초)",
			ToolTip = "플레이어: 이 간격마다 재추적.\n우주선: Repath 주기.",
			ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "도착 허용 반경 (cm)",
			ToolTip = "MoveTo의 AcceptanceRadius입니다.",
			ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	// ─── Stuck 감지 (위치 기반) ────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "Stuck 체크 간격 (초)", ClampMin = "0.1"))
	float StuckCheckInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "Stuck 거리 임계치 (cm)", ClampMin = "1.0"))
	float StuckDistThreshold = 30.f;

	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "우회 오프셋 (cm)", ClampMin = "50.0"))
	float DetourOffset = 300.f;

	// ─── 우주선 전용 설정 ──────────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "우주선 근접 기준 거리 (cm)",
			ToolTip = "표면 거리가 이 값 이하이면 도착으로 판정합니다.\nStuck 감지에서도 이 범위 내이면 Stuck으로 판정하지 않습니다.",
			ClampMin = "50.0"))
	float AttackRange = 250.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "우주선 공격 슬롯 시스템 사용",
			ToolTip = "체크: 우주선 주변 슬롯을 예약해서 이동합니다. (근거리 몬스터 권장)\n해제: 슬롯 없이 우주선 방향으로 자유롭게 이동합니다. (원거리 몬스터 권장)"))
	bool bUseSlotSystem = true;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "슬롯 진입 반경 (cm)",
			ToolTip = "우주선 중심으로부터 이 거리 안에 들어오면 슬롯 배정을 시도합니다.\n밖에서는 NavMesh 경유점으로 접근합니다.",
			ClampMin = "100.0", EditCondition = "bUseSlotSystem"))
	float SlotEngageRadius = 800.f;

	// ─── 플레이어 전용 설정 ────────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "공격 위치 EQS",
			ToolTip = "EQ_HellunaAttackPosition 에셋을 연결하세요.\n비워두면 타겟에게 직접 달려갑니다."))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "EQS 재실행 간격 (초)", ClampMin = "0.1"))
	float EQSInterval = 1.0f;

	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "플레이어 공격 범위 (cm)", ClampMin = "50.0"))
	float PlayerAttackRange = 200.f;
};
