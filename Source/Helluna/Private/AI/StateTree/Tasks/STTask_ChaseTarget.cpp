/**
 * STTask_ChaseTarget.cpp
 *
 * 2-Phase Ship Chase + Player Chase
 *
 * Rush:   Aim for ring point at assigned angle -> different NavMesh paths
 * Spread: Move to standoff position at assigned angle near ship
 * Stuck:  Rotate angle. After N consecutive stucks, disable pathfinding
 *         and walk directly (CMC Walking handles ground/collision).
 *
 * @author
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

#include <atomic>

static std::atomic<int32> GAngleCounter{0};

// ============================================================================
// Helpers
// ============================================================================
namespace ChaseHelpers
{

FVector ProjectToNav(UWorld* World, const FVector& Point, const FVector& Extent = FVector(500.f, 500.f, 1000.f))
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) return Point;
	FNavLocation NavLoc;
	if (NavSys->ProjectPointToNavigation(Point, NavLoc, Extent))
		return NavLoc.Location;
	return Point;
}

/**
 * Issue MoveTo. Returns true if path request was accepted.
 * Returns false if pathfinding failed (no NavMesh path).
 */
static bool IssueMoveToLocation(AAIController* AIC, const FVector& Goal, float Radius, bool bUsePathfinding = true)
{
	if (!AIC) return false;
	FAIMoveRequest Req;
	Req.SetGoalLocation(Goal);
	Req.SetAcceptanceRadius(Radius);
	Req.SetReachTestIncludesAgentRadius(false);
	Req.SetReachTestIncludesGoalRadius(false);
	Req.SetUsePathfinding(bUsePathfinding);
	Req.SetAllowPartialPath(true);
	Req.SetCanStrafe(false);
	const FPathFollowingRequestResult Result = AIC->MoveTo(Req);
	return Result.Code != EPathFollowingRequestResult::Failed;
}

FVector AngleToWorldPos(const FVector& Center, float AngleDeg, float Radius)
{
	const float Rad = FMath::DegreesToRadians(AngleDeg);
	return FVector(
		Center.X + FMath::Cos(Rad) * Radius,
		Center.Y + FMath::Sin(Rad) * Radius,
		Center.Z);
}

/**
 * Rush goal: aim for the ring point (assigned angle at SpreadPhaseRadius).
 * Each monster takes a different NavMesh path.
 */
FVector ComputeRushGoal(
	const FVector& PawnLoc,
	const FVector& ShipLoc,
	float AssignedAngleDeg,
	float SpreadPhaseRadius,
	float WaypointStep,
	UWorld* World)
{
	const FVector RingTarget = AngleToWorldPos(ShipLoc, AssignedAngleDeg, SpreadPhaseRadius);
	const FVector ToRing = RingTarget - PawnLoc;
	const float DistToRing = ToRing.Size2D();

	FVector Goal;
	if (DistToRing <= WaypointStep)
		Goal = RingTarget;
	else
		Goal = PawnLoc + ToRing.GetSafeNormal2D() * WaypointStep;

	return ProjectToNav(World, Goal, FVector(400.f, 400.f, 800.f));
}

FVector ComputeSpreadGoal(
	const FVector& ShipLoc,
	float AssignedAngleDeg,
	float StandoffRadius,
	UWorld* World)
{
	const FVector RawGoal = AngleToWorldPos(ShipLoc, AssignedAngleDeg, StandoffRadius);
	return ProjectToNav(World, RawGoal, FVector(500.f, 500.f, 1000.f));
}

