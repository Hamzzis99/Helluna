/**
 * STTask_ChaseSpaceShip.cpp
 *
 * 우주선 추적 전용 StateTree Task.
 * 에디터에서 이동 전략(Direct / RandomOffset / EQS)을 토글할 수 있다.
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_ChaseSpaceShip.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "AITypes.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"

namespace ChaseSpaceShipHelpers
{

void IssueMoveToLocation(AAIController* AIController, const FVector& Goal, float AcceptanceRadius)
{
	if (!AIController) return;

	FAIMoveRequest Req;
	Req.SetGoalLocation(Goal);
	Req.SetAcceptanceRadius(AcceptanceRadius);
	Req.SetReachTestIncludesAgentRadius(false);
	Req.SetReachTestIncludesGoalRadius(false);
	Req.SetUsePathfinding(true);
	Req.SetAllowPartialPath(true);
	Req.SetCanStrafe(false);
	AIController->MoveTo(Req);
}

/**
 * MoveToActor(SpaceShip) 대체: 우주선 Root가 메시 내부/상공이면
 * NavMesh 도달 불가 → 멈춤 발생. NavMesh 투영 위치로 MoveToLocation 한다.
 */
void IssueMoveToActorNavProjected(AAIController* AIController, AActor* TargetActor, float AcceptanceRadius)
{
	if (!AIController || !TargetActor) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIController->GetWorld());
	if (NavSys)
	{
		FNavLocation NavLoc;
		if (NavSys->ProjectPointToNavigation(TargetActor->GetActorLocation(), NavLoc, FVector(500.f, 500.f, 1000.f)))
		{
			IssueMoveToLocation(AIController, NavLoc.Location, AcceptanceRadius);
			return;
		}
	}
	// NavMesh 투영 실패 시 AllowPartialPath로 최대한 접근
	IssueMoveToLocation(AIController, TargetActor->GetActorLocation(), AcceptanceRadius);
}

