/**
 * STTask_ChaseTarget.h
 *
 * StateTree Task: 2-Phase Chase (RUSH + SPREAD)
 *
 * [Phase 1: RUSH]
 *   Each monster gets a unique angle (0, 60, 120...).
 *   Rush goal = point at assigned angle, SpreadPhaseRadius away from ship.
 *   Forces each monster to take a different NavMesh path.
 *
 * [Phase 2: SPREAD]
 *   Move to assigned angle position at StandoffRadius from ship.
 *   On arrival, face ship and hold (Running, attack state handles transition).
 *
 * [Stuck -> Direct Move]
 *   After N consecutive stucks, disable pathfinding and walk directly
 *   toward the spread goal. CMC Walking mode still handles ground/collision.
 *
 * [Player]
 *   MoveToActor with RepathInterval. Optional EQS.
 *
 * @author
 */

#pragma once

#include "CoreMinimal.h"
#include "Tasks/StateTreeAITask.h"
#include "AI/StateTree/HellunaStateTreeTypes.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "STTask_ChaseTarget.generated.h"

class AAIController;
class UEnvQuery;

UENUM()
enum class EChasePhase : uint8
{
	Rush,
	Spread,
};

USTRUCT()
struct FSTTask_ChaseTargetInstanceData
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

	UPROPERTY()
	float TimeUntilNextEQS = 0.f;

	// --- Ship chase ---

	UPROPERTY()
	EChasePhase Phase = EChasePhase::Rush;

	UPROPERTY()
	float AssignedAngleDeg = 0.f;

	UPROPERTY()
	FVector LastCheckedLocation = FVector::ZeroVector;

	UPROPERTY()
	float StuckCheckTimer = 0.f;

	UPROPERTY()
	int32 ConsecutiveStuckCount = 0;

	UPROPERTY()
	bool bSpreadArrived = false;

	/** pathfinding OFF direct movement mode */
	UPROPERTY()
	bool bDirectMoveMode = false;

	UPROPERTY()
	float DiagTimer = 0.f;

	// --- Player chase ---

	UPROPERTY()
	int32 PlayerStuckCount = 0;

	UPROPERTY()
	float PlayerStuckTimer = 0.f;

	UPROPERTY()
	FVector PlayerLastCheckedLocation = FVector::ZeroVector;
};

USTRUCT(meta = (DisplayName = "Helluna: Chase Target", Category = "Helluna|AI"))
struct HELLUNA_API FSTTask_ChaseTarget : public FStateTreeAITaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSTTask_ChaseTargetInstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;

	EStateTreeRunStatus TickShipChase(FStateTreeExecutionContext& Context,
		FInstanceDataType& Data, AAIController* AIC, APawn* Pawn,
		AActor* Ship, float DeltaTime) const;

	EStateTreeRunStatus TickPlayerChase(FInstanceDataType& Data,
		AAIController* AIC, APawn* Pawn, AActor* Target, float DeltaTime) const;

public:

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "경로 재탐색 주기",
			ToolTip = "MoveTo 경로 재탐색 주기 (초).\n플레이어: 재추적 주기.\n우주선: 웨이포인트 갱신 주기.",
			ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "공통",
		meta = (DisplayName = "도착 판정 반경",
			ToolTip = "MoveTo 도착 판정 반경 (cm).",
			ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "산개 전환 거리",
			ToolTip = "돌진 -> 산개 페이즈 전환 거리 (cm).\n대기 반경보다 커야 합니다.",
			ClampMin = "100.0"))
	float SpreadPhaseRadius = 1200.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "대기 반경",
			ToolTip = "우주선 중심에서 몬스터가 멈춰서 공격하는 거리 (cm).",
			ClampMin = "50.0"))
	float StandoffRadius = 350.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "돌진 웨이포인트 간격",
			ToolTip = "돌진 페이즈에서 한 웨이포인트당 최대 이동 거리 (cm).",
			ClampMin = "100.0"))
	float RushWaypointStep = 800.f;

	UPROPERTY(EditAnywhere, Category = "우주선",
		meta = (DisplayName = "재추격 여유 거리",
			ToolTip = "도착 후 우주선이 대기 반경 + 이 값 이상 멀어지면 다시 추격 (cm).",
			ClampMin = "50.0"))
	float SpreadReEngageMargin = 300.f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "끼임 감지 주기",
			ToolTip = "몬스터 끼임 감지 주기 (초).",
			ClampMin = "0.1"))
	float StuckCheckInterval = 0.4f;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "끼임 판정 거리",
			ToolTip = "감지 주기 동안 이 거리 미만으로 이동하면 끼임 판정 (cm).",
			ClampMin = "1.0"))
	float StuckDistThreshold = 50.f;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "끼임 시 각도 회전량",
			ToolTip = "끼임 감지 시 할당 각도를 이만큼 회전 (도).",
			ClampMin = "10.0"))
	float AngleRotationOnStuck = 90.f;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "직선이동 전환 횟수",
			ToolTip = "이 횟수만큼 연속 끼이면 길찾기를 끄고 직선 이동으로 전환.\nNavMesh 경계 끼임 해결용.",
			ClampMin = "1"))
	int32 DirectMoveStuckThreshold = 2;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "강제 도착 전환 횟수",
			ToolTip = "직선이동 모드에서 이 횟수만큼 끼이면 강제 도착 처리.\n우주선 근처 충돌체에 막혔을 때 사용.",
			ClampMin = "3"))
	int32 DirectModeForceArriveThreshold = 8;

	UPROPERTY(EditAnywhere, Category = "끼임 감지",
		meta = (DisplayName = "경로실패 시 즉시 직선이동",
			ToolTip = "ON: MoveTo 경로 실패 시 즉시 직선이동 전환.\nOFF: 끼임 감지만으로 전환."))
	bool bInstantDirectModeOnPathFail = false;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "플레이어",
		meta = (DisplayName = "플레이어 추격 직선이동 횟수",
			ToolTip = "플레이어 추격 중 이 횟수만큼 연속 끼이면 직선이동으로 전환.",
			ClampMin = "2"))
	int32 PlayerDirectMoveThreshold = 4;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "공격 위치 EQS",
			ToolTip = "플레이어 공격 위치용 EQS 에셋. 비우면 직접 MoveToActor 사용."))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "EQS 실행 주기",
			ToolTip = "EQS 반복 실행 주기 (초).",
			ClampMin = "0.1"))
	float EQSInterval = 1.0f;
};
