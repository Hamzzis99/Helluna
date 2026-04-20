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
#include "AI/HellunaAIAttackZone.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "AITypes.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "GameFramework/Pawn.h"

#include "Character/HellunaEnemyCharacter.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
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

/**
 * [ChaseBlockDetourV1] AttackZone 앞쪽 박스에 다른 Enemy 가 있는지 검사.
 *   "내 앞에 같은 편 몬스터가 있다" = "이 경로는 막혔다" → 우회 트리거 용도.
 *   Pawn Object channel 만 조회해 정적 콜리전은 무시.
 */
static bool IsPathBlockedByOtherEnemy(
	APawn* Pawn, AActor* TargetToIgnore,
	const FVector& HalfExtent, float ForwardOffset)
{
	if (!Pawn) return false;
	UWorld* World = Pawn->GetWorld();
	if (!World) return false;

	const FVector PawnLoc = Pawn->GetActorLocation();
	const FQuat PawnRot = Pawn->GetActorQuat();
	const FVector Forward = PawnRot.GetForwardVector();
	const FVector BoxCenter = PawnLoc + Forward * ForwardOffset;
	const FCollisionShape BoxShape = FCollisionShape::MakeBox(HalfExtent);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Pawn);
	if (TargetToIgnore) Params.AddIgnoredActor(TargetToIgnore);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps, BoxCenter, PawnRot,
		FCollisionObjectQueryParams(ECC_Pawn),
		BoxShape, Params);

	for (const FOverlapResult& Ov : Overlaps)
	{
		AActor* A = Ov.GetActor();
		if (!A || A == Pawn || A == TargetToIgnore) continue;
		if (Cast<AHellunaEnemyCharacter>(A)) return true;
	}
	return false;
}

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

/**
 * [ChaseVelocityPreserveV1] MoveTo 재발급 시 CharacterMovement Velocity 를 보존.
 *   AIC->MoveTo 내부에서 기존 Path 폐기 시 StopMovementImmediately 호출되어 Velocity=0 됨.
 *   → 다음 Tick 에 PathFollowing 이 MaxSpeed 설정하지만 1~2 Frame Velocity 0 구간이 애니 Blend 끊김 원인.
 *   저장/복원으로 그 1 Frame 속도 유지 → AnimBP Speed 값 연속성 보장.
 */