void RunAttackPositionEQS(
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

	TWeakObjectPtr<AAIController> WeakCtrl = AIController;
	TWeakObjectPtr<AActor> WeakFallback = FallbackTarget;

	FEnvQueryRequest Request(Query, Pawn);
	Request.Execute(EEnvQueryRunMode::SingleResult,
		FQueryFinishedSignature::CreateLambda(
			[WeakCtrl, WeakFallback, AcceptanceRadius](TSharedPtr<FEnvQueryResult> Result)
			{
				AAIController* Ctrl = WeakCtrl.Get();
				if (!Ctrl) return;
				if (Result.IsValid() && !Result->IsAborted() && Result->Items.Num() > 0)
					ChaseHelpers::IssueMoveToLocation(Ctrl, Result->GetItemAsLocation(0), AcceptanceRadius);
				else if (AActor* Fallback = WeakFallback.Get())
					Ctrl->MoveToActor(Fallback, AcceptanceRadius, true, true, false, nullptr, true);
			}));
}

void FaceTarget(APawn* Pawn, AActor* Target, float DeltaTime, float InterpSpeed = 10.f)
{
	if (!Pawn || !Target) return;
	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero()) return;

	const FRotator CurrentRot = Pawn->GetActorRotation();
	const FRotator TargetRot(0.f, ToTarget.Rotation().Yaw, 0.f);
	const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, InterpSpeed);
	Pawn->SetActorRotation(NewRot);
	if (AController* C = Pawn->GetController())
		C->SetControlRotation(NewRot);
}

} // namespace ChaseHelpers

using namespace ChaseHelpers;

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* AIC = Data.AIController;
	if (!AIC)
	{
		UE_LOG(LogTemp, Error, TEXT("[enemybugreport][Chase] EnterState FAIL: NullAIController"));
		return EStateTreeRunStatus::Failed;
	}

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TD = Data.TargetData;
	if (!TD.HasValidTarget())
	{
		Data.LastMoveTarget = nullptr;
		return EStateTreeRunStatus::Running;
	}

	AActor* Target = TD.TargetActor.Get();
	const bool bIsShip = (Cast<AResourceUsingObject_SpaceShip>(Target) != nullptr);

	// Reset
	Data.TimeSinceRepath = 0.f;
	Data.TimeUntilNextEQS = 0.f;
	Data.StuckCheckTimer = 0.f;
	Data.ConsecutiveStuckCount = 0;
	Data.bSpreadArrived = false;
	Data.bDirectMoveMode = false;
	Data.DiagTimer = 0.f;
	Data.LastCheckedLocation = Pawn->GetActorLocation();

	if (bIsShip)
	{
		const int32 AngleIndex = GAngleCounter.fetch_add(1);
		Data.AssignedAngleDeg = FMath::Fmod(AngleIndex * 60.f, 360.f);

		const float DistToShip2D = FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation());

		if (DistToShip2D > SpreadPhaseRadius)
		{
			Data.Phase = EChasePhase::Rush;
			const FVector Goal = ComputeRushGoal(
				Pawn->GetActorLocation(), Target->GetActorLocation(),
				Data.AssignedAngleDeg, SpreadPhaseRadius, RushWaypointStep,
				Pawn->GetWorld());
			IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
		}
		else
		{
			Data.Phase = EChasePhase::Spread;
			const FVector Goal = ComputeSpreadGoal(
				Target->GetActorLocation(), Data.AssignedAngleDeg, StandoffRadius,
				Pawn->GetWorld());
			IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][Chase] Enter: Enemy=%s Angle=%.0f Phase=%s Dist2D=%.1f"),
			*GetNameSafe(Pawn), Data.AssignedAngleDeg,
			Data.Phase == EChasePhase::Rush ? TEXT("Rush") : TEXT("Spread"),
			DistToShip2D);
	}
	else
	{
		AIC->MoveToActor(Target, AcceptanceRadius, true, true, false, nullptr, true);
	}

	Data.LastMoveTarget = TD.TargetActor;
	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	AAIController* AIC = Data.AIController;
	if (!AIC) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TD = Data.TargetData;
	if (!TD.HasValidTarget()) return EStateTreeRunStatus::Failed;

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn) return EStateTreeRunStatus::Failed;

	AActor* Target = TD.TargetActor.Get();

	// Stop if attacking
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
			if (AttackingTag.IsValid() && ASC->HasMatchingGameplayTag(AttackingTag))
			{
				AIC->StopMovement();
				return EStateTreeRunStatus::Running;
			}
		}
	}

	Data.TimeSinceRepath += DeltaTime;
	Data.TimeUntilNextEQS -= DeltaTime;

	const bool bIsShip = (Cast<AResourceUsingObject_SpaceShip>(Target) != nullptr);

	if (bIsShip)
		return TickShipChase(Context, Data, AIC, Pawn, Target, DeltaTime);
	else
		return TickPlayerChase(Data, AIC, Pawn, Target, DeltaTime);
}

