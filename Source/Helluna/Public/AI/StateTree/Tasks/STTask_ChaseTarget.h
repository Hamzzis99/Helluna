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

	UPROPERTY(EditAnywhere, Category = "Common",
		meta = (DisplayName = "Repath Interval",
			ToolTip = "MoveTo Repath Interval (sec). \nPlayer: Re-Chase Interval. \nShip: Waypoint Refresh Interval.",
			ClampMin = "0.1"))
	float RepathInterval = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Common",
		meta = (DisplayName = "Acceptance Radius",
			ToolTip = "MoveTo Acceptance Radius (cm).",
			ClampMin = "10.0"))
	float AcceptanceRadius = 50.f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "Ship",
		meta = (DisplayName = "Spread Phase Radius",
			ToolTip = "Rush -> Spread Phase Switching Distance (cm). \nMust be larger than StandoffRadius.",
			ClampMin = "100.0"))
	float SpreadPhaseRadius = 1200.f;

	UPROPERTY(EditAnywhere, Category = "Ship",
		meta = (DisplayName = "Standoff Radius",
			ToolTip = "Distance from ship center where monsters hold position (cm). \nThis is where they stand and attack.",
			ClampMin = "50.0"))
	float StandoffRadius = 350.f;

	UPROPERTY(EditAnywhere, Category = "Ship",
		meta = (DisplayName = "Rush Waypoint Step",
			ToolTip = "Rush Phase Maximum Distance Per Waypoint (cm).",
			ClampMin = "100.0"))
	float RushWaypointStep = 800.f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "Stuck",
		meta = (DisplayName = "Stuck Check Interval",
			ToolTip = "Monster Stuck Check Interval (sec).",
			ClampMin = "0.1"))
	float StuckCheckInterval = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Stuck",
		meta = (DisplayName = "Stuck Distance Threshold",
			ToolTip = "Stuck if moved less than this in one check interval (cm).",
			ClampMin = "1.0"))
	float StuckDistThreshold = 50.f;

	UPROPERTY(EditAnywhere, Category = "Stuck",
		meta = (DisplayName = "Stuck Angle Rotation",
			ToolTip = "Stuck Detected -> Rotate Assigned Angle (degrees).",
			ClampMin = "10.0"))
	float AngleRotationOnStuck = 90.f;

	UPROPERTY(EditAnywhere, Category = "Stuck",
		meta = (DisplayName = "Direct Move Stuck Threshold",
			ToolTip = "This many consecutive stucks -> disable pathfinding and walk directly. \nNavMesh boundary Stuck Fix.",
			ClampMin = "1"))
	int32 DirectMoveStuckThreshold = 2;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "Attack Position EQS",
			ToolTip = "EQS Asset for Player Attack Position. Leave empty for direct MoveToActor."))
	TObjectPtr<UEnvQuery> AttackPositionQuery = nullptr;

	UPROPERTY(EditAnywhere, Category = "EQS",
		meta = (DisplayName = "EQS Interval",
			ToolTip = "EQS Repeat Interval (sec).",
			ClampMin = "0.1"))
	float EQSInterval = 1.0f;

	// ==========================================================

	UPROPERTY(EditAnywhere, Category = "Player",
		meta = (DisplayName = "Player Attack Range",
			ToolTip = "Distance at which player chase completes (cm).",
			ClampMin = "50.0"))
	float PlayerAttackRange = 200.f;
};
