/**
 * STTask_ChasePlayer.cpp
 * 플레이어 추적 전용 Task
 */

#include "AI/StateTree/Tasks/STTask_ChasePlayer.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "AITypes.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"

// ── 헬퍼: 위치로 이동 ────────────────────────────────────────
static void ChasePlayer_MoveToLocation(AAIController* AIC, const FVector& Goal, float Radius)
{
	if (!AIC) return;
	FAIMoveRequest Req;
	Req.SetGoalLocation(Goal);
	Req.SetAcceptanceRadius(Radius);
	Req.SetUsePathfinding(true);
	Req.SetAllowPartialPath(true);
	AIC->MoveTo(Req);
}

// ── 헬퍼: EQS 실행 ───────────────────────────────────────────
static void ChasePlayer_RunEQS(UEnvQuery* Query, AAIController* AIC, float Radius, AActor* Fallback)
{
	if (!Query || !AIC) return;
	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return;
	UEnvQueryManager* Mgr = UEnvQueryManager::GetCurrent(Pawn->GetWorld());
	if (!Mgr) return;

	TWeakObjectPtr<AAIController> WeakAIC = AIC;
	TWeakObjectPtr<AActor> WeakFallback   = Fallback;

	FEnvQueryRequest Req(Query, Pawn);
	Req.Execute(EEnvQueryRunMode::SingleResult,
		FQueryFinishedSignature::CreateLambda(
			[WeakAIC, WeakFallback, Radius](TSharedPtr<FEnvQueryResult> Result)
			{
				AAIController* Ctrl = WeakAIC.Get();
				if (!Ctrl) return;
				if (Result.IsValid() && !Result->IsAborted() && Result->Items.Num() > 0)
					ChasePlayer_MoveToLocation(Ctrl, Result->GetItemAsLocation(0), Radius);
				else if (AActor* F = WeakFallback.Get())
					Ctrl->MoveToActor(F, Radius, true, true, false, nullptr, true);
			}));
}

// ============================================================================
EStateTreeRunStatus FSTTask_ChasePlayer::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);
	if (!D.AIController) return EStateTreeRunStatus::Failed;

	D.TimeSinceRepath   = 0.f;
	D.TimeUntilNextEQS  = 0.f;
	D.LastMoveTarget    = nullptr;

	const FHellunaAITargetData& TD = D.TargetData;
	if (!TD.HasValidTarget()) return EStateTreeRunStatus::Running;

	AActor* Target = TD.TargetActor.Get();
	if (AttackPositionQuery)
		ChasePlayer_RunEQS(AttackPositionQuery, D.AIController, AcceptanceRadius, Target);
	else
		D.AIController->MoveToActor(Target, AcceptanceRadius, true, true, false, nullptr, true);

	D.LastMoveTarget = TD.TargetActor;
	return EStateTreeRunStatus::Running;
}

// ============================================================================
EStateTreeRunStatus FSTTask_ChasePlayer::Tick(
	FStateTreeExecutionContext& Context, float DeltaTime) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);
	if (!D.AIController) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TD = D.TargetData;
	if (!TD.HasValidTarget()) return EStateTreeRunStatus::Failed;

	APawn* Pawn = D.AIController->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AActor* Target = TD.TargetActor.Get();

	// 공격 중이면 이동 정지
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
			if (Tag.IsValid() && ASC->HasMatchingGameplayTag(Tag))
			{
				D.AIController->StopMovement();
				return EStateTreeRunStatus::Running;
			}
		}
	}

	D.TimeSinceRepath  += DeltaTime;
	D.TimeUntilNextEQS -= DeltaTime;

	const bool bRepathDue     = D.TimeSinceRepath >= RepathInterval;
	const bool bTargetChanged = D.LastMoveTarget != TD.TargetActor;

	bool bStuck = false;
	if (UPathFollowingComponent* PFC = D.AIController->GetPathFollowingComponent())
	{
		const bool bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
		const bool bSlow = (Pawn->GetVelocity().SizeSquared2D() < 50.f * 50.f);
		const bool bFar  = (TD.DistanceToTarget > AcceptanceRadius + 150.f);
		bStuck = bIdle && bSlow && bFar;
	}

	if (bTargetChanged || bRepathDue || bStuck)
	{
		if (AttackPositionQuery && D.TimeUntilNextEQS <= 0.f)
		{
			ChasePlayer_RunEQS(AttackPositionQuery, D.AIController, AcceptanceRadius, Target);
			D.TimeUntilNextEQS = EQSInterval;
		}
		else
		{
			D.AIController->MoveToActor(Target, AcceptanceRadius, true, true, false, nullptr, true);
		}
		D.LastMoveTarget  = TD.TargetActor;
		D.TimeSinceRepath = 0.f;
	}

	// 타겟 방향 회전
	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (!ToTarget.IsNearlyZero())
	{
		Pawn->SetActorRotation(FMath::RInterpTo(
			Pawn->GetActorRotation(),
			FRotator(0.f, ToTarget.Rotation().Yaw, 0.f),
			DeltaTime, 5.f));
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
void FSTTask_ChasePlayer::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);
	if (D.AIController) D.AIController->StopMovement();
}
