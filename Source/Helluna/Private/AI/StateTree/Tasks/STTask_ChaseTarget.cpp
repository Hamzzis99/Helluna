/**
 * STTask_ChaseTarget.cpp
 *
 * [ChaseSimpV1][LCv14] Natural Surround + Detour
 *
 * Rush:   Aim for ring point at assigned angle -> different NavMesh paths
 * Spread: Move to standoff ring at assigned angle. On arrival -> Stop + Face ship.
 *         새 몬스터가 오면 다른 각도 → 자연스러운 에워싸기.
 *         우주선이 SpreadReEngageMargin 이상 움직이면 재추격.
 * Stuck:  Rotate angle. After N consecutive stucks, disable pathfinding
 *         and use AddMovementInput with perpendicular sidestep (우회).
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
#include "Object/ResourceUsingObject/ResourceUsingObject_AttackTurret.h"
#include "Object/ResourceUsingObject/HellunaTurretBase.h"
#include "AI/TurretAggroTracker.h"

static int32 GAngleCounter = 0;

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

/**
 * Spread goal: angle position at ApproachRadius from ship.
 * 각 몬스터가 서로 다른 각도로 접근해 한 점에 몰리지 않게 함.
 * 도착 판정은 하지 않음 — 목표점이 콜리전 안이어도 OK.
 * AttackZone Condition이 공격 돌입을 담당.
 */
