/**
 * STTask_ChaseSpaceShip.cpp
 *
 * 우주선 추적 전용 Task.
 * 에디터에서 이동 전략(Direct / RandomOffset / EQS)을 토글할 수 있다.
 *
 * Stuck 감지: 위치 변화량 기반 (속도가 아닌 실제 이동 거리)
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_ChaseSpaceShip.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "AITypes.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"

// ── 헬퍼: 우주선 방향 NavMesh 위 목표 계산 ─────────────────
static FVector ComputeNavGoalTowardShip(
	const FVector& PawnLoc,
	const FVector& ShipLoc,
	AAIController* AIC,
	float MaxStepDist = 1200.f)
{
	const float Dist = FVector::Dist(PawnLoc, ShipLoc);
	const FVector Dir = (ShipLoc - PawnLoc).GetSafeNormal();
	const FVector RawGoal = PawnLoc + Dir * FMath::Min(Dist * 0.8f, MaxStepDist);

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	FNavLocation NavGoal;
	if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(200.f, 200.f, 500.f)))
		return NavGoal.Location;

	return RawGoal;
}

// ── 헬퍼: 위치로 이동 ────────────────────────────────────────
static void ShipMove_MoveToLocation(AAIController* AIC, const FVector& Goal, float Radius)
{
	if (!IsValid(AIC)) return;
	FAIMoveRequest Req;
	Req.SetGoalLocation(Goal);
	Req.SetAcceptanceRadius(Radius);
	Req.SetUsePathfinding(true);
	Req.SetAllowPartialPath(true);
	FPathFollowingRequestResult Result = AIC->MoveTo(Req);

	// MoveTo 실패 시 목표 방향 중간 지점으로 폴백
	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		const FVector PawnLoc = AIC->GetPawn() ? AIC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		const FVector FallbackGoal = ComputeNavGoalTowardShip(PawnLoc, Goal, AIC);

		FAIMoveRequest FallbackReq;
		FallbackReq.SetGoalLocation(FallbackGoal);
		FallbackReq.SetAcceptanceRadius(Radius);
		FallbackReq.SetUsePathfinding(true);
		FallbackReq.SetAllowPartialPath(true);
		AIC->MoveTo(FallbackReq);
	}
}

// ── 헬퍼: 우회 오프셋 목표 계산 (좌/우 랜덤) ─────────────────
static FVector ComputeDetourGoal(
	const FVector& PawnLoc,
	const FVector& ShipLoc,
	AAIController* AIC,
	float OffsetAmount)
{
	const FVector DirToShip = (ShipLoc - PawnLoc).GetSafeNormal2D();
	// 좌/우 90도 방향 중 랜덤 선택
	const FVector Right = FVector::CrossProduct(FVector::UpVector, DirToShip).GetSafeNormal();
	const float Sign = (FMath::RandBool()) ? 1.f : -1.f;

	// 전방 + 측면 오프셋 조합
	const FVector RawGoal = PawnLoc + DirToShip * OffsetAmount * 0.5f + Right * Sign * OffsetAmount;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	FNavLocation NavGoal;
	if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(300.f, 300.f, 500.f)))
		return NavGoal.Location;

	return RawGoal;
}

// ── 헬퍼: EQS 실행 후 결과 위치로 이동 ──────────────────────
static void ShipMove_RunEQS(UEnvQuery* Query, AAIController* AIC, float Radius, AActor* Fallback)
{
	if (!Query || !IsValid(AIC)) return;
	APawn* Pawn = AIC->GetPawn();
	if (!IsValid(Pawn)) return;

	UWorld* World = Pawn->GetWorld();
	if (!World) return;

	UEnvQueryManager* Mgr = UEnvQueryManager::GetCurrent(World);
	if (!Mgr) return;

	TWeakObjectPtr<AAIController> WeakAIC      = AIC;
	TWeakObjectPtr<AActor>        WeakFallback = Fallback;

	FEnvQueryRequest Req(Query, Pawn);
	Req.Execute(EEnvQueryRunMode::SingleResult,
		FQueryFinishedSignature::CreateLambda(
			[WeakAIC, WeakFallback, Radius](TSharedPtr<FEnvQueryResult> Result)
			{
				AAIController* Ctrl = WeakAIC.Get();
				if (!IsValid(Ctrl))
				{
					UE_LOG(LogTemp, Error, TEXT("[ChaseShip][EQS] 콜백: AIController 무효"));
					return;
				}

				const bool bValid = Result.IsValid() && !Result->IsAborted();
				const int32 ItemCount = bValid ? Result->Items.Num() : 0;
				UE_LOG(LogTemp, Warning, TEXT("[ChaseShip][EQS] 콜백: bValid=%d | Items=%d | bAborted=%d"),
					(int)bValid, ItemCount, bValid ? (int)Result->IsAborted() : -1);

				if (bValid && ItemCount > 0)
				{
					const FVector Dest = Result->GetItemAsLocation(0);
					UE_LOG(LogTemp, Warning, TEXT("[ChaseShip][EQS] MoveToLocation: %s"), *Dest.ToString());
					ShipMove_MoveToLocation(Ctrl, Dest, Radius);
				}
				else if (AActor* F = WeakFallback.Get(); IsValid(F))
				{
					const FVector PawnLoc = Ctrl->GetPawn()->GetActorLocation();
					const FVector Goal = ComputeNavGoalTowardShip(PawnLoc, F->GetActorLocation(), Ctrl);
					UE_LOG(LogTemp, Warning, TEXT("[ChaseShip][EQS] 폴백 이동: %s"), *Goal.ToString());
					ShipMove_MoveToLocation(Ctrl, Goal, Radius);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[ChaseShip][EQS] 콜백: 결과도 없고 폴백도 무효"));
				}
			}));
}

// ── 헬퍼: Direct/RandomOffset 공통 이동 발행 ─────────────────
static void IssueMoveTowardShip(
	AAIController* AIC,
	APawn* Pawn,
	AActor* Ship,
	float AcceptanceRadius)
{
	const FVector Goal = ComputeNavGoalTowardShip(
		Pawn->GetActorLocation(), Ship->GetActorLocation(), AIC);
	ShipMove_MoveToLocation(AIC, Goal, AcceptanceRadius);
}

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseSpaceShip::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);
	if (!IsValid(D.AIController))
		return EStateTreeRunStatus::Failed;

	D.TimeSinceRepath      = 0.f;
	D.TimeUntilNextEQS     = 0.f;
	D.StuckCheckTimer      = 0.f;
	D.ConsecutiveStuckCount = 0;

	APawn* Pawn = D.AIController->GetPawn();
	if (!IsValid(Pawn))
		return EStateTreeRunStatus::Failed;

	D.LastCheckedLocation = Pawn->GetActorLocation();

	const FHellunaAITargetData& TD = D.TargetData;
	if (!TD.HasValidTarget())
		return EStateTreeRunStatus::Running;

	AActor* Ship = TD.TargetActor.Get();
	if (!IsValid(Ship))
		return EStateTreeRunStatus::Running;

	// ── 전략별 초기 이동 ────────────────────────────────────────
	switch (Strategy)
	{
	case EChaseShipStrategy::EQS:
		if (AttackPositionQuery)
		{
			ShipMove_RunEQS(AttackPositionQuery, D.AIController, AcceptanceRadius, Ship);
			break;
		}
		// EQS 에셋 미설정 시 Direct로 폴백
		[[fallthrough]];

	case EChaseShipStrategy::Direct:
	case EChaseShipStrategy::RandomOffset:
		IssueMoveTowardShip(D.AIController, Pawn, Ship, AcceptanceRadius);
		break;
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseSpaceShip::Tick(
	FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);
	if (!IsValid(D.AIController)) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TD = D.TargetData;
	if (!TD.HasValidTarget()) return EStateTreeRunStatus::Failed;

	APawn* Pawn = D.AIController->GetPawn();
	if (!IsValid(Pawn)) return EStateTreeRunStatus::Failed;

	AActor* Ship = TD.TargetActor.Get();
	if (!IsValid(Ship)) return EStateTreeRunStatus::Failed;

	// ── 공격 태그 있으면 이동 정지 ───────────────────────────
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
				FName("State.Enemy.Attacking"), false);
			if (Tag.IsValid() && ASC->HasMatchingGameplayTag(Tag))
			{
				D.AIController->StopMovement();
				return EStateTreeRunStatus::Running;
			}
		}
	}

	D.TimeSinceRepath  += DeltaTime;
	D.TimeUntilNextEQS -= DeltaTime;

	// ── Stuck 감지 (위치 변화량 기반) ──────────────────────────
	D.StuckCheckTimer += DeltaTime;
	bool bStuck = false;

	if (D.StuckCheckTimer >= StuckCheckInterval)
	{
		const FVector CurrentLoc = Pawn->GetActorLocation();
		const float MovedDist = FVector::Dist(CurrentLoc, D.LastCheckedLocation);

		bStuck = (MovedDist <= StuckDistThreshold)
			&& (TD.DistanceToTarget > AcceptanceRadius + 150.f);

		if (bStuck)
		{
			D.ConsecutiveStuckCount++;
		}
		else
		{
			D.ConsecutiveStuckCount = 0;
		}

		D.LastCheckedLocation = CurrentLoc;
		D.StuckCheckTimer = 0.f;
	}

	// ── 재경로 판정 ──────────────────────────────────────────
	const bool bRepathDue = D.TimeSinceRepath >= RepathInterval;

	if (bRepathDue || bStuck)
	{
		switch (Strategy)
		{
		case EChaseShipStrategy::Direct:
			IssueMoveTowardShip(D.AIController, Pawn, Ship, AcceptanceRadius);
			break;

		case EChaseShipStrategy::RandomOffset:
			if (bStuck)
			{
				// Stuck 횟수에 비례해서 우회 강도 증가 (최대 3배)
				const float Multiplier = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
				const FVector DetourGoal = ComputeDetourGoal(
					Pawn->GetActorLocation(),
					Ship->GetActorLocation(),
					D.AIController,
					DetourOffset * Multiplier);
				ShipMove_MoveToLocation(D.AIController, DetourGoal, AcceptanceRadius);

				UE_LOG(LogTemp, Log, TEXT("[ChaseShip] RandomOffset 우회 | StuckCount=%d | Offset=%.0f"),
					D.ConsecutiveStuckCount, DetourOffset * Multiplier);
			}
			else
			{
				IssueMoveTowardShip(D.AIController, Pawn, Ship, AcceptanceRadius);
			}
			break;

		case EChaseShipStrategy::EQS:
			if (AttackPositionQuery && D.TimeUntilNextEQS <= 0.f)
			{
				ShipMove_RunEQS(AttackPositionQuery, D.AIController, AcceptanceRadius, Ship);
				D.TimeUntilNextEQS = EQSInterval;
			}
			else if (!AttackPositionQuery)
			{
				// EQS 미설정 폴백
				IssueMoveTowardShip(D.AIController, Pawn, Ship, AcceptanceRadius);
			}
			break;
		}

		D.TimeSinceRepath = 0.f;
	}

	// ── 우주선 방향 회전 ─────────────────────────────────────
	const FVector ToShip = (Ship->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (!ToShip.IsNearlyZero())
	{
		Pawn->SetActorRotation(FMath::RInterpTo(
			Pawn->GetActorRotation(),
			FRotator(0.f, ToShip.Rotation().Yaw, 0.f),
			DeltaTime, 5.f));
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
	FInstanceDataType& D = Context.GetInstanceData(*this);
	if (IsValid(D.AIController))
	{
		D.AIController->StopMovement();
	}
}