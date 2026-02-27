/**
 * STTask_ChaseTarget.cpp
 *
 * 플레이어/우주선을 향해 이동 명령을 발행하고 경로를 유지한다.
 *
 * ─── 이동 방식 요약 ──────────────────────────────────────────
 *  [플레이어]
 *    - RepathInterval마다 MoveToActor 재발행 (움직이는 대상 추적)
 *    - AttackPositionQuery 설정 시 EQS로 공격 위치 계산 후 이동
 *    - 이동 중 타겟 방향으로 서서히 회전 (RInterpTo)
 *    - Stuck(이동 중 저속) 감지 시 즉시 재발행
 *
 *  [우주선]
 *    - EnterState에서 이동 명령 1회 발행 (고정 대상)
 *    - FindShipApproachPoint로 NavMesh 위 랜덤 접근 위치 사용
 *      (여러 몬스터가 같은 지점으로 몰리는 것 방지)
 *    - Stuck 또는 Idle+원거리 감지 시에만 재발행
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
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"

#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

// ============================================================================
// 헬퍼: 위치로 이동 명령
// ============================================================================
static void IssueMoveToLocation(AAIController* AIController, const FVector& Goal, float AcceptanceRadius)
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

// ============================================================================
// 헬퍼: 우주선 주변 NavMesh 위 랜덤 접근 위치 반환
// ============================================================================
static bool FindShipApproachPoint(UWorld* World, const FVector& ShipCenter, float Radius, FVector& OutPoint)
{
	if (!World) return false;
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	FNavLocation NavLoc;
	if (!NavSys->GetRandomReachablePointInRadius(ShipCenter, Radius, NavLoc))
		return false;

	OutPoint = NavLoc.Location;
	return true;
}

// ============================================================================
// 헬퍼: EQS 실행 후 결과 위치로 이동 (플레이어 전용)
// ============================================================================
static void RunAttackPositionEQS(
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
					Ctrl->MoveToActor(Fallback, AcceptanceRadius, true, true, false, nullptr, true);
				}
			}));
}

// ============================================================================
// 우주선 이동 명령 발행 헬퍼
// ============================================================================
static void IssueShipMove(AAIController* AIController, AActor* ShipActor, float AcceptanceRadius)
{
	FVector Goal;
	if (FindShipApproachPoint(AIController->GetWorld(), ShipActor->GetActorLocation(), 800.f, Goal))
		IssueMoveToLocation(AIController, Goal, AcceptanceRadius);
	else
		AIController->MoveToActor(ShipActor, AcceptanceRadius, true, true, false, nullptr, true);
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
		return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.HasValidTarget())
	{
		// Evaluator가 아직 첫 Tick을 안 돌았을 수 있음
		// Failed 대신 Running 반환 → Tick에서 타겟 생기면 이동 명령 발행
		InstanceData.LastMoveTarget  = nullptr;
		InstanceData.TimeSinceRepath = 0.f;
		InstanceData.TimeUntilNextEQS = 0.f;
		return EStateTreeRunStatus::Running;
	}

	AActor* TargetActor = TargetData.TargetActor.Get();
	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	InstanceData.TimeSinceRepath  = 0.f;
	InstanceData.TimeUntilNextEQS = 0.f;

	if (bIsSpaceShip)
	{
		// 우주선: FindShipApproachPoint로 NavMesh 위 랜덤 접근 위치로 이동
		IssueShipMove(AIController, TargetActor, AcceptanceRadius);
	}
	else
	{
		// 플레이어: 직접 추적 시작
		AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	}

	// 이동 명령 후에 설정해야 Tick에서 bTargetChanged가 정상 동작
	InstanceData.LastMoveTarget = TargetData.TargetActor;

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
	if (!AIController) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;
	if (!TargetData.HasValidTarget()) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AActor* TargetActor = TargetData.TargetActor.Get();

	// 공격 중이면 이동 정지
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

	InstanceData.TimeSinceRepath  += DeltaTime;
	InstanceData.TimeUntilNextEQS -= DeltaTime;

	const bool bTargetChanged = InstanceData.LastMoveTarget != TargetData.TargetActor;
	const bool bIsSpaceShip   = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	if (bIsSpaceShip)
	{
		// ── 우주선 ──────────────────────────────────────────────
		// EQS 사용 안 함. FindShipApproachPoint로 NavMesh 위 접근 위치 사용.
		// Stuck: Moving 중인데 속도가 거의 없을 때만 재발행
		bool bStuck = false;
		if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
		{
			const bool bMoving = (PFC->GetStatus() == EPathFollowingStatus::Moving);
			const bool bSlow   = (Pawn->GetVelocity().SizeSquared2D() < 30.f * 30.f);
			const bool bFar    = (TargetData.DistanceToTarget > AcceptanceRadius + 150.f);
			bStuck = bMoving && bSlow && bFar;
		}

		// Idle이고 멀리 있으면 이동 명령 재발행 (EnterState 실패 대비)
		bool bNeedMove = false;
		if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
		{
			const bool bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
			const bool bFar  = (TargetData.DistanceToTarget > AcceptanceRadius + 200.f);
			bNeedMove = bIdle && bFar;
		}

		if (bTargetChanged || bStuck || bNeedMove)
		{
			IssueShipMove(AIController, TargetActor, AcceptanceRadius);
			InstanceData.LastMoveTarget  = TargetData.TargetActor;
			InstanceData.TimeSinceRepath = 0.f;
		}
	}
	else
	{
		// ── 플레이어 ────────────────────────────────────────────
		const bool bRepathDue = InstanceData.TimeSinceRepath >= RepathInterval;

		bool bStuck = false;
		if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
		{
			const bool bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
			const bool bSlow = (Pawn->GetVelocity().SizeSquared2D() < 50.f * 50.f);
			const bool bFar  = (TargetData.DistanceToTarget > (AcceptanceRadius + 150.f));
			bStuck = bIdle && bSlow && bFar;
		}

		if (bTargetChanged || bRepathDue || bStuck)
		{
			if (AttackPositionQuery && InstanceData.TimeUntilNextEQS <= 0.f)
			{
				RunAttackPositionEQS(AttackPositionQuery, AIController, AcceptanceRadius, TargetActor);
				InstanceData.TimeUntilNextEQS = EQSInterval;
			}
			else
			{
				AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
			}

			InstanceData.LastMoveTarget  = TargetData.TargetActor;
			InstanceData.TimeSinceRepath = 0.f;
		}

		// 플레이어를 향해 회전
		const FVector ToTarget = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator TargetRot  = FRotator(0.f, ToTarget.Rotation().Yaw, 0.f);
			Pawn->SetActorRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 5.f));
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