FVector ComputeSpreadGoal(
	const FVector& ShipLoc,
	float AssignedAngleDeg,
	float ApproachRadius,
	UWorld* World)
{
	const FVector RawGoal = AngleToWorldPos(ShipLoc, AssignedAngleDeg, ApproachRadius);
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

/** 도착 처리 공통: 정지 -> 타겟 주시 -> 회전 -> 네트워크 갱신 */
void HandleArrival(AAIController* AIC, APawn* Pawn, AActor* FocusTarget)
{
	if (!AIC || !Pawn || !FocusTarget) return;
	AIC->StopMovement();
	AIC->SetFocus(FocusTarget);

	const FVector ToTarget = (FocusTarget->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (!ToTarget.IsNearlyZero())
	{
		const FRotator FaceRot(0.f, ToTarget.Rotation().Yaw, 0.f);
		Pawn->SetActorRotation(FaceRot);
		if (AController* C = Pawn->GetController())
			C->SetControlRotation(FaceRot);
		Pawn->ForceNetUpdate();
	}
}

} // namespace ChaseHelpers


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
	// 공격/회복 포탑 모두 동일한 터렛 추격 분기로 — 공통 부모 AHellunaTurretBase 기준.
	const bool bIsTurret = (Cast<AHellunaTurretBase>(Target) != nullptr);

	// Reset
	Data.TimeSinceRepath = 0.f;
	Data.TimeUntilNextEQS = 0.f;
	Data.StuckCheckTimer = 0.f;
	Data.ConsecutiveStuckCount = 0;
	Data.bDirectMoveMode = false;
	Data.bSpreadArrived = false;
	Data.SidestepSign = 0;
	Data.DiagTimer = 0.f;
	Data.LastCheckedLocation = Pawn->GetActorLocation();
	Data.PlayerStuckCount = 0;
	Data.PlayerStuckTimer = 0.f;
	Data.PlayerLastCheckedLocation = Pawn->GetActorLocation();

	if (bIsShip)
	{
		const int32 AngleIndex = GAngleCounter++;
		Data.AssignedAngleDeg = FMath::Fmod(AngleIndex * 60.f, 360.f);

		const float DistToShip2D = FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation());

		if (DistToShip2D > SpreadPhaseRadius)
		{
			Data.Phase = EChasePhase::Rush;
			const FVector Goal = ChaseHelpers::ComputeRushGoal(
				Pawn->GetActorLocation(), Target->GetActorLocation(),
				Data.AssignedAngleDeg, SpreadPhaseRadius, RushWaypointStep,
				Pawn->GetWorld());
			ChaseHelpers::IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
		}
		else
		{
			// [ChaseSimpV1][LCv14] Spread: 각도별 링 포인트로 접근 → 도착 시 정지 + 타겟 주시
			Data.Phase = EChasePhase::Spread;
			const FVector Goal = ChaseHelpers::ComputeSpreadGoal(
				Target->GetActorLocation(), Data.AssignedAngleDeg, SpreadApproachRadius,
				Pawn->GetWorld());
			ChaseHelpers::IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[ChaseSimpV1][LCv14] Enter: Enemy=%s Angle=%.0f Phase=%s Dist2D=%.1f"),
			*GetNameSafe(Pawn), Data.AssignedAngleDeg,
			Data.Phase == EChasePhase::Rush ? TEXT("Rush") : TEXT("Spread"),
			DistToShip2D);
	}
	else if (bIsTurret)
	{
		// 터렛 산개: TurretAggroTracker에서 할당된 각도로 접근
		const float Angle = FTurretAggroTracker::GetAssignedAngle(Target, Pawn);
		Data.AssignedAngleDeg = (Angle >= 0.f) ? Angle : FMath::FRandRange(0.f, 360.f);

		const FVector TurretLoc = Target->GetActorLocation();
		const FVector SpreadGoal = ChaseHelpers::AngleToWorldPos(TurretLoc, Data.AssignedAngleDeg, TurretStandoffRadius);
		const FVector NavGoal = ChaseHelpers::ProjectToNav(Pawn->GetWorld(), SpreadGoal);
		ChaseHelpers::IssueMoveToLocation(AIC, NavGoal, AcceptanceRadius);
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
	if (!Target) return EStateTreeRunStatus::Failed;

	// Stop if attacking
	static const FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
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
	// 공격/회복 포탑 모두 동일한 터렛 추격 분기로 — 공통 부모 AHellunaTurretBase 기준.
	const bool bIsTurret = (Cast<AHellunaTurretBase>(Target) != nullptr);

	if (bIsShip)
		return TickShipChase(Context, Data, AIC, Pawn, Target, DeltaTime);
	else if (bIsTurret)
		return TickTurretChase(Data, AIC, Pawn, Target, DeltaTime);
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
			TEXT("[ChaseSimpV1][LCv14] Tick: Enemy=%s Phase=%s Angle=%.0f Dist2D=%.1f Vel=%.1f StuckCnt=%d Direct=%d Arrived=%d PFC=%s"),
			*GetNameSafe(Pawn),
			Data.Phase == EChasePhase::Rush ? TEXT("Rush") : TEXT("Spread"),
			Data.AssignedAngleDeg, DistToShip2D,
			Pawn->GetVelocity().Size2D(),
			Data.ConsecutiveStuckCount,
			(int)Data.bDirectMoveMode,
			(int)Data.bSpreadArrived,
			*PFCStatus);
	}

	// --- Phase transition: Rush -> Spread ---
	if (Data.Phase == EChasePhase::Rush && DistToShip2D <= SpreadPhaseRadius)
	{
		Data.Phase = EChasePhase::Spread;
		Data.ConsecutiveStuckCount = 0;
		Data.StuckCheckTimer = 0.f;
		Data.LastCheckedLocation = PawnLoc;
		Data.bSpreadArrived = false;

		if (!Data.bDirectMoveMode)
		{
			const FVector Goal = ChaseHelpers::ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, SpreadApproachRadius, Pawn->GetWorld());
			ChaseHelpers::IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[ChaseSimpV1][LCv14] Rush->Spread: Enemy=%s Angle=%.0f Direct=%d Dist2D=%.1f"),
			*GetNameSafe(Pawn), Data.AssignedAngleDeg, (int)Data.bDirectMoveMode, DistToShip2D);
		return EStateTreeRunStatus::Running;
	}

	// --- Spread 도착 판정: 링 포인트에 도달하면 정지 + 타겟 주시 (자연스러운 에워싸기) ---
	if (Data.Phase == EChasePhase::Spread && !Data.bSpreadArrived)
	{
		const bool bAtStandoff = (DistToShip2D <= SpreadApproachRadius + AcceptanceRadius);
		if (bAtStandoff)
		{
			Data.bSpreadArrived = true;
			Data.bDirectMoveMode = false;
			Data.ConsecutiveStuckCount = 0;
			Data.SidestepSign = 0;
			ChaseHelpers::HandleArrival(AIC, Pawn, Ship);

			UE_LOG(LogTemp, Warning,
				TEXT("[ChaseSimpV1][LCv14] SpreadArrive: Enemy=%s Angle=%.0f Dist2D=%.1f"),
				*GetNameSafe(Pawn), Data.AssignedAngleDeg, DistToShip2D);
			return EStateTreeRunStatus::Running;
		}
	}

	// --- Spread 도착 상태 유지: 우주선 드리프트 시 재추격 ---
	if (Data.Phase == EChasePhase::Spread && Data.bSpreadArrived)
	{
		const bool bShipDrifted = (DistToShip2D > SpreadApproachRadius + SpreadReEngageMargin);
		if (bShipDrifted)
		{
			Data.bSpreadArrived = false;
			Data.SidestepSign = 0;
			if (AIC->GetFocusActor()) AIC->ClearFocus(EAIFocusPriority::Gameplay);
			Data.StuckCheckTimer = 0.f;
			Data.ConsecutiveStuckCount = 0;
			Data.LastCheckedLocation = PawnLoc;

			const FVector Goal = ChaseHelpers::ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, SpreadApproachRadius, Pawn->GetWorld());
			ChaseHelpers::IssueMoveToLocation(AIC, Goal, AcceptanceRadius);

			UE_LOG(LogTemp, Warning,
				TEXT("[ChaseSimpV1][LCv14] ReEngage: Enemy=%s Dist2D=%.1f Margin=%.1f"),
				*GetNameSafe(Pawn), DistToShip2D, SpreadReEngageMargin);
			return EStateTreeRunStatus::Running;
		}

		// 도착 상태: 우주선 주시만. StopMovement는 HandleArrival에서 이미 수행.
		ChaseHelpers::FaceTarget(Pawn, Ship, DeltaTime, 10.f);
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
					TEXT("[ChaseSimpV1][LCv14] DirectMode ON: Enemy=%s StuckCnt=%d Dist2D=%.1f"),
					*GetNameSafe(Pawn), Data.ConsecutiveStuckCount, DistToShip2D);
			}

			// 각도 회전으로 다른 NavMesh 경로 유도
			Data.AssignedAngleDeg = FMath::Fmod(Data.AssignedAngleDeg + AngleRotationOnStuck, 360.f);

			UE_LOG(LogTemp, Warning,
				TEXT("[ChaseSimpV1][LCv14] STUCK: Enemy=%s Moved=%.1f NewAngle=%.0f StuckCnt=%d Direct=%d Phase=%s Dist2D=%.1f"),
				*GetNameSafe(Pawn), Moved, Data.AssignedAngleDeg,
				Data.ConsecutiveStuckCount, (int)Data.bDirectMoveMode,
				Data.Phase == EChasePhase::Rush ? TEXT("Rush") : TEXT("Spread"),
				DistToShip2D);

			// Reissue movement (DirectMode uses AddMovementInput in tick, no MoveTo needed)
			if (Data.bDirectMoveMode)
			{
				// Direction handled by AddMovementInput toward ShipLoc next tick
			}
			else if (Data.Phase == EChasePhase::Rush)
			{
				const FVector Goal = ChaseHelpers::ComputeRushGoal(PawnLoc, ShipLoc,
					Data.AssignedAngleDeg, SpreadPhaseRadius, RushWaypointStep, Pawn->GetWorld());
				ChaseHelpers::IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
			}
			else
			{
				const FVector Goal = ChaseHelpers::ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, SpreadApproachRadius, Pawn->GetWorld());
				ChaseHelpers::IssueMoveToLocation(AIC, Goal, AcceptanceRadius);
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
	// [ChaseSimpV1][LCv14] 각도 기반 링 포인트로 밀어붙이되, 끼이면 수직 사이드스텝으로 우회.
	// 충분히 가까우면 직접 도착 처리 → 자연스러운 에워싸기.
	if (Data.bDirectMoveMode)
	{
		// Stop pathfollowing once on first DirectMode tick
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
		{
			if (PFC->GetStatus() != EPathFollowingStatus::Idle)
				AIC->StopMovement();
		}

		// DirectMode에서도 링 반경 근처 도달 시 강제 도착 → 길찾기 끊긴 채로도 에워싸기 성립
		if (DistToShip2D <= SpreadApproachRadius + DirectModeForceArriveThreshold)
		{
			Data.bSpreadArrived = true;
			Data.bDirectMoveMode = false;
			Data.ConsecutiveStuckCount = 0;
			Data.SidestepSign = 0;
			ChaseHelpers::HandleArrival(AIC, Pawn, Ship);

			UE_LOG(LogTemp, Warning,
				TEXT("[ChaseSimpV1][LCv14] DirectArrive: Enemy=%s Angle=%.0f Dist2D=%.1f"),
				*GetNameSafe(Pawn), Data.AssignedAngleDeg, DistToShip2D);
			return EStateTreeRunStatus::Running;
		}

		// Target = angle-based approach point (각도 분산 유지)
		const FVector ApproachPoint = ChaseHelpers::AngleToWorldPos(ShipLoc, Data.AssignedAngleDeg, SpreadApproachRadius);
		const FVector ToApproach2D = (ApproachPoint - PawnLoc).GetSafeNormal2D();
		if (!ToApproach2D.IsNearlyZero())
		{
			FVector MoveDir = ToApproach2D;

			// 끼임이 계속되면 수직 방향 혼합 → 한 방향 고정 우회 (한 번 정해지면 유지)
			if (Data.ConsecutiveStuckCount >= DirectMoveStuckThreshold + 1)
			{
				if (Data.SidestepSign == 0)
				{
					Data.SidestepSign = FMath::RandBool() ? 1 : -1;
					UE_LOG(LogTemp, Warning,
						TEXT("[ChaseSimpV1][LCv14] Sidestep COMMIT: Enemy=%s Sign=%d"),
						*GetNameSafe(Pawn), (int)Data.SidestepSign);
				}
				const float SideSign = (float)Data.SidestepSign;
				const FVector Side(-ToApproach2D.Y * SideSign, ToApproach2D.X * SideSign, 0.f);
				MoveDir = (ToApproach2D * 0.6f + Side * 0.4f).GetSafeNormal2D();
			}

			Pawn->AddMovementInput(MoveDir, 1.0f);

			// Face movement direction while walking
			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator MoveRot(0.f, ToApproach2D.Rotation().Yaw, 0.f);
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
			const FVector Goal = ChaseHelpers::ComputeRushGoal(PawnLoc, ShipLoc,
				Data.AssignedAngleDeg, SpreadPhaseRadius, RushWaypointStep, Pawn->GetWorld());
			const bool bPathOk = ChaseHelpers::IssueMoveToLocation(AIC, Goal, AcceptanceRadius);

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
	else // Spread: 각도 기반 접근, 도착 판정은 InAttackZone Condition이 담당
	{
		const FVector SpreadGoal = ChaseHelpers::ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, SpreadApproachRadius, Pawn->GetWorld());

		const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		if (bRepathDue || bIdle)
		{
			const bool bPathOk = ChaseHelpers::IssueMoveToLocation(AIC, SpreadGoal, AcceptanceRadius);

			// Path failed -> NavMesh boundary. Instant DirectMode.
			if (bInstantDirectModeOnPathFail && !bPathOk && !Data.bDirectMoveMode)
			{
				Data.bDirectMoveMode = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[ChaseSimpV1][LCv14] DirectMode ON (PathFail): Enemy=%s Phase=Spread Dist2D=%.1f"),
					*GetNameSafe(Pawn), DistToShip2D);
			}
			Data.TimeSinceRepath = 0.f;
		}

		// 이동 방향 주시 (멈춰있을 때는 우주선 주시 — AttackZone 정면 박스 정확도↑)
		const FVector Vel2D = Pawn->GetVelocity().GetSafeNormal2D();
		if (!Vel2D.IsNearlyZero())
		{
			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator MoveRot(0.f, Vel2D.Rotation().Yaw, 0.f);
			const FRotator NewRot = FMath::RInterpTo(CurrentRot, MoveRot, DeltaTime, 8.f);
			Pawn->SetActorRotation(NewRot);
			if (AController* C = Pawn->GetController())
				C->SetControlRotation(NewRot);
		}
		else
		{
			ChaseHelpers::FaceTarget(Pawn, Ship, DeltaTime, 10.f);
		}
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
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector TargetLoc = Target->GetActorLocation();
	const float DistToTarget2D = FVector::Dist2D(PawnLoc, TargetLoc);

	// --- Player 끼임 감지 ---
	Data.PlayerStuckTimer += DeltaTime;
	if (Data.PlayerStuckTimer >= StuckCheckInterval)
	{
		Data.PlayerStuckTimer = 0.f;
		const float Moved = FVector::Dist2D(PawnLoc, Data.PlayerLastCheckedLocation);
		const bool bFar = (DistToTarget2D > AcceptanceRadius + 150.f);

		if (Moved < StuckDistThreshold && bFar)
			Data.PlayerStuckCount++;
		else
			Data.PlayerStuckCount = 0;

		Data.PlayerLastCheckedLocation = PawnLoc;
	}

	// --- Player DirectMode: 끼임 N회 이상 -> AddMovementInput ---
	if (Data.PlayerStuckCount >= PlayerDirectMoveThreshold)
	{
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
		{
			if (PFC->GetStatus() != EPathFollowingStatus::Idle)
				AIC->StopMovement();
		}

		const FVector ToTarget2D = (TargetLoc - PawnLoc).GetSafeNormal2D();
		if (!ToTarget2D.IsNearlyZero())
		{
			Pawn->AddMovementInput(ToTarget2D, 1.0f);

			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator MoveRot(0.f, ToTarget2D.Rotation().Yaw, 0.f);
			const FRotator NewRot = FMath::RInterpTo(CurrentRot, MoveRot, DeltaTime, 8.f);
			Pawn->SetActorRotation(NewRot);
			if (AController* C = Pawn->GetController())
				C->SetControlRotation(NewRot);
		}
		return EStateTreeRunStatus::Running;
	}

	// --- Normal pathfinding ---
	const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
	const bool bTargetChanged = Data.LastMoveTarget != Data.TargetData.TargetActor;

	bool bIdle = false;
	if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
		bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

	if (bTargetChanged || bRepathDue || bIdle)
	{
		if (AttackPositionQuery && Data.TimeUntilNextEQS <= 0.f)
		{
			ChaseHelpers::RunAttackPositionEQS(AttackPositionQuery, AIC, AcceptanceRadius, Target);
			Data.TimeUntilNextEQS = EQSInterval;
		}
		else
		{
			AIC->MoveToActor(Target, AcceptanceRadius, true, true, false, nullptr, true);
		}

		Data.LastMoveTarget = Data.TargetData.TargetActor;
		Data.TimeSinceRepath = 0.f;
	}

	// 이동 중에는 이동 방향, 멈춰있으면 타겟 방향
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
	else
	{
		ChaseHelpers::FaceTarget(Pawn, Target, DeltaTime, 5.f);
	}

	return EStateTreeRunStatus::Running;
}