static bool IssueMoveToLocationKeepSpeed(AAIController* AIC, APawn* Pawn,
	const FVector& Goal, float Radius, bool bUsePathfinding = true)
{
	if (!AIC || !Pawn) return false;

	UCharacterMovementComponent* CMC = nullptr;
	FVector SavedVel = FVector::ZeroVector;
	if (ACharacter* Ch = Cast<ACharacter>(Pawn))
	{
		CMC = Ch->GetCharacterMovement();
		if (CMC) SavedVel = CMC->Velocity;
	}

	const bool bOk = IssueMoveToLocation(AIC, Goal, Radius, bUsePathfinding);

	// MoveTo 가 내부적으로 Velocity 를 0 으로 reset 했을 수 있으므로 기존 속도로 복원.
	// PathFollowing 이 다음 Tick 에 새 방향으로 Velocity 재설정하므로 여기선 1 프레임 유지만 하면 됨.
	if (CMC && !SavedVel.IsNearlyZero())
	{
		CMC->Velocity = SavedVel;
	}
	return bOk;
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

/**
 * [AutoApproachRadiusV1] Target Bounds 기반 자동 접근 반경 계산.
 * 작은 타워는 감싸듯 가까이, 큰 우주선은 원본 반경 유지.
 *  - AutoRadius = max(Bounds.X, Bounds.Y) + GapBeyondBounds
 *  - 사용자 SpreadApproachRadius 가 더 작으면 그 값 사용 (수동 override 존중)
 *  - 결과: min(AutoRadius, UserRadius)
 *    • 큰 우주선(Bounds.XY=600) → AutoRadius=750 > UserRadius(500) → UserRadius 사용 (변화 없음)
 *    • 작은 타워(Bounds.XY=90)  → AutoRadius=240 < UserRadius(500) → AutoRadius 사용 (감싸듯)
 */
float GetEffectiveApproachRadius(const AActor* Target, float UserSetRadius)
{
	if (!Target) return UserSetRadius;
	FVector Origin, Extent;
	Target->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent, /*bIncludeFromChildActors=*/true);
	const float TargetXY = FMath::Max(Extent.X, Extent.Y);
	const float GapBeyondBounds = 150.f; // 목표 경계에서 이 거리만큼 바깥에서 접근
	const float AutoRadius = TargetXY + GapBeyondBounds;
	// 최소 100 하한 (너무 붙으면 navmesh 정사영 실패)
	const float Clamped = FMath::Max(AutoRadius, 100.f);
	return FMath::Min(Clamped, UserSetRadius);
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

	// [ArrivalStopV1] StopMovement 는 PathFollowing 정지만 담당. CharacterMovement 의
	//   Velocity / PendingInput 이 남아있으면 관성으로 몇 프레임 더 미끄러져 움찔거림.
	//   몬스터들끼리의 Capsule Block 과 맞물려 경계선에서 왕복 진동이 발생하므로 즉시 0.
	if (ACharacter* Ch = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
		}
	}

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
			// [ChaseKeepSpeedV2] 모든 비-DirectMode MoveTo 발급을 KeepSpeed 로 → Idle 포즈 슬라이드 제거.
			ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, Goal, AcceptanceRadius);
		}
		else
		{
			// [ChaseSimpV1][LCv14] Spread: 각도별 링 포인트로 접근 → 도착 시 정지 + 타겟 주시
			Data.Phase = EChasePhase::Spread;
			const FVector Goal = ChaseHelpers::ComputeSpreadGoal(
				Target->GetActorLocation(), Data.AssignedAngleDeg, SpreadApproachRadius,
				Pawn->GetWorld());
			ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, Goal, AcceptanceRadius);
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
		ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, NavGoal, AcceptanceRadius);
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
	// [AutoApproachRadiusV1] 타겟 크기 기반 자동 조정 반경.
	// 작은 타겟(타워)은 감싸듯 가까이, 큰 우주선은 원본 반경 유지.
	const float EffApproachRadius = ChaseHelpers::GetEffectiveApproachRadius(Ship, SpreadApproachRadius);

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
	// [ChaseNoArrivalV2] Spread Phase 진입 시 Ring Goal 로 MoveTo.
	//   Target 본인 위치 MoveTo 는 NavMesh 도달 불가(AcceptanceRadius 50 이내 불가)로 PathFollowing 이 무한 Running →
	//   앞 Capsule 에 박힌 채 AddMovementInput 반복으로 멈칫거림 발생. Ring Goal 은 NavMesh 도달 가능.
	if (Data.Phase == EChasePhase::Rush && DistToShip2D <= SpreadPhaseRadius)
	{
		Data.Phase = EChasePhase::Spread;
		Data.ConsecutiveStuckCount = 0;
		Data.StuckCheckTimer = 0.f;
		Data.LastCheckedLocation = PawnLoc;
		Data.bSpreadArrived = false;

		if (!Data.bDirectMoveMode)
		{
			const FVector RingGoal = ChaseHelpers::AngleToWorldPos(
				ShipLoc, Data.AssignedAngleDeg, EffApproachRadius);
			ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, RingGoal, AcceptanceRadius);
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[ChaseNoArrivalV2] Rush->Spread: Enemy=%s Angle=%.0f Dist2D=%.1f → MoveTo RingGoal"),
			*GetNameSafe(Pawn), Data.AssignedAngleDeg, DistToShip2D);
		return EStateTreeRunStatus::Running;
	}

	// [ChaseBlockDetourV2] AttackZone 기반 차단 감지 + 쿨다운. 앞에 적 있으면 각도 회전.
	Data.BlockDetourCooldown = FMath::Max(0.f, Data.BlockDetourCooldown - DeltaTime);
	if (Data.Phase == EChasePhase::Spread && !Data.bDirectMoveMode && Data.BlockDetourCooldown <= 0.f)
	{
		Data.BlockCheckTimer += DeltaTime;
		if (Data.BlockCheckTimer >= BlockCheckInterval)
		{
			Data.BlockCheckTimer = 0.f;
			if (ChaseHelpers::IsPathBlockedByOtherEnemy(
				Pawn, Ship, AttackZoneHalfExtent, AttackZoneForwardOffset))
			{
				const float Rotate = FMath::RandRange(BlockDetourAngleMin, BlockDetourAngleMax)
					* (FMath::RandBool() ? 1.f : -1.f);
				Data.AssignedAngleDeg = FMath::Fmod(Data.AssignedAngleDeg + Rotate + 360.f, 360.f);
				const FVector RingGoal = ChaseHelpers::AngleToWorldPos(
					ShipLoc, Data.AssignedAngleDeg, EffApproachRadius);
				ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, RingGoal, AcceptanceRadius);
				UE_LOG(LogTemp, Warning,
					TEXT("[ChaseBlockDetourV2] Ship Blocked: Enemy=%s NewAngle=%.0f Rotate=%+.0f CD=%.2f"),
					*GetNameSafe(Pawn), Data.AssignedAngleDeg, Rotate, BlockDetourCooldownDuration);
				Data.RepathTimer = 0.f;
				Data.BlockDetourCooldown = BlockDetourCooldownDuration;
				return EStateTreeRunStatus::Running;
			}
		}
	}

	// [ChaseRepathMinV1] 주기적 Repath 제거 — PathFollowing 이 Moving 중이면 재발급 안 함.
	if (Data.Phase == EChasePhase::Spread && !Data.bDirectMoveMode)
	{
		UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent();
		const bool bIdle = !PFC || PFC->GetStatus() == EPathFollowingStatus::Idle;
		if (bIdle)
		{
			const FVector RingGoal = ChaseHelpers::AngleToWorldPos(
				ShipLoc, Data.AssignedAngleDeg, EffApproachRadius);
			ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, RingGoal, AcceptanceRadius);
		}
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
				ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, Goal, AcceptanceRadius);
			}
			else
			{
				const FVector Goal = ChaseHelpers::ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, EffApproachRadius, Pawn->GetWorld());
				ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, Goal, AcceptanceRadius);
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

		// [ChaseNoArrivalV1] DirectMode 도 Arrival 제거. 계속 Target 방향으로 힘 적용.
		//   도착은 StateTree InAttackZone Condition 이 Attack 전이로 처리.

		// Target = Ship 본인 쪽으로 직진 (NavMesh 무시). 각도 분산은 Rush phase 로 이미 수행됨.
		const FVector ApproachPoint = ShipLoc;
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
		// [ChaseKeepSpeedV2] Rush 도 bIdle||bRepathDue → bRepathDue 만으로 축소 + KeepSpeed.
		//   bIdle 재발급은 웨이포인트 도달 시 필요하므로 유지해야 하지만, KeepSpeed 로 Vel 보존.
		//   이전 비-KeepSpeed 호출이 웨이포인트 도달마다 Vel=0 리셋 → Idle 포즈 슬라이드 유발.
		const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		if (bRepathDue || bIdle)
		{
			const FVector Goal = ChaseHelpers::ComputeRushGoal(PawnLoc, ShipLoc,
				Data.AssignedAngleDeg, SpreadPhaseRadius, RushWaypointStep, Pawn->GetWorld());
			const bool bPathOk = ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, Goal, AcceptanceRadius);

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
		const FVector SpreadGoal = ChaseHelpers::ComputeSpreadGoal(ShipLoc, Data.AssignedAngleDeg, EffApproachRadius, Pawn->GetWorld());

		// [ChaseKeepSpeedV2] bIdle 은 위 ChaseRepathMinV1 블록이 처리. 여기는 RepathInterval 주기만.
		//   bIdle||bRepathDue 이전 로직은 매 0.5s (또는 RepathInterval) 마다 Vel=0 리셋하여
		//   0→MaxSpeed 가속 ~0.3s 동안 Idle 포즈 + 이동 → "idle 자세로 순간이동" 증상의 원인.
		const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
		if (bRepathDue)
		{
			const bool bPathOk = ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, SpreadGoal, AcceptanceRadius);

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

	// 회전 방향 결정 — bAlwaysFaceTargetDuringChase 옵션에 따라 분기
	if (bAlwaysFaceTargetDuringChase)
	{
		// 항상 타겟 방향 — RepathInterval 마다 속도가 잠깐 0이 되는 프레임에
		// FaceTarget 으로 스냅되어 "잠깐 플레이어 바라보다 다시 경로 방향"으로 튀는
		// 현상 제거. 경로가 타겟까지 휘어져도 상체는 계속 타겟을 응시.
		ChaseHelpers::FaceTarget(Pawn, Target, DeltaTime, 5.f);
	}
	else
	{
		// 기존 동작: 이동 중에는 속도 방향, 멈춰있으면 타겟 방향
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
	// [AutoApproachRadiusV1] 타겟 크기 기반 자동 반경 — 작은 타워는 감싸듯 가까이 접근.
	const float EffTurretStandoffRadius = ChaseHelpers::GetEffectiveApproachRadius(Turret, TurretStandoffRadius);

	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector TurretLoc = Turret->GetActorLocation();
	const float DistToTurret2D = FVector::Dist2D(PawnLoc, TurretLoc);

	// 각 몬스터의 고유 각도 링 포인트 (Rush 단계 웨이포인트용)
	const FVector RingGoal = ChaseHelpers::AngleToWorldPos(TurretLoc, Data.AssignedAngleDeg, EffTurretStandoffRadius);
	const float DistToRing2D = FVector::Dist2D(PawnLoc, RingGoal);

	// [TurretTickDiagV1] 링 내부 idle-cycle 분석용 Tick 로그. Data.DiagTimer 로 마리당 0.5s throttle.
	Data.DiagTimer += DeltaTime;
	if (Data.DiagTimer >= 0.5f)
	{
		Data.DiagTimer = 0.f;
		const FVector CmcVel = Pawn->GetVelocity();
		UPathFollowingComponent* PFCDiag = AIC->GetPathFollowingComponent();
		const TCHAR* PFCStatusStr =
			(!PFCDiag) ? TEXT("NULL") :
			(PFCDiag->GetStatus() == EPathFollowingStatus::Idle) ? TEXT("Idle") :
			(PFCDiag->GetStatus() == EPathFollowingStatus::Waiting) ? TEXT("Wait") :
			(PFCDiag->GetStatus() == EPathFollowingStatus::Paused) ? TEXT("Paused") :
			(PFCDiag->GetStatus() == EPathFollowingStatus::Moving) ? TEXT("Moving") : TEXT("?");
		UE_LOG(LogTemp, Warning,
			TEXT("[TurretTickDiagV1] Enemy=%s Turret=%s Angle=%.0f DistTgt=%.1f DistRing=%.1f EffR=%.1f Vel=%.1f PFC=%s Direct=%d StuckCnt=%d DetourCD=%.2f"),
			*GetNameSafe(Pawn), *GetNameSafe(Turret),
			Data.AssignedAngleDeg, DistToTurret2D, DistToRing2D, EffTurretStandoffRadius,
			CmcVel.Size2D(), PFCStatusStr, (int)Data.bDirectMoveMode,
			Data.ConsecutiveStuckCount, Data.BlockDetourCooldown);
	}

	// [ChaseBlockDetourV2] Turret 쪽도 쿨다운 + 각도 확대 적용.
	Data.BlockDetourCooldown = FMath::Max(0.f, Data.BlockDetourCooldown - DeltaTime);
	if (!Data.bDirectMoveMode && Data.BlockDetourCooldown <= 0.f)
	{
		Data.BlockCheckTimer += DeltaTime;
		if (Data.BlockCheckTimer >= BlockCheckInterval)
		{
			Data.BlockCheckTimer = 0.f;
			if (ChaseHelpers::IsPathBlockedByOtherEnemy(
				Pawn, Turret, AttackZoneHalfExtent, AttackZoneForwardOffset))
			{
				const float Rotate = FMath::RandRange(BlockDetourAngleMin, BlockDetourAngleMax)
					* (FMath::RandBool() ? 1.f : -1.f);
				Data.AssignedAngleDeg = FMath::Fmod(Data.AssignedAngleDeg + Rotate + 360.f, 360.f);
				const FVector DetourGoal = ChaseHelpers::AngleToWorldPos(
					TurretLoc, Data.AssignedAngleDeg, EffTurretStandoffRadius);
				const FVector NavGoal = ChaseHelpers::ProjectToNav(Pawn->GetWorld(), DetourGoal);
				ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, NavGoal, AcceptanceRadius);
				UE_LOG(LogTemp, Warning,
					TEXT("[ChaseBlockDetourV2] Turret Blocked: Enemy=%s NewAngle=%.0f Rotate=%+.0f CD=%.2f"),
					*GetNameSafe(Pawn), Data.AssignedAngleDeg, Rotate, BlockDetourCooldownDuration);
				Data.RepathTimer = 0.f;
				Data.BlockDetourCooldown = BlockDetourCooldownDuration;
				return EStateTreeRunStatus::Running;
			}
		}
	}

	// [ChaseRepathMinV1] Turret 도 주기적 Repath 제거 → Idle 일 때만 재발급.
	if (!Data.bDirectMoveMode)
	{
		UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent();
		const bool bIdle = !PFC || PFC->GetStatus() == EPathFollowingStatus::Idle;
		if (bIdle)
		{
			const FVector NavGoal = ChaseHelpers::ProjectToNav(Pawn->GetWorld(), RingGoal);
			// [ChaseRepathCycleV1] Idle 상태에서 MoveTo 재발급하는 빈도 + 거리 관측.
			//   DistToNavGoal 이 AcceptanceRadius 이내면 MoveTo 가 즉시 완료 → 다음 틱 또 Idle 되는 cycle 의심.
			//   Throttle 은 쓰지 않음 — cycle 이 매 틱이라면 그 자체가 원인 단서.
			const float DistToNavGoal = FVector::Dist2D(PawnLoc, NavGoal);
			UE_LOG(LogTemp, Warning,
				TEXT("[ChaseRepathCycleV1] Turret Enemy=%s DistToRingGoal=%.1f DistToNavGoal=%.1f AccRadius=%.1f Angle=%.0f DetourCD=%.2f"),
				*GetNameSafe(Pawn), DistToRing2D, DistToNavGoal, AcceptanceRadius,
				Data.AssignedAngleDeg, Data.BlockDetourCooldown);
			ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, NavGoal, AcceptanceRadius);
		}
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

		// [ChaseNoArrivalV1] DirectMode Arrival 제거. Target 본인 방향으로 직진.
		const FVector ToRing2D = (TurretLoc - PawnLoc).GetSafeNormal2D();
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
	// [ChaseKeepSpeedV2] bIdle 케이스는 위 ChaseRepathMinV1 블록이 KeepSpeed 로 처리.
	//   여기서는 RepathInterval 주기 경로 갱신만 담당 + KeepSpeed 로 Vel 리셋 차단.
	//   이전에 bIdle 중복 재발급 + 비-KeepSpeed MoveTo 로 Vel=0 리셋이 반복되어
	//   Idle 포즈 슬라이드(유저 관찰 "idle 자세로 순간이동") 발생했던 지점.
	const bool bRepathDue = Data.TimeSinceRepath >= RepathInterval;
	if (bRepathDue)
	{
		const FVector NavGoal = ChaseHelpers::ProjectToNav(Pawn->GetWorld(), RingGoal);
		const bool bPathOk = ChaseHelpers::IssueMoveToLocationKeepSpeed(AIC, Pawn, NavGoal, AcceptanceRadius);
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
