/**
 * STTask_ChaseTarget_Test.cpp
 *
 * File: Source/Helluna/Private/AI/StateTree/Tasks/STTask_ChaseTarget_Test.cpp
 *
 * SlotManager 없이 거리 기반 공격 판정 + MoveToActor 직진 방식.
 * 분산은 UCrowdFollowingComponent(DetourCrowd/RVO)에 위임.
 *
 * ─── 우주선 흐름 ─────────────────────────────────────────────
 *  EnterState: MoveToActor(SpaceShip, AcceptanceRadius)
 *  Tick:       DistanceToTarget <= AttackRange → Succeeded
 *              Stuck/Idle 감지 시 MoveToActor 재발행
 *
 * ─── 플레이어 흐름 ───────────────────────────────────────────
 *  EnterState: MoveToActor(Player, AcceptanceRadius)
 *  Tick:       RepathInterval마다 재발행, EQS 옵션 지원
 *              DistanceToTarget <= PlayerAttackRange → Succeeded
 *
 * @author 김민우
 */

#include "AI/StateTree/Tasks/STTask_ChaseTarget_Test.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"

#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Components/PrimitiveComponent.h"

// ============================================================================
// 헬퍼: 메시 표면까지 최단 거리 계산
//   1순위: 루트 DynamicMeshComponent → GetClosestPointOnCollision
//          (ComplexAsSimple 콜리전이므로 실제 메시 표면 형상 반영)
//   2순위: ShipCombatCollision 태그 컴포넌트 (레거시 호환)
//   3순위: 중심점 거리 (폴백)
// ============================================================================
static float ComputeSurfaceDistance(const FVector& FromLoc, const AActor* ToActor)
{
	if (!ToActor) return MAX_FLT;

	// 1순위: 루트 PrimitiveComponent (DynamicMeshComponent)
	// ComplexAsSimple + QueryAndPhysics 설정이므로 메시 표면 그대로 거리 계산
	if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(ToActor->GetRootComponent()))
	{
		if (RootPrim->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			FVector ClosestPoint;
			const float D = RootPrim->GetClosestPointOnCollision(FromLoc, ClosestPoint);
			if (D >= 0.f)
				return D;
		}
	}

	// 2순위: ShipCombatCollision 태그 컴포넌트 (레거시 호환)
	TArray<UPrimitiveComponent*> Prims;
	ToActor->GetComponents<UPrimitiveComponent>(Prims);

	float MinDist = MAX_FLT;
	bool bFound = false;

	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim || !Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
			continue;

		FVector Closest;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);
		if (D >= 0.f && D < MinDist)
		{
			MinDist = D;
			bFound = true;
		}
	}

	if (bFound)
		return MinDist;

	// 3순위: 중심점 거리 폴백
	return FVector::Dist(FromLoc, ToActor->GetActorLocation());
}