// ============================================================================
// TickTurretChase — 터렛을 에워싸며 접근 (Ship과 동일한 ring arrival 패턴)
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget::TickTurretChase(
	FInstanceDataType& Data, AAIController* AIC, APawn* Pawn,
	AActor* Turret, float DeltaTime) const
{
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector TurretLoc = Turret->GetActorLocation();
	const float DistToTurret2D = FVector::Dist2D(PawnLoc, TurretLoc);

	// 각 몬스터의 고유 각도 링 포인트
	const FVector RingGoal = ChaseHelpers::AngleToWorldPos(TurretLoc, Data.AssignedAngleDeg, TurretStandoffRadius);
	const float DistToRing2D = FVector::Dist2D(PawnLoc, RingGoal);

	// --- 도착 상태 유지: 터렛 이동 감지 시 재추격 ---
	if (Data.bSpreadArrived)
	{
		const bool bTurretDrifted = (DistToTurret2D > TurretStandoffRadius + SpreadReEngageMargin);
		if (bTurretDrifted)
		{
			Data.bSpreadArrived = false;
			Data.SidestepSign = 0;
			if (AIC->GetFocusActor()) AIC->ClearFocus(EAIFocusPriority::Gameplay);
			Data.ConsecutiveStuckCount = 0;
			Data.LastCheckedLocation = PawnLoc;
			const FVector NavGoal = ChaseHelpers::ProjectToNav(Pawn->GetWorld(), RingGoal);
			ChaseHelpers::IssueMoveToLocation(AIC, NavGoal, AcceptanceRadius);
			return EStateTreeRunStatus::Running;
		}
		ChaseHelpers::FaceTarget(Pawn, Turret, DeltaTime, 10.f);
		return EStateTreeRunStatus::Running;
	}

	// --- Ring 도착 판정: 반드시 자기 각도 위치까지 가야 도착 → 자연스러운 에워싸기 ---
	if (DistToRing2D <= AcceptanceRadius + 50.f)
	{
		Data.bSpreadArrived = true;
		Data.bDirectMoveMode = false;
		Data.ConsecutiveStuckCount = 0;
		Data.SidestepSign = 0;
		ChaseHelpers::HandleArrival(AIC, Pawn, Turret);

		UE_LOG(LogTemp, Warning,
			TEXT("[ChaseSimpV1][LCv14] Turret Arrive: Enemy=%s Angle=%.0f Dist=%.1f"),
			*GetNameSafe(Pawn), Data.AssignedAngleDeg, DistToRing2D);
		return EStateTreeRunStatus::Running;
	}

	// --- 끼임 감지 ---
	Data.StuckCheckTimer += DeltaTime;
	if (Data.StuckCheckTimer >= StuckCheckInterval)
	{
		Data.StuckCheckTimer = 0.f;
		const float Moved = FVector::Dist2D(PawnLoc, Data.LastCheckedLocation);
		if (Moved < StuckDistThreshold)
		{
			Data.ConsecutiveStuckCount++;
			if (!Data.bDirectMoveMode && Data.ConsecutiveStuckCount >= DirectMoveStuckThreshold)
			{
				Data.bDirectMoveMode = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[ChaseSimpV1][LCv14] Turret DirectMode ON: Enemy=%s StuckCnt=%d"),
					*GetNameSafe(Pawn), Data.ConsecutiveStuckCount);
			}
			Data.AssignedAngleDeg = FMath::Fmod(Data.AssignedAngleDeg + AngleRotationOnStuck, 360.f);
		}
		Data.LastCheckedLocation = PawnLoc;
	}

	// --- DirectMode: 각도 링 포인트로 직접 밀어붙이되 수직 사이드스텝으로 우회 ---
	if (Data.bDirectMoveMode)
	{
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
		{
			if (PFC->GetStatus() != EPathFollowingStatus::Idle)
				AIC->StopMovement();
		}

		// DirectMode 강제 도착 — 경로 끊긴 상태라도 링 근처 오면 ring 포착
		if (DistToRing2D <= AcceptanceRadius + DirectModeForceArriveThreshold)
		{
			Data.bSpreadArrived = true;
			Data.bDirectMoveMode = false;
			Data.ConsecutiveStuckCount = 0;
			Data.SidestepSign = 0;
			ChaseHelpers::HandleArrival(AIC, Pawn, Turret);
			UE_LOG(LogTemp, Warning,
				TEXT("[ChaseSimpV1][LCv14] Turret DirectArrive: Enemy=%s Angle=%.0f"),
				*GetNameSafe(Pawn), Data.AssignedAngleDeg);
			return EStateTreeRunStatus::Running;
		}

		const FVector ToRing2D = (RingGoal - PawnLoc).GetSafeNormal2D();
		if (!ToRing2D.IsNearlyZero())
		{
			FVector MoveDir = ToRing2D;
			if (Data.ConsecutiveStuckCount >= DirectMoveStuckThreshold + 1)
			{
				if (Data.SidestepSign == 0)
				{
					Data.SidestepSign = FMath::RandBool() ? 1 : -1;
					UE_LOG(LogTemp, Warning,
						TEXT("[ChaseSimpV1][LCv14] Turret Sidestep COMMIT: Enemy=%s Sign=%d"),
						*GetNameSafe(Pawn), (int)Data.SidestepSign);
				}
				const float SideSign = (float)Data.SidestepSign;
				const FVector Side(-ToRing2D.Y * SideSign, ToRing2D.X * SideSign, 0.f);
				MoveDir = (ToRing2D * 0.6f + Side * 0.4f).GetSafeNormal2D();
			}
			Pawn->AddMovementInput(MoveDir, 1.0f);

			const FRotator CurrentRot = Pawn->GetActorRotation();
			const FRotator MoveRot(0.f, ToRing2D.Rotation().Yaw, 0.f);
			const FRotator NewRot = FMath::RInterpTo(CurrentRot, MoveRot, DeltaTime, 8.f);
			Pawn->SetActorRotation(NewRot);
			if (AController* C = Pawn->GetController())
				C->SetControlRotation(NewRot);
		}
		return EStateTreeRunStatus::Running;
	}

	// --- Normal pathfinding to ring point ---
	const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
	bool bIdle = false;
	if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
		bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

	if (bRepathDue || bIdle)
	{
		const FVector NavGoal = ChaseHelpers::ProjectToNav(Pawn->GetWorld(), RingGoal);
		const bool bPathOk = ChaseHelpers::IssueMoveToLocation(AIC, NavGoal, AcceptanceRadius);
		if (bInstantDirectModeOnPathFail && !bPathOk && !Data.bDirectMoveMode)
		{
			Data.bDirectMoveMode = true;
		}
		Data.TimeSinceRepath = 0.f;
	}

	// Face movement direction
	const FVector Vel2D = Pawn->GetVelocity().GetSafeNormal2D();
	if (!Vel2D.IsNearlyZero())
	{
		const FRotator CurrentRot = Pawn->GetActorRotation();
		const FRotator MoveRot(0.f, Vel2D.Rotation().Yaw, 0.f);
		const FRotator NewRot = FMath::RInterpTo(CurrentRot, MoveRot, DeltaTime, 8.f);
		Pawn->SetActorRotation(NewRot);
		if (AController* C = Pawn->GetController())
			C->SetControlRotation(NewRot);
	}
	else
	{
		ChaseHelpers::FaceTarget(Pawn, Turret, DeltaTime, 5.f);
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
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (AAIController* AIC = Data.AIController)
		AIC->StopMovement();
}
