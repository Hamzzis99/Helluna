/**
 * STTask_ChaseTarget.cpp
 *
 * [방안 C 적용]
 * 우주선: 고정 오브젝트이므로 타겟 변경 시에만 MoveTo 재발행.
 *         이후 Tick에서는 RVO(CrowdFollowingComponent)에 회피 위임.
 * 플레이어: 움직이는 대상이므로 RepathInterval 재발행 유지.
 * Stuck 판단: Idle + 저속 + 거리 3중 조건으로 RVO 조정 중 오탐 방지.
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_ChaseTarget.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "AITypes.h"

#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

// ============================================================================
// 우주선 주변 "도달 가능한 점" 하나 뽑기
// ============================================================================
static bool FindShipApproachPoint(
	UWorld* World,
	const FVector& ShipCenter,
	float Radius,
	FVector& OutPoint
)
{
	if (!World)
		return false;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys)
		return false;

	FNavLocation NavLoc;
	if (!NavSys->GetRandomReachablePointInRadius(ShipCenter, Radius, NavLoc))
		return false;

	OutPoint = NavLoc.Location;
	return true;
}

// ============================================================================
// MoveToLocation (우주선 분산 접근용)
// ============================================================================
static void IssueMoveToLocation(
	AAIController* AIController,
	const FVector& Goal,
	float AcceptanceRadius
)
{
	if (!AIController)
		return;

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

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController)
	{
		UE_LOG(LogTemp, Warning, TEXT("STTask_ChaseTarget: AIController is null"));
		return EStateTreeRunStatus::Failed;
	}

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("STTask_ChaseTarget: TargetData invalid"));
		return EStateTreeRunStatus::Failed;
	}

	AActor* TargetActor = TargetData.TargetActor.Get();
	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	if (bIsSpaceShip)
	{
		const float ApproachRadius = 800.f;
		const float LocAcceptance  = 100.f;

		FVector Goal;
		if (FindShipApproachPoint(AIController->GetWorld(), TargetActor->GetActorLocation(), ApproachRadius, Goal))
			IssueMoveToLocation(AIController, Goal, LocAcceptance);
		else
			AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	}
	else
	{
		AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	}

	InstanceData.LastMoveTarget  = TargetData.TargetActor;
	InstanceData.TimeSinceRepath = 0.f;

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController)
		return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.IsValid())
		return EStateTreeRunStatus::Failed;

	// 공격 중이면 이동 정지
	if (APawn* Pawn = AIController->GetPawn())
	{
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
				if (AttackingTag.IsValid() && ASC->HasMatchingGameplayTag(AttackingTag))
				{
					AIController->StopMovement();
					return EStateTreeRunStatus::Running;
				}
			}
		}
	}

	InstanceData.TimeSinceRepath += DeltaTime;

	const bool bTargetChanged = InstanceData.LastMoveTarget != TargetData.TargetActor;
	const bool bIsSpaceShip   = (Cast<AResourceUsingObject_SpaceShip>(TargetData.TargetActor.Get()) != nullptr);
	AActor* TargetActor = TargetData.TargetActor.Get();

	if (bIsSpaceShip)
	{
		// 우주선: 타겟 변경 시에만 MoveTo 재발행, 이후 RVO에 위임
		if (bTargetChanged)
		{
			const float ApproachRadius = 800.f;
			const float LocAcceptance  = 100.f;

			FVector Goal;
			if (FindShipApproachPoint(AIController->GetWorld(), TargetActor->GetActorLocation(), ApproachRadius, Goal))
				IssueMoveToLocation(AIController, Goal, LocAcceptance);
			else
				AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);

			InstanceData.LastMoveTarget  = TargetData.TargetActor;
			InstanceData.TimeSinceRepath = 0.f;
		}
	}
	else
	{
		// 플레이어: 주기적 재탐색 유지
		const bool bRepathDue = InstanceData.TimeSinceRepath >= RepathInterval;

		bool bStuck = false;
		if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
		{
			if (APawn* Pawn = AIController->GetPawn())
			{
				const bool bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
				const bool bSlow = (Pawn->GetVelocity().SizeSquared2D() < 50.f * 50.f);
				const bool bFar  = (TargetData.DistanceToTarget > (AcceptanceRadius + 150.f));
				bStuck = bIdle && bSlow && bFar;
			}
		}

		if (bTargetChanged || bRepathDue || bStuck)
		{
			AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);

			InstanceData.LastMoveTarget  = TargetData.TargetActor;
			InstanceData.TimeSinceRepath = 0.f;
		}
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_ChaseTarget::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (AAIController* AIController = InstanceData.AIController)
		AIController->StopMovement();
}