// ============================================================================
// 헬퍼: EQS 실행 후 결과 위치로 이동 (플레이어 전용)
// ============================================================================
static void RunPlayerAttackEQS(
	UEnvQuery* Query,
	AAIController* AIController,
	float InAcceptanceRadius,
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
			[WeakController, WeakFallback, InAcceptanceRadius](TSharedPtr<FEnvQueryResult> Result)
			{
				AAIController* Ctrl = WeakController.Get();
				if (!Ctrl) return;

				if (Result.IsValid() && !Result->IsAborted() && Result->Items.Num() > 0)
				{
					FAIMoveRequest Req;
					Req.SetGoalLocation(Result->GetItemAsLocation(0));
					Req.SetAcceptanceRadius(InAcceptanceRadius);
					Req.SetUsePathfinding(true);
					Req.SetAllowPartialPath(true);
					Ctrl->MoveTo(Req);
				}
				else if (AActor* Fallback = WeakFallback.Get())
				{
					Ctrl->MoveToActor(Fallback, InAcceptanceRadius, true, true, false, nullptr, true);
				}
			}));
}

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget_Test::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	AAIController* AIController = InstanceData.AIController;
	if (!AIController)
		return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// 인스턴스 데이터 초기화
	InstanceData.TimeSinceRepath  = 0.f;
	InstanceData.TimeUntilNextEQS = 0.f;
	InstanceData.StuckAccumTime   = 0.f;

	if (!TargetData.HasValidTarget())
	{
		InstanceData.LastMoveTarget = nullptr;
		return EStateTreeRunStatus::Running;
	}

	AActor* TargetActor = TargetData.TargetActor.Get();
	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);
	const float CurrentAttackRange = bIsSpaceShip ? AttackRange : PlayerAttackRange;

	// 이미 공격 범위 안이면 즉시 Succeeded
	// 우주선: ShipCombatCollision 표면 거리 기준 판정
	APawn* CheckPawn = AIController->GetPawn();
	const float EffectiveDist = (bIsSpaceShip && CheckPawn)
		? ComputeSurfaceDistance(CheckPawn->GetActorLocation(), TargetActor)
		: TargetData.DistanceToTarget;

	if (EffectiveDist <= CurrentAttackRange)
		return EStateTreeRunStatus::Succeeded;

	// 타겟을 향해 이동 시작
	AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	InstanceData.LastMoveTarget = TargetData.TargetActor;

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget_Test::Tick(
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
	if (!TargetActor) return EStateTreeRunStatus::Failed;

	// ── 공격 중이면 이동 정지 ────────────────────────────────────────
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

	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);
	const float CurrentAttackRange = bIsSpaceShip ? AttackRange : PlayerAttackRange;

	// ── 공격 범위 도달 판정 → Succeeded ──────────────────────────────
	// 우주선: ShipCombatCollision 표면 거리 기준 판정
	const float SurfaceDist = (bIsSpaceShip)
		? ComputeSurfaceDistance(Pawn->GetActorLocation(), TargetActor)
		: TargetData.DistanceToTarget;

	if (SurfaceDist <= CurrentAttackRange)
	{
		AIController->StopMovement();
		return EStateTreeRunStatus::Succeeded;
	}

	InstanceData.TimeSinceRepath  += DeltaTime;
	InstanceData.TimeUntilNextEQS -= DeltaTime;

	// ── Stuck 감지 (공통) ────────────────────────────────────────────
	bool bStuck = false;
	bool bIdle  = false;
	if (UPathFollowingComponent* PFC = AIController->GetPathFollowingComponent())
	{
		bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
		const bool bMoving = (PFC->GetStatus() == EPathFollowingStatus::Moving);
		const bool bSlow   = (Pawn->GetVelocity().SizeSquared2D() < 30.f * 30.f);
		const bool bFar    = (SurfaceDist > CurrentAttackRange + 150.f);

		if (bMoving && bSlow && bFar)
			InstanceData.StuckAccumTime += DeltaTime;
		else
			InstanceData.StuckAccumTime = FMath::Max(0.f, InstanceData.StuckAccumTime - DeltaTime);

		bStuck = (InstanceData.StuckAccumTime >= 2.f);
	}

	if (bIsSpaceShip)
	{
		// ── 우주선: 단순 MoveToActor ─────────────────────────────────
		// Stuck 또는 Idle 상태에서 재발행
		const bool bNeedRepath = bStuck || (bIdle && SurfaceDist > CurrentAttackRange + 100.f);

		if (bNeedRepath)
		{
			AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
			InstanceData.StuckAccumTime = 0.f;
			InstanceData.TimeSinceRepath = 0.f;
		}
	}
	else
	{
		// ── 플레이어: 주기적 재발행 + EQS ────────────────────────────
		const bool bRepathDue     = InstanceData.TimeSinceRepath >= RepathInterval;
		const bool bTargetChanged = InstanceData.LastMoveTarget != TargetData.TargetActor;

		if (bTargetChanged || bRepathDue || bStuck)
		{
			if (AttackPositionQuery && InstanceData.TimeUntilNextEQS <= 0.f)
			{
				RunPlayerAttackEQS(AttackPositionQuery, AIController, AcceptanceRadius, TargetActor);
				InstanceData.TimeUntilNextEQS = EQSInterval;
			}
			else
			{
				AIController->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
			}

			InstanceData.LastMoveTarget  = TargetData.TargetActor;
			InstanceData.TimeSinceRepath = 0.f;
			InstanceData.StuckAccumTime  = 0.f;
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
void FSTTask_ChaseTarget_Test::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (AAIController* AIController = InstanceData.AIController)
		AIController->StopMovement();
}