void RunEQS(
	UEnvQuery* Query,
	AAIController* AIController,
	float AcceptanceRadius,
	AActor* FallbackTarget)
{
	if (!Query || !AIController) return;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return;

	UWorld* World = Pawn->GetWorld();
	if (!World) return;

	UEnvQueryManager* EQSManager = UEnvQueryManager::GetCurrent(World);
	if (!EQSManager) return;

	TWeakObjectPtr<AAIController> WeakController = AIController;
	TWeakObjectPtr<AActor> WeakFallback = FallbackTarget;

	FEnvQueryRequest Request(Query, Pawn);
	Request.Execute(EEnvQueryRunMode::SingleResult,
		FQueryFinishedSignature::CreateLambda(
			[WeakController, WeakFallback, AcceptanceRadius](TSharedPtr<FEnvQueryResult> Result)
			{
				AAIController* Ctrl = WeakController.Get();
				if (!Ctrl) return;

				if (Result.IsValid() && !Result->IsAborted() && Result->Items.Num() > 0)
				{
					IssueMoveToLocation(Ctrl, Result->GetItemAsLocation(0), AcceptanceRadius);
				}
				else if (AActor* Fallback = WeakFallback.Get())
				{
					IssueMoveToActorNavProjected(Ctrl, Fallback, AcceptanceRadius);
				}
			}));
}

} // namespace ChaseSpaceShipHelpers
using namespace ChaseSpaceShipHelpers;

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseSpaceShip::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController)
	{
		UE_LOG(LogTemp, Error, TEXT("[ChaseSpaceShip] EnterState: AIController가 nullptr!"));
		return EStateTreeRunStatus::Failed;
	}

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.HasValidTarget())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[ChaseSpaceShip] EnterState: TargetActor가 nullptr — 타겟 없음"));
		InstanceData.TimeSinceRepath   = 0.f;
		InstanceData.TimeUntilNextEQS  = 0.f;
		InstanceData.StuckCheckTimer   = 0.f;
		InstanceData.ConsecutiveStuckCount = 0;
		InstanceData.LastCheckedLocation   = FVector::ZeroVector;
		return EStateTreeRunStatus::Running;
	}

	AActor* TargetActor = TargetData.TargetActor.Get();

	InstanceData.TimeSinceRepath       = 0.f;
	InstanceData.TimeUntilNextEQS      = 0.f;
	InstanceData.StuckCheckTimer       = 0.f;
	InstanceData.ConsecutiveStuckCount = 0;

	if (APawn* Pawn = AIController->GetPawn())
		InstanceData.LastCheckedLocation = Pawn->GetActorLocation();

	switch (Strategy)
	{
	case EChaseShipStrategy::EQS:
		if (AttackPositionQuery)
		{
			RunEQS(AttackPositionQuery, AIController, AcceptanceRadius, TargetActor);
			break;
		}
		// EQS 에셋 미설정 → Direct로 폴백
		[[fallthrough]];

	case EChaseShipStrategy::Direct:
	default:
		IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
		break;

	case EChaseShipStrategy::RandomOffset:
		IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
		break;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[ChaseSpaceShip] EnterState: Target=%s | Strategy=%d | Dist=%.1f"),
		*GetNameSafe(TargetActor), (int)Strategy, TargetData.DistanceToTarget);

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseSpaceShip::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.HasValidTarget()) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AActor* TargetActor = TargetData.TargetActor.Get();

	InstanceData.TimeSinceRepath  += DeltaTime;
	InstanceData.TimeUntilNextEQS -= DeltaTime;

	// ── Stuck 감지 ──────────────────────────────────────────────
	InstanceData.StuckCheckTimer += DeltaTime;
	bool bStuck = false;
	if (InstanceData.StuckCheckTimer >= StuckCheckInterval)
	{
		InstanceData.StuckCheckTimer = 0.f;
		const FVector CurrentLoc = Pawn->GetActorLocation();
		const float MovedDist = FVector::Dist(CurrentLoc, InstanceData.LastCheckedLocation);

		if (MovedDist < StuckDistThreshold && TargetData.DistanceToTarget > AcceptanceRadius + 100.f)
		{
			InstanceData.ConsecutiveStuckCount++;
			bStuck = (InstanceData.ConsecutiveStuckCount >= 2);
		}
		else
		{
			InstanceData.ConsecutiveStuckCount = 0;
		}
		InstanceData.LastCheckedLocation = CurrentLoc;
	}

	// ── 재경로 판정 ─────────────────────────────────────────────
	const bool bRepathDue = InstanceData.TimeSinceRepath >= RepathInterval;

	if (!bRepathDue && !bStuck)
		return EStateTreeRunStatus::Running;

	InstanceData.TimeSinceRepath = 0.f;

	switch (Strategy)
	{
	case EChaseShipStrategy::Direct:
	default:
		IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
		break;

	case EChaseShipStrategy::RandomOffset:
	{
		if (bStuck)
		{
			// Stuck 시 좌/우 랜덤 우회
			const FVector PawnLoc  = Pawn->GetActorLocation();
			const FVector ShipLoc  = TargetActor->GetActorLocation();
			const FVector Dir      = (ShipLoc - PawnLoc).GetSafeNormal();
			const FVector Perp     = FVector(-Dir.Y, Dir.X, 0.f);
			const float   Side     = (FMath::RandBool() ? 1.f : -1.f);
			const FVector RawGoal  = PawnLoc + Dir * 300.f + Perp * DetourOffset * Side;

			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIController->GetWorld());
			FNavLocation NavGoal;
			FVector FinalGoal = RawGoal;
			if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(200.f, 200.f, 500.f)))
				FinalGoal = NavGoal.Location;

			IssueMoveToLocation(AIController, FinalGoal, AcceptanceRadius);
			InstanceData.ConsecutiveStuckCount = 0;

			UE_LOG(LogTemp, Verbose, TEXT("[ChaseSpaceShip] Stuck 우회: Side=%.0f, Goal=%s"), Side, *FinalGoal.ToString());
		}
		else
		{
			IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
		}
		break;
	}

	case EChaseShipStrategy::EQS:
		if (AttackPositionQuery && InstanceData.TimeUntilNextEQS <= 0.f)
		{
			RunEQS(AttackPositionQuery, AIController, AcceptanceRadius, TargetActor);
			InstanceData.TimeUntilNextEQS = EQSInterval;
		}
		else
		{
			IssueMoveToActorNavProjected(AIController, TargetActor, AcceptanceRadius);
		}
		break;
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_ChaseSpaceShip::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (AAIController* AIController = InstanceData.AIController)
		AIController->StopMovement();
}
