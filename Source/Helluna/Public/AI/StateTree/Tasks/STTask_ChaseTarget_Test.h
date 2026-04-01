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

	UPROPERTY()
	int32 DetourDirectionSign = 0;

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

	/** 슬롯 대기 중 이동 목표 (한 번 정하면 도착까지 유지) */
	UPROPERTY()
	FVector WaitingDestination = FVector::ZeroVector;

	/** 대기 목표가 유효한지 */
	UPROPERTY()
	bool bHasWaitingDestination = false;

	/** 연속 슬롯 배정 실패 횟수 (Idle 재배정 누적) */
	UPROPERTY()
	int32 ConsecutiveSlotFailures = 0;

	UPROPERTY()
	FVector SimpleMoveDetourGoal = FVector::ZeroVector;

	UPROPERTY()
	bool bSimpleMoveDetourActive = false;

	UPROPERTY()
	int32 SimpleMoveDetourDirectionSign = 0;
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

	// ═══════════════════════════════════════════════════════════
	// ★ 기능 토글 (원하는 조합으로 ON/OFF 가능)
	// ═══════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, Category = "★ 기능 토글",
		meta = (DisplayName = "[1] EQS 공격 위치 시스템",
			ToolTip = "ON: EQS 쿼리로 최적 공격 위치를 계산해서 이동. EQS 에셋 필요.\nOFF: 타겟에게 직선 MoveToActor.\n슬롯과 중첩 가능 (EQS=플레이어, 슬롯=우주선)."))
	bool bUseEQS = false;

	UPROPERTY(EditAnywhere, Category = "★ 기능 토글",
		meta = (DisplayName = "[2] 슬롯 시스템 (우주선 전용)",
			ToolTip = "ON: 우주선 주변 슬롯 위치로 분산 이동 (근거리 권장).\nOFF: 슬롯 없이 자유 접근 (원거리 권장).\nStuck 감지와 중첩 가능."))
	bool bUseSlotSystem = true;

	UPROPERTY(EditAnywhere, Category = "★ 기능 토글",
		meta = (DisplayName = "[3] Stuck 자동 우회",
			ToolTip = "ON: 위치 변화 없으면 좌/우 자동 우회. 연속 시 강도 증가 (최대 3배).\nOFF: Stuck 감지 비활성화.\n슬롯 모드에서는 Stuck 시 슬롯 재배정."))
	bool bUseStuckDetour = true;

	UPROPERTY(EditAnywhere, Category = "??湲곕뒫 ?좉?",
		meta = (DisplayName = "[4] Simple MoveToActor Test",
			ToolTip = "ON: Ignore slot and EQS logic for spaceship targets. Use MoveToActor directly, and if stuck, sidestep before resuming MoveToActor."))
	bool bUseSimpleMoveToActorTest = false;

	// ═══════════════════════════════════════════════════════════
	// Stuck 감지 설정 (bUseStuckDetour = true 일 때 활성)
	// ═══════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "Stuck 체크 간격 (초)",
			ToolTip = "이 간격마다 위치를 비교. 짧을수록 민감. 권장: 0.3~1.0초",
			ClampMin = "0.1", EditCondition = "bUseStuckDetour"))
	float StuckCheckInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "Stuck 거리 임계치 (cm)",
			ToolTip = "이동 거리가 이 값 이하이면 Stuck 판정. 권장: 20~50cm",
			ClampMin = "1.0", EditCondition = "bUseStuckDetour"))
	float StuckDistThreshold = 30.f;

	UPROPERTY(EditAnywhere, Category = "Stuck 감지",
		meta = (DisplayName = "우회 오프셋 (cm)",
			ToolTip = "Stuck 시 좌/우 우회 거리. 연속 시 x횟수(최대 3배). 권장: 200~500cm",
			ClampMin = "50.0", EditCondition = "bUseStuckDetour"))
	float DetourOffset = 300.f;

	UPROPERTY(EditAnywhere, Category = "Stuck 媛먯?",
		meta = (DisplayName = "Simple Test Detour Distance (cm)",
			ToolTip = "Fixed sidestep distance used by the Simple MoveToActor test mode after a stuck detection.",
			ClampMin = "50.0", EditCondition = "bUseSimpleMoveToActorTest && bUseStuckDetour"))
	float SimpleMoveToActorDetourDistance = 350.f;

	UPROPERTY(EditAnywhere, Category = "Stuck 媛먯?",
		meta = (DisplayName = "Stuck Detour Direction Lock",
			ToolTip = "ON: Reuse the first random left/right detour direction while the actor remains consecutively stuck.\nOFF: Pick a new random left/right direction on each stuck detour.",
			EditCondition = "bUseStuckDetour"))
	bool bUsePersistentDetourDirection = true;

	// ═══════════════════════════════════════════════════════════
	// 우주선 전용 설정
	// ═══════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "우주선 근접 기준 거리 (cm)",
			ToolTip = "표면 거리가 이 값 이하이면 도착 판정. Stuck 감지에서도 이 범위 내는 무시. 권장: 150~400cm",
			ClampMin = "50.0"))
	float AttackRange = 250.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "슬롯 진입 반경 (cm)",
			ToolTip = "우주선 중심에서 이 거리 안이면 슬롯 배정 시도. 밖에서는 NavMesh 경유점 접근. 권장: 600~1200cm",
			ClampMin = "100.0", EditCondition = "bUseSlotSystem"))
	float SlotEngageRadius = 800.f;

	// ═══════════════════════════════════════════════════════════
	// EQS 설정 (bUseEQS = true 일 때 활성)
	// ═══════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "공격 위치 EQS 에셋",
			ToolTip = "EQS 에셋 연결. 타겟 주변 최적 위치 계산. 비워두면 직접 이동으로 폴백.",
			EditCondition = "bUseEQS"))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "EQS 재실행 간격 (초)",
			ToolTip = "EQS 쿼리 재실행 간격. 짧을수록 정확하지만 CPU 부하 증가. 권장: 0.5~2.0초",
			ClampMin = "0.1", EditCondition = "bUseEQS"))
	float EQSInterval = 1.0f;

	// ═══════════════════════════════════════════════════════════
	// 플레이어 전용 설정
	// ═══════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "플레이어 공격 범위 (cm)",
			ToolTip = "플레이어와의 거리가 이 값 이하이면 추적 완료(Succeeded).",
			ClampMin = "50.0"))
	float PlayerAttackRange = 200.f;
};