// ============================================================================
// TickShipChase
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::TickShipChase(
	FStateTreeExecutionContext& Context,
	FInstanceDataType& Data, AAIController* AIC, APawn* Pawn,
	AActor* Ship, float DeltaTime) const
{
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector ShipLoc = Ship->GetActorLocation();
	const float DistToShip2D = FVector::Dist2D(PawnLoc, ShipLoc);

	// --- Diagnostic ---
	Data.DiagTimer -= DeltaTime;
	if (Data.DiagTimer <= 0.f)
	{
		Data.DiagTimer = 1.f;

		FString PFCStatus = TEXT("None");
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
		{
			switch (PFC->GetStatus())
			{
			case EPathFollowingStatus::Idle:   PFCStatus = TEXT("Idle"); break;
			case EPathFollowingStatus::Moving: PFCStatus = TEXT("Moving"); break;
			case EPathFollowingStatus::Paused: PFCStatus = TEXT("Paused"); break;
			default: PFCStatus = TEXT("Other"); break;
			}
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][Chase] Tick: Enemy=%s Phase=%s Angle=%.0f Dist2D=%.1f Vel=%.1f StuckCnt=%d Direct=%d Arrived=%d PFC=%s"),
			*GetNameSafe(Pawn),
			Data.Phase == EChasePhase::Rush ? TEXT("Rush") : TEXT("Spread"),
			Data.AssignedAngleDeg, DistToShip2D,
			Pawn->GetVelocity().Size2D(),
			Data.ConsecutiveStuckCount,
			(int)Data.bDirectMoveMode,
			(int)Data.bSpreadArrived,
			*PFCStatus);
	}

	// --- Already arrived ---
	if (Data.bSpreadArrived)
	{
		AIC->StopMovement();
		AIC->SetFocus(Ship);
		FaceTarget(Pawn, Ship, DeltaTime, 15.f);

		const FVector ToShip = (ShipLoc - PawnLoc).GetSafeNormal2D();
		if (!ToShip.IsNearlyZero())
		{
			const float YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
				Pawn->GetActorRotation().Yaw, ToShip.Rotation().Yaw));
			if (YawDelta > 2.f)
				Pawn->ForceNetUpdate();
		}
		return EStateTreeRunStatus::Running;
	}

	// --- Phase transition: Rush -> Spread ---
	if (Data.Phase == EChasePhase::Rush && DistToShip2D <= SpreadPhaseRadius)
	{
		Data.Phase = EChasePhase::Spread;
		Data.ConsecutiveStuckCount = 0;
		Data.StuckCheckTimer = 0.f;
		Data.LastCheckedLocation = PawnLoc;
		// Keep bDirectMoveMode if already set

		if (!Data.bDirectMoveMode)
		{
			const FVector Goal = ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, StandoffRadius, Pawn->GetWorld());
			IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
		}
		// DirectMode: AddMovementInput handles movement in tick, no MoveTo needed

		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][Chase] Rush->Spread: Enemy=%s Angle=%.0f Direct=%d Dist2D=%.1f"),
			*GetNameSafe(Pawn), Data.AssignedAngleDeg, (int)Data.bDirectMoveMode, DistToShip2D);
		return EStateTreeRunStatus::Running;
	}

	// --- Stuck detection ---
	Data.StuckCheckTimer += DeltaTime;
	if (Data.StuckCheckTimer >= StuckCheckInterval)
	{
		Data.StuckCheckTimer = 0.f;
		const float Moved = FVector::Dist2D(PawnLoc, Data.LastCheckedLocation);

		if (Moved < StuckDistThreshold)
		{
			Data.ConsecutiveStuckCount++;

			// After N consecutive stucks, switch to direct movement (no pathfinding)
			if (!Data.bDirectMoveMode && Data.ConsecutiveStuckCount >= DirectMoveStuckThreshold)
			{
				Data.bDirectMoveMode = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][Chase] DirectMode ON: Enemy=%s StuckCnt=%d Dist2D=%.1f"),
					*GetNameSafe(Pawn), Data.ConsecutiveStuckCount, DistToShip2D);
			}

			// DirectMode force arrive: collision barrier blocks all directions
			if (Data.bDirectMoveMode && Data.ConsecutiveStuckCount >= DirectModeForceArriveThreshold)
			{
				Data.bSpreadArrived = true;
				AIC->StopMovement();
				AIC->SetFocus(Ship);

				const FVector ToShip = (ShipLoc - PawnLoc).GetSafeNormal2D();
				if (!ToShip.IsNearlyZero())
				{
					const FRotator FaceRot(0.f, ToShip.Rotation().Yaw, 0.f);
					Pawn->SetActorRotation(FaceRot);
					if (AController* C = Pawn->GetController())
						C->SetControlRotation(FaceRot);
					Pawn->ForceNetUpdate();
				}

				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][Chase] ForceArrive (DirectMode stuck): Enemy=%s StuckCnt=%d Dist2D=%.1f"),
					*GetNameSafe(Pawn), Data.ConsecutiveStuckCount, DistToShip2D);
				return EStateTreeRunStatus::Running;
			}

			// Rotate angle
			Data.AssignedAngleDeg = FMath::Fmod(Data.AssignedAngleDeg + AngleRotationOnStuck, 360.f);

			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][Chase] STUCK: Enemy=%s Moved=%.1f NewAngle=%.0f StuckCnt=%d Direct=%d Phase=%s Dist2D=%.1f"),
				*GetNameSafe(Pawn), Moved, Data.AssignedAngleDeg,
				Data.ConsecutiveStuckCount, (int)Data.bDirectMoveMode,
				Data.Phase == EChasePhase::Rush ? TEXT("Rush") : TEXT("Spread"),
				DistToShip2D);

			// Reissue movement (DirectMode uses AddMovementInput in tick, no MoveTo needed)
			if (Data.bDirectMoveMode)
			{
				// Direction changes automatically via angle rotation; AddMovementInput handles it next tick
			}
			else if (Data.Phase == EChasePhase::Rush)
			{
				const FVector Goal = ComputeRushGoal(PawnLoc, ShipLoc,
					Data.AssignedAngleDeg, SpreadPhaseRadius, RushWaypointStep, Pawn->GetWorld());
				IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
			}
			else
			{
				const FVector Goal = ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, StandoffRadius, Pawn->GetWorld());
				IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
			}
		}
		else
		{
			// Do NOT reset stuck count here.
			// Lateral movement along NavMesh boundary looks like "progress"
			// but doesn't actually get closer to the ship.
			// Count only resets on phase transition (Rush->Spread).
			// DirectMode activates quickly and works reliably (Vel=600).
		}
		Data.LastCheckedLocation = PawnLoc;
	}

	// --- Direct move mode: AddMovementInput every tick (bypasses PathFollowing) ---
	if (Data.bDirectMoveMode)
	{
		// Stop any active pathfollowing so it doesn't fight with AddMovementInput
		AIC->StopMovement();

		// Target = standoff position at assigned angle
		const FVector SpreadTarget = AngleToWorldPos(ShipLoc, Data.AssignedAngleDeg, StandoffRadius);
		const float DistToTarget2D = FVector::Dist2D(PawnLoc, SpreadTarget);

		// Arrival check (generous: AcceptanceRadius + 100)
		if (DistToTarget2D <= AcceptanceRadius + 100.f)
		{
			Data.bSpreadArrived = true;
			AIC->SetFocus(Ship);

			const FVector ToShip = (ShipLoc - PawnLoc).GetSafeNormal2D();
			if (!ToShip.IsNearlyZero())
			{
				const FRotator FaceRot(0.f, ToShip.Rotation().Yaw, 0.f);
				Pawn->SetActorRotation(FaceRot);
				if (AController* C = Pawn->GetController())
					C->SetControlRotation(FaceRot);
				Pawn->ForceNetUpdate();
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][Chase] DirectArrive: Enemy=%s Angle=%.0f DistTarget=%.1f DistShip=%.1f"),
				*GetNameSafe(Pawn), Data.AssignedAngleDeg, DistToTarget2D, DistToShip2D);
			return EStateTreeRunStatus::Running;
		}

		// Walk directly toward spread target via AddMovementInput
		const FVector ToTarget2D = (SpreadTarget - PawnLoc).GetSafeNormal2D();
		if (!ToTarget2D.IsNearlyZero())
		{
			FVector MoveDir = ToTarget2D;

			// When stuck in DirectMode, add perpendicular sidestep + Z boost to escape collision
			if (Data.ConsecutiveStuckCount >= DirectMoveStuckThreshold + 1)
			{
				// Perpendicular sidestep: alternate left/right based on stuck count
				const float SideSign = (Data.ConsecutiveStuckCount % 2 == 0) ? 1.f : -1.f;
				const FVector Side(- ToTarget2D.Y * SideSign, ToTarget2D.X * SideSign, 0.f);
				// Blend: 60% forward + 40% sidestep + Z boost
				MoveDir = (ToTarget2D * 0.6f + Side * 0.4f).GetSafeNormal();
				MoveDir.Z = 0.3f;
				MoveDir.Normalize();
			}
			else if (Data.ConsecutiveStuckCount > 0)
			{
				// Light Z boost to climb terrain
				MoveDir.Z = 0.2f;
				MoveDir.Normalize();
			}

			Pawn->AddMovementInput(MoveDir, 1.0f);

			// Face movement direction while walking
			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator MoveRot(0.f, ToTarget2D.Rotation().Yaw, 0.f);
			const FRotator NewRot = FMath::RInterpTo(CurrentRot, MoveRot, DeltaTime, 8.f);
			Pawn->SetActorRotation(NewRot);
			if (AController* C = Pawn->GetController())
				C->SetControlRotation(NewRot);
		}
		return EStateTreeRunStatus::Running;
	}

	// --- Normal phase movement ---
	if (Data.Phase == EChasePhase::Rush)
	{
		const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		if (bRepathDue || bIdle)
		{
			const FVector Goal = ComputeRushGoal(PawnLoc, ShipLoc,
				Data.AssignedAngleDeg, SpreadPhaseRadius, RushWaypointStep, Pawn->GetWorld());
			const bool bPathOk = IssueMoveToLocation(AIC, Goal, AcceptanceRadius);

			// Path failed -> NavMesh boundary. Instant DirectMode.
			if (bInstantDirectModeOnPathFail && !bPathOk && !Data.bDirectMoveMode)
			{
				Data.bDirectMoveMode = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][Chase] DirectMode ON (PathFail): Enemy=%s Phase=Rush Dist2D=%.1f"),
					*GetNameSafe(Pawn), DistToShip2D);
			}
			Data.TimeSinceRepath = 0.f;
		}

		// Rush: face movement direction, not ship
		const FVector Vel2D = Pawn->GetVelocity().GetSafeNormal2D();
		if (!Vel2D.IsNearlyZero())
		{
			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator MoveRot(0.f, Vel2D.Rotation().Yaw, 0.f);
			const FRotator NewRot = FMath::RInterpTo(CurrentRot, MoveRot, DeltaTime, 5.f);
			Pawn->SetActorRotation(NewRot);
			if (AController* C = Pawn->GetController())
				C->SetControlRotation(NewRot);
		}
	}
	else // Spread (with pathfinding)
	{
		const FVector SpreadGoal = ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, StandoffRadius, Pawn->GetWorld());
		const float DistToGoal2D = FVector::Dist2D(PawnLoc, SpreadGoal);

		if (DistToGoal2D <= AcceptanceRadius + 50.f)
		{
			Data.bSpreadArrived = true;
			AIC->StopMovement();
			AIC->SetFocus(Ship);

			const FVector ToShip = (ShipLoc - PawnLoc).GetSafeNormal2D();
			if (!ToShip.IsNearlyZero())
			{
				const FRotator FaceRot(0.f, ToShip.Rotation().Yaw, 0.f);
				Pawn->SetActorRotation(FaceRot);
				if (AController* C = Pawn->GetController())
					C->SetControlRotation(FaceRot);
				Pawn->ForceNetUpdate();
			}

			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][Chase] SpreadArrive: Enemy=%s Angle=%.0f DistGoal=%.1f DistShip=%.1f"),
				*GetNameSafe(Pawn), Data.AssignedAngleDeg, DistToGoal2D, DistToShip2D);
			return EStateTreeRunStatus::Running;
		}

		const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		if (bRepathDue || bIdle)
		{
			const bool bPathOk = IssueMoveToLocation(AIC, SpreadGoal, AcceptanceRadius);

			// Path failed -> NavMesh boundary. Instant DirectMode.
			if (bInstantDirectModeOnPathFail && !bPathOk && !Data.bDirectMoveMode)
			{
				Data.bDirectMoveMode = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][Chase] DirectMode ON (PathFail): Enemy=%s Phase=Spread Dist2D=%.1f"),
					*GetNameSafe(Pawn), DistToShip2D);
			}
			Data.TimeSinceRepath = 0.f;
		}

		FaceTarget(Pawn, Ship, DeltaTime, 10.f);
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// TickPlayerChase
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::TickPlayerChase(
	FInstanceDataType& Data, AAIController* AIC, APawn* Pawn,
	AActor* Target, float DeltaTime) const
{
	const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
	const bool bTargetChanged = Data.LastMoveTarget != Data.TargetData.TargetActor;

	bool bStuck = false;
	if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
	{
		const bool bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);
		const bool bSlow = (Pawn->GetVelocity().SizeSquared2D() < 50.f * 50.f);
		const bool bFar = (Data.TargetData.DistanceToTarget > (AcceptanceRadius + 150.f));
		bStuck = bIdle && bSlow && bFar;
	}

	if (bTargetChanged || bRepathDue || bStuck)
	{
		if (AttackPositionQuery && Data.TimeUntilNextEQS <= 0.f)
		{
			RunAttackPositionEQS(AttackPositionQuery, AIC, AcceptanceRadius, Target);
			Data.TimeUntilNextEQS = EQSInterval;
		}
		else
		{
			AIC->MoveToActor(Target, AcceptanceRadius, true, true, false, nullptr, true);
		}

		Data.LastMoveTarget = Data.TargetData.TargetActor;
		Data.TimeSinceRepath = 0.f;
	}

	FaceTarget(Pawn, Target, DeltaTime, 5.f);
	return EStateTreeRunStatus::Running;
}

// ============================================================================
// ExitState
// ============================================================================
void FSTTask_ChaseTarget::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (AAIController* AIC = Data.AIController)
		AIC->StopMovement();
}
