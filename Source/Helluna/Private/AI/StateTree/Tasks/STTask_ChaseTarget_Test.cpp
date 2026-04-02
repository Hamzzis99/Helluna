/**
 * STTask_ChaseTarget_Test.cpp
 *
 * 우주선/플레이어 통합 추적 Task (v2).
 *
 * ─── 우주선 이동 (지형에 묻힌 우주선 대응) ──────────────────
 *
 *  [Phase 1: 원거리] 중심거리 > SlotEngageRadius
 *    NavMesh 위 중간 경유점으로 우주선 방향 접근.
 *    NavMesh 경계 도달 시 pathfinding OFF 직접 이동.
 *
 *  [Phase 2: 근거리 슬롯] bUseSlotSystem && SlotManager 유효
 *    SlotManager에서 배정받은 NavMesh 검증 위치로 이동.
 *    도착 시 OccupySlot → Running 유지 (Attack State 전환 대기).
 *    Stuck/경로실패 시 슬롯 반납 후 재배정.
 *
 *  [Phase 2': 근거리 자유] 슬롯 OFF 또는 슬롯 없음
 *    NavMesh 경유점 접근 + NavMesh 경계 시 직접 이동.
 *
 * ─── 플레이어 이동 ───────────────────────────────────────────
 *  RepathInterval마다 MoveToActor / EQS 재발행.
 *  Stuck 시 랜덤 우회 후 재추적.
 *  DistanceToTarget <= PlayerAttackRange → Succeeded.
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
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"

#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "AI/SpaceShipAttackSlotManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaseTarget, Log, All);

namespace ChaseTargetTestHelpers {

FVector ComputeDetourGoal(
	const FVector& PawnLoc,
	const FVector& TargetLoc,
	AAIController* AIC,
	float OffsetAmount,
	int32 DirectionSign = 0);

int32 ResolveDetourDirectionSign(
	FSTTask_ChaseTarget_TestInstanceData& D,
	bool bUsePersistentDirection);

// 전방 선언: 이동 + 표면 최근접점 + pathfinding OFF 직접 이동
bool IssueMoveToLocation(AAIController* AIC, const FVector& Goal, float Radius);
FVector ComputeClosestSurfacePoint(const FVector& FromLoc, const AActor* ToActor);
void IssueDirectMoveTowardShipSurface(AAIController* AIC, APawn* Pawn, AActor* Ship, float AcceptanceRadius, int32 ConsecutiveStuckCount = 0, float DeltaTime = 0.016f);
bool ForceDirectMove(APawn* Pawn, const FVector& Direction, float DeltaTime);

// 전방 선언: 우주선 밑/위인지 체크하는 공용 함수
static bool IsUnderOrOnShip(UWorld* World, AActor* Ship, const FVector& Location)
{
	if (!World || !Ship) return false;

	FCollisionQueryParams TraceParams;

	// 위로 Trace: 우주선 밑인지
	{
		const FVector UpStart = Location + FVector(0, 0, 10.f);
		const FVector UpEnd   = Location + FVector(0, 0, 1500.f);
		FHitResult UpHit;
		if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, TraceParams))
		{
			if (UpHit.GetActor() == Ship) return true;
		}
	}

	// 아래로 Trace: 우주선 위인지
	{
		const FVector DownStart = Location + FVector(0, 0, 10.f);
		const FVector DownEnd   = Location - FVector(0, 0, 500.f);
		FHitResult DownHit;
		if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_Visibility, TraceParams))
		{
			if (DownHit.GetActor() == Ship) return true;
		}
	}

	return false;
}

// ============================================================================
// 헬퍼: 우주선 방향 NavMesh 위 중간 경유점 계산
//   우주선 밑/위로 투영되는 경유점은 거부하고, 대안 경유점을 시도한다.
// ============================================================================
FVector ComputeNavGoalTowardShip(
	const FVector& PawnLoc,
	const FVector& ShipLoc,
	AAIController* AIC,
	AActor* ShipActor = nullptr,
	float MaxStepDist = 1200.f)
{
	const FVector ToShip = ShipLoc - PawnLoc;
	const float Dist = ToShip.Size2D();
	const FVector Dir = ToShip.GetSafeNormal2D();

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	if (!NavSys) return PawnLoc;
	if (Dir.IsNearlyZero()) return PawnLoc;

	// 여러 거리를 시도해서 유효한 경유점을 찾는다
	// (가까운 것부터: 80%, 60%, 40%, 20% 거리)
	const float Fractions[] = { 0.8f, 0.6f, 0.4f, 0.2f };
	const float AngleOffsets[] = { 0.f, -25.f, 25.f, -50.f, 50.f, -75.f, 75.f };
	const FVector ProjectionExtent(260.f, 260.f, 900.f);
	float BestScore = MAX_FLT;
	FVector BestGoal = PawnLoc;

	auto ConsiderGoal = [&](const FVector& RawGoal)
	{
		FNavLocation NavGoal;
		if (!NavSys->ProjectPointToNavigation(RawGoal, NavGoal, ProjectionExtent))
		{
			return;
		}

		if (FVector::Dist2D(PawnLoc, NavGoal.Location) < 80.f)
		{
			return;
		}

		if (ShipActor && IsUnderOrOnShip(AIC->GetWorld(), ShipActor, NavGoal.Location))
		{
			return;
		}

		const float DistToShipCenter = FVector::Dist2D(NavGoal.Location, ShipLoc);
		const float DistFromPawn = FVector::Dist2D(PawnLoc, NavGoal.Location);
		const float Score = DistToShipCenter + DistFromPawn * 0.25f;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestGoal = NavGoal.Location;
		}
	};

	for (float Frac : Fractions)
	{
		const float StepDist = FMath::Min(Dist * Frac, MaxStepDist);
		if (StepDist < 80.f) continue;  // 너무 가까우면 스킵

		for (const float AngleOffset : AngleOffsets)
		{
			if (FMath::IsNearlyZero(AngleOffset))
			{
				continue;
			}

			const FVector StepDir = Dir.RotateAngleAxis(AngleOffset, FVector::UpVector).GetSafeNormal2D();
			ConsiderGoal(PawnLoc + StepDir * StepDist);
		}

		const FVector RawGoal = PawnLoc + Dir * StepDist;

		FNavLocation NavGoal;
		if (!NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(200.f, 200.f, 500.f)))
			continue;

		// 현재 위치와 너무 가까우면 스킵
		if (FVector::Dist(PawnLoc, NavGoal.Location) < 80.f)
			continue;

		// 우주선 밑/위인지 체크
		if (ShipActor && IsUnderOrOnShip(AIC->GetWorld(), ShipActor, NavGoal.Location))
			continue;

		const float DirectDistToShipCenter = FVector::Dist2D(NavGoal.Location, ShipLoc);
		const float DirectDistFromPawn = FVector::Dist2D(PawnLoc, NavGoal.Location);
		const float DirectScore = DirectDistToShipCenter + DirectDistFromPawn * 0.25f;
		if (DirectScore < BestScore)
		{
			BestScore = DirectScore;
			BestGoal = NavGoal.Location;
		}
	}

	// 모든 직선 경유점이 실패 → 현재 위치 반환 (정지)
	if (BestScore < MAX_FLT)
	{
		return BestGoal;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
	const float OrbitDistances[] = { 350.f, 550.f, 750.f };
	for (const float OrbitDistance : OrbitDistances)
	{
		ConsiderGoal(PawnLoc + Right * OrbitDistance);
		ConsiderGoal(PawnLoc - Right * OrbitDistance);
	}

	return BestScore < MAX_FLT ? BestGoal : PawnLoc;
}

// ============================================================================
// 헬퍼: 위치로 이동 (MoveTo 실패 시 중간 경유점 폴백)
// ============================================================================
// IssueMoveToLocation 반환값: 실제 이동이 시작되었는지 여부
// false = AlreadyAtGoal 또는 Failed (이동 불가 상태)
bool IssueMoveToLocation(AAIController* AIC, const FVector& Goal, float Radius)
{
	if (!IsValid(AIC)) return false;

	FAIMoveRequest Req;
	Req.SetGoalLocation(Goal);
	Req.SetAcceptanceRadius(Radius);
	Req.SetUsePathfinding(true);
	Req.SetAllowPartialPath(true);
	FPathFollowingRequestResult Result = AIC->MoveTo(Req);

	if (Result.Code == EPathFollowingRequestResult::Failed)
	{
		APawn* Pawn = AIC->GetPawn();
		if (!IsValid(Pawn)) return false;

		// 쓰로틀링: 같은 Pawn에서 2초마다만 로그
		{
			static TMap<uint32, double> LastLogTimes;
			const uint32 PawnId = Pawn->GetUniqueID();
			const double Now = FPlatformTime::Seconds();
			double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
			if (Now - LastTime >= 2.0)
			{
				LastTime = Now;
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][MoveToFailed] Pawn=%s Goal=%s Radius=%.1f -> Fallback"),
					*Pawn->GetName(), *Goal.ToString(), Radius);
			}
		}

		const FVector FallbackGoal = ComputeNavGoalTowardShip(
			Pawn->GetActorLocation(), Goal, AIC, nullptr);

		FAIMoveRequest FallbackReq;
		FallbackReq.SetGoalLocation(FallbackGoal);
		FallbackReq.SetAcceptanceRadius(Radius);
		FallbackReq.SetUsePathfinding(true);
		FallbackReq.SetAllowPartialPath(true);
		FPathFollowingRequestResult FallbackResult = AIC->MoveTo(FallbackReq);

		return FallbackResult.Code == EPathFollowingRequestResult::RequestSuccessful;
	}
	else if (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		APawn* Pawn = AIC->GetPawn();
		// 쓰로틀링: 같은 Pawn에서 2초마다만 로그
		{
			static TMap<uint32, double> LastLogTimes;
			const uint32 PawnId = IsValid(Pawn) ? Pawn->GetUniqueID() : 0;
			const double Now = FPlatformTime::Seconds();
			double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
			if (Now - LastTime >= 2.0)
			{
				LastTime = Now;
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][AlreadyAtGoal] Pawn=%s Goal=%s Radius=%.1f PawnPos=%s"),
					IsValid(Pawn) ? *Pawn->GetName() : TEXT("null"), *Goal.ToString(), Radius,
					IsValid(Pawn) ? *Pawn->GetActorLocation().ToString() : TEXT("null"));
			}
		}
		return false;
	}

	return Result.Code == EPathFollowingRequestResult::RequestSuccessful;
}

void IssueMoveToActorNavProjected(AAIController* AIC, AActor* TargetActor, float AcceptanceRadius)
{
	if (!IsValid(AIC) || !IsValid(TargetActor)) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	if (NavSys)
	{
		FNavLocation NavLoc;
		if (NavSys->ProjectPointToNavigation(
			TargetActor->GetActorLocation(),
			NavLoc,
			FVector(500.f, 500.f, 1000.f)))
		{
			IssueMoveToLocation(AIC, NavLoc.Location, AcceptanceRadius);
			return;
		}
	}

	IssueMoveToLocation(AIC, TargetActor->GetActorLocation(), AcceptanceRadius);
}

bool TryComputeStableShipApproachGoal(
	AAIController* AIC,
	APawn* Pawn,
	AActor* Ship,
	FVector& OutGoal)
{
	if (!IsValid(AIC) || !IsValid(Pawn) || !IsValid(Ship))
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	if (!NavSys)
	{
		return false;
	}

	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector ShipLoc = Ship->GetActorLocation();
	const uint32 PawnSeed = PointerHash(Pawn);
	float BestScore = MAX_FLT;
	FVector BestGoal = FVector::ZeroVector;

	auto ConsiderGoal = [&](const FVector& RawGoal, float BiasScore)
	{
		FNavLocation NavGoal;
		if (!NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(260.f, 260.f, 900.f)))
		{
			return;
		}

		if (FVector::Dist2D(PawnLoc, NavGoal.Location) < 80.f)
		{
			return;
		}

		if (IsUnderOrOnShip(AIC->GetWorld(), Ship, NavGoal.Location))
		{
			return;
		}

		const float Score =
			FVector::DistSquared2D(PawnLoc, NavGoal.Location) +
			FVector::DistSquared2D(NavGoal.Location, ShipLoc) * 0.2f +
			BiasScore;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestGoal = NavGoal.Location;
		}
	};

	if (const USpaceShipAttackSlotManager* SlotMgr = Ship->FindComponentByClass<USpaceShipAttackSlotManager>())
	{
		const TArray<FAttackSlot>& Slots = SlotMgr->GetSlots();
		if (!Slots.IsEmpty())
		{
			const int32 StartIndex = static_cast<int32>(PawnSeed % Slots.Num());
			for (int32 Offset = 0; Offset < Slots.Num(); ++Offset)
			{
				const int32 SlotIndex = (StartIndex + Offset) % Slots.Num();
				const FAttackSlot& Slot = Slots[SlotIndex];
				if (!Slot.IsValid())
				{
					continue;
				}

				const FVector SlotAnchor = Slot.SurfaceLocation.IsZero() ? Slot.WorldLocation : Slot.SurfaceLocation;
				const FVector Outward =
					Slot.SurfaceNormal.IsNearlyZero()
						? (SlotAnchor - ShipLoc).GetSafeNormal2D()
						: Slot.SurfaceNormal.GetSafeNormal2D();
				const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Outward).GetSafeNormal();
				const float SideSign = ((PawnSeed >> 1) & 1U) ? 1.f : -1.f;
				const float OffsetBias = static_cast<float>(Offset) * 4000.f;

				ConsiderGoal(Slot.WorldLocation, OffsetBias);
				if (!Outward.IsNearlyZero())
				{
					ConsiderGoal(SlotAnchor + Outward * 110.f, OffsetBias + 300.f);
					ConsiderGoal(SlotAnchor + Outward * 90.f + Tangent * SideSign * 70.f, OffsetBias + 600.f);
					ConsiderGoal(SlotAnchor + Outward * 90.f - Tangent * SideSign * 70.f, OffsetBias + 900.f);
				}

				if (BestScore < MAX_FLT && Offset >= 2)
				{
					OutGoal = BestGoal;
					return true;
				}
			}

			if (BestScore < MAX_FLT)
			{
				OutGoal = BestGoal;
				return true;
			}
		}
	}

	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	Ship->GetActorBounds(true, BoundsOrigin, BoundsExtent);
	const float RingRadius = FMath::Max(BoundsExtent.Size2D() + 180.f, 260.f);
	const int32 RingPointCount = 8;
	const int32 StartRingIndex = static_cast<int32>(PawnSeed % RingPointCount);

	for (int32 Offset = 0; Offset < RingPointCount; ++Offset)
	{
		const int32 RingIndex = (StartRingIndex + Offset) % RingPointCount;
		const float AngleRad = FMath::DegreesToRadians(360.f * static_cast<float>(RingIndex) / static_cast<float>(RingPointCount));
		const FVector Dir(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.f);
		ConsiderGoal(ShipLoc + Dir * RingRadius, static_cast<float>(Offset) * 2500.f);
	}

	if (BestScore < MAX_FLT)
	{
		OutGoal = BestGoal;
		return true;
	}

	return false;
}

// ============================================================================
// 헬퍼: 우주선 방향 이동
//   NavMesh 경계에 도달하면 무리하게 직진하지 않고 정지한다.
//   (pathfinding OFF 직진은 우주선 메시 아래로 파고드는 문제 유발)
// ============================================================================
void IssueMoveTowardShip(
	AAIController* AIC,
	APawn* Pawn,
	AActor* Ship,
	float AcceptanceRadius,
	int32 ConsecutiveStuckCount = 0,
	float DeltaTime = 0.016f)
{
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector ShipLoc = Ship->GetActorLocation();

	// 쓰로틀링용 로그 헬퍼
	auto ThrottledLog = [&](const TCHAR* Method, const FVector& Goal, float ExtraDist = -1.f)
	{
		static TMap<uint32, double> LastLogTimes;
		const uint32 PawnId = Pawn->GetUniqueID();
		const double Now = FPlatformTime::Seconds();
		double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
		if (Now - LastTime >= 2.0)
		{
			LastTime = Now;
			if (ExtraDist >= 0.f)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][MoveTowardShip] Pawn=%s Method=%s Goal=%s GoalDist=%.1f PawnPos=%s"),
					*Pawn->GetName(), Method, *Goal.ToString(), ExtraDist, *PawnLoc.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[enemybugreport][MoveTowardShip] Pawn=%s Method=%s Goal=%s PawnPos=%s"),
					*Pawn->GetName(), Method, *Goal.ToString(), *PawnLoc.ToString());
			}
		}
	};

	FVector StableApproachGoal = FVector::ZeroVector;
	if (TryComputeStableShipApproachGoal(AIC, Pawn, Ship, StableApproachGoal))
	{
		const float GoalDist = FVector::Dist2D(PawnLoc, StableApproachGoal);
		ThrottledLog(TEXT("StableApproach"), StableApproachGoal, GoalDist);
		const bool bMoveStarted = IssueMoveToLocation(AIC, StableApproachGoal, AcceptanceRadius);
		if (!bMoveStarted)
		{
			// NavMesh 경계 도달: AlreadyAtGoal 또는 Failed → pathfinding OFF 직접 이동
			ThrottledLog(TEXT("DirectFallback(StableApproachFailed)"), ShipLoc);
			IssueDirectMoveTowardShipSurface(AIC, Pawn, Ship, AcceptanceRadius, ConsecutiveStuckCount, DeltaTime);
		}
		return;
	}

	const FVector NavGoal = ComputeNavGoalTowardShip(PawnLoc, ShipLoc, AIC, Ship);

	// 경유점이 현재 위치와 같으면 (제자리 반환) → NavMesh 경계 도달, 직접 이동
	if (FVector::Dist(PawnLoc, NavGoal) < 50.f)
	{
		ThrottledLog(TEXT("DirectFallback(NavGoalTooClose)"), ShipLoc);
		IssueDirectMoveTowardShipSurface(AIC, Pawn, Ship, AcceptanceRadius, ConsecutiveStuckCount, DeltaTime);
		return;
	}

	ThrottledLog(TEXT("NavGoal"), NavGoal, FVector::Dist2D(PawnLoc, NavGoal));
	const bool bMoveStarted = IssueMoveToLocation(AIC, NavGoal, AcceptanceRadius);
	if (!bMoveStarted)
	{
		// NavMesh 이동 실패 → pathfinding OFF 직접 이동
		ThrottledLog(TEXT("DirectFallback(NavGoalFailed)"), ShipLoc);
		IssueDirectMoveTowardShipSurface(AIC, Pawn, Ship, AcceptanceRadius, ConsecutiveStuckCount, DeltaTime);
	}
}

// ============================================================================
// 헬퍼: 우주선 표면 최근접점 계산
// ============================================================================
FVector ComputeClosestSurfacePoint(const FVector& FromLoc, const AActor* ToActor)
{
	if (!ToActor) return FromLoc;

	TArray<UPrimitiveComponent*> Prims;
	ToActor->GetComponents<UPrimitiveComponent>(Prims);

	float MinDist = MAX_FLT;
	FVector BestPoint = ToActor->GetActorLocation();

	// ShipCombatCollision 태그 우선
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision) continue;
		if (!Prim->ComponentHasTag(TEXT("ShipCombatCollision"))) continue;

		FVector Closest = FVector::ZeroVector;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);
		if (D >= 0.f && D < MinDist)
		{
			MinDist = D;
			BestPoint = Closest;
		}
	}
	if (MinDist < MAX_FLT) return BestPoint;

	// 폴백: 모든 콜리전 컴포넌트
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision) continue;

		FVector Closest = FVector::ZeroVector;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);
		if (D >= 0.f && D < MinDist)
		{
			MinDist = D;
			BestPoint = Closest;
		}
	}

	return BestPoint;
}

// ============================================================================
// 헬퍼: NavMesh 경계 도달 후 우주선 표면까지 직접 이동 (pathfinding OFF)
//   우주선 메시 표면의 최근접점을 목표로 직선 이동.
//   CharacterMovementComponent가 콜리전 처리하므로 메시 관통 없음.
// ============================================================================
void IssueDirectMoveTowardShipSurface(AAIController* AIC, APawn* Pawn, AActor* Ship, float AcceptanceRadius, int32 ConsecutiveStuckCount, float DeltaTime)
{
	if (!IsValid(AIC) || !IsValid(Pawn) || !IsValid(Ship)) return;

	const FVector PawnLoc = Pawn->GetActorLocation();
	FVector SurfacePoint = ComputeClosestSurfacePoint(PawnLoc, Ship);

	// 표면점의 Z를 Pawn 높이로 보정 (지형 아래로 파고드는 것 방지)
	SurfacePoint.Z = PawnLoc.Z;

	const float DistToSurface = FVector::Dist2D(PawnLoc, SurfacePoint);
	if (DistToSurface < 10.f) return;

	// Idle 상태(MoveTo 비활성)일 때만 AddMovementInput 사용
	// Moving 상태면 MoveTo가 제어 중이므로 간섭하지 않음
	bool bIsIdle = true;
	if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
	{
		bIsIdle = (PFC->GetStatus() != EPathFollowingStatus::Moving);
	}

	if (bIsIdle)
	{
		const FVector DirToSurface = (SurfacePoint - PawnLoc).GetSafeNormal2D();
		if (!DirToSurface.IsNearlyZero())
		{
			// AddMovementInput이 2회 이상 실패 → SetActorLocation(sweep)으로 강제 이동
			if (ConsecutiveStuckCount >= 2)
			{
				ForceDirectMove(Pawn, DirToSurface, DeltaTime);
			}
			else
			{
				Pawn->AddMovementInput(DirToSurface, 1.f);
			}
		}
	}
	else
	{
		// Moving 상태: MoveTo(pathfinding OFF)로 목표 갱신만
		FAIMoveRequest Req;
		Req.SetGoalLocation(SurfacePoint);
		Req.SetAcceptanceRadius(AcceptanceRadius);
		Req.SetUsePathfinding(false);
		Req.SetAllowPartialPath(true);
		AIC->MoveTo(Req);
	}

	// 쓰로틀링: 같은 Pawn에서 2초마다만 로그
	{
		static TMap<uint32, double> LastLogTimes;
		const uint32 PawnId = Pawn->GetUniqueID();
		const double Now = FPlatformTime::Seconds();
		double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
		if (Now - LastTime >= 2.0)
		{
			LastTime = Now;
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][DirectMoveToShip] Pawn=%s Goal=%s Dist=%.1f PawnPos=%s"),
				*Pawn->GetName(), *SurfacePoint.ToString(), DistToSurface, *PawnLoc.ToString());
		}
	}
}

// ============================================================================
// 헬퍼: 우회 오프셋 목표 계산 (좌/우 랜덤)
// ============================================================================
FVector ComputeDetourGoal(
	const FVector& PawnLoc,
	const FVector& TargetLoc,
	AAIController* AIC,
	float OffsetAmount,
	int32 DirectionSign /* = 0 */)
{
	const FVector DirToTarget = (TargetLoc - PawnLoc).GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, DirToTarget).GetSafeNormal();
	const float Sign = DirectionSign == 0
		? ((FMath::RandBool()) ? 1.f : -1.f)
		: (DirectionSign > 0 ? 1.f : -1.f);

	const FVector RawGoal = PawnLoc + DirToTarget * OffsetAmount * 0.5f + Right * Sign * OffsetAmount;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(AIC->GetWorld());
	FNavLocation NavGoal;
	if (NavSys && NavSys->ProjectPointToNavigation(RawGoal, NavGoal, FVector(300.f, 300.f, 500.f)))
		return NavGoal.Location;

	return RawGoal;
}

int32 ResolveDetourDirectionSign(
	FSTTask_ChaseTarget_TestInstanceData& D,
	bool bUsePersistentDirection)
{
	if (!bUsePersistentDirection)
		return FMath::RandBool() ? 1 : -1;

	if (D.DetourDirectionSign == 0)
		D.DetourDirectionSign = FMath::RandBool() ? 1 : -1;

	return D.DetourDirectionSign;
}

void IssueMoveToActorDirect(AAIController* AIC, AActor* TargetActor, float AcceptanceRadius)
{
	if (!IsValid(AIC) || !IsValid(TargetActor))
	{
		return;
	}

	if (APawn* Pawn = AIC->GetPawn())
	{
		if (TargetActor->ActorHasTag(TEXT("SpaceShip"))
			|| TargetActor->FindComponentByClass<USpaceShipAttackSlotManager>() != nullptr)
		{
			FVector StableApproachGoal = FVector::ZeroVector;
			if (TryComputeStableShipApproachGoal(AIC, Pawn, TargetActor, StableApproachGoal))
			{
				IssueMoveToLocation(AIC, StableApproachGoal, AcceptanceRadius);
				return;
			}
		}
	}

	AIC->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
}

AActor* ResolveFallbackSpaceShip(AAIController* AIC)
{
	if (!IsValid(AIC))
	{
		return nullptr;
	}

	UWorld* World = AIC->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (AHellunaDefenseGameState* DefenseGameState = World->GetGameState<AHellunaDefenseGameState>())
	{
		return DefenseGameState->GetSpaceShip();
	}

	return nullptr;
}

// ============================================================================
// 헬퍼: EQS 실행 후 결과 위치로 이동 (플레이어 전용)
// ============================================================================
void RunPlayerAttackEQS(
	UEnvQuery* Query,
	AAIController* AIController,
	float InAcceptanceRadius,
	AActor* FallbackTarget)
{
	if (!Query || !IsValid(AIController)) return;

	APawn* Pawn = AIController->GetPawn();
	if (!IsValid(Pawn)) return;

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
				if (!IsValid(Ctrl)) return;

				if (Result.IsValid() && !Result->IsAborted() && Result->Items.Num() > 0)
				{
					IssueMoveToLocation(Ctrl, Result->GetItemAsLocation(0), InAcceptanceRadius);
				}
				else if (AActor* Fallback = WeakFallback.Get(); IsValid(Fallback))
				{
					if (Cast<AResourceUsingObject_SpaceShip>(Fallback))
					{
						IssueMoveToActorNavProjected(Ctrl, Fallback, InAcceptanceRadius);
					}
					else
					{
						Ctrl->MoveToActor(Fallback, InAcceptanceRadius, true, true, false, nullptr, true);
					}
				}
			}));
}

// ============================================================================
// 헬퍼: 공격 태그 확인
// ============================================================================
bool IsAttacking(APawn* Pawn)
{
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			static const FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(
				FName("State.Enemy.Attacking"), false);
			if (AttackingTag.IsValid() && ASC->HasMatchingGameplayTag(AttackingTag))
				return true;
		}
	}
	return false;
}

// ============================================================================
// 헬퍼: 표면 거리 계산
//   우주선처럼 크기가 큰 Actor는 중심점 거리가 아닌 메시 표면까지 최단 거리.
// ============================================================================
float ComputeSurfaceDistance(const FVector& FromLoc, const AActor* ToActor)
{
	if (!ToActor) return MAX_FLT;

	TArray<UPrimitiveComponent*> Prims;
	ToActor->GetComponents<UPrimitiveComponent>(Prims);

	float MinDist = MAX_FLT;
	bool bFoundTagged = false;
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}

		if (!Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
		{
			continue;
		}

		FVector Closest = FVector::ZeroVector;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);
		if (D >= 0.f && D < MinDist)
		{
			MinDist = D;
			bFoundTagged = true;
		}
	}

	if (bFoundTagged)
	{
		return MinDist;
	}

	if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(ToActor->GetRootComponent()))
	{
		if (RootPrim->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			FVector ClosestPoint = FVector::ZeroVector;
			const float D = RootPrim->GetClosestPointOnCollision(FromLoc, ClosestPoint);
			if (D >= 0.f)
			{
				return D;
			}
		}
	}

	bool bFoundAny = false;
	MinDist = MAX_FLT;
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}

		FVector Closest = FVector::ZeroVector;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);
		if (D >= 0.f && D < MinDist)
		{
			MinDist = D;
			bFoundAny = true;
		}
	}

	if (bFoundAny)
	{
		return MinDist;
	}

	return FVector::Dist(FromLoc, ToActor->GetActorLocation());
}

// ============================================================================
// 헬퍼: SpaceShipAttackSlotManager 가져오기
// ============================================================================
USpaceShipAttackSlotManager* GetSlotManager(AActor* SpaceShip)
{
	if (!SpaceShip) return nullptr;
	return SpaceShip->FindComponentByClass<USpaceShipAttackSlotManager>();
}

// ============================================================================
// 헬퍼: 위치 변화량 기반 Stuck 감지
// ============================================================================
bool CheckPositionBasedStuck(
	FSTTask_ChaseTarget_TestInstanceData& D,
	const FVector& CurrentLoc,
	float SurfaceDist,
	float AttackThreshold,
	float DeltaTime,
	float InStuckCheckInterval,
	float InStuckDistThreshold)
{
	D.StuckCheckTimer += DeltaTime;

	if (D.StuckCheckTimer < InStuckCheckInterval)
		return false;

	const float MovedDist = FVector::Dist(CurrentLoc, D.LastCheckedLocation);
	const bool bStuck = (MovedDist <= InStuckDistThreshold)
		&& (SurfaceDist > AttackThreshold);

	if (bStuck)
		D.ConsecutiveStuckCount++;
	else
	{
		D.ConsecutiveStuckCount = 0;
		D.DetourDirectionSign = 0;
	}

	D.LastCheckedLocation = CurrentLoc;
	D.StuckCheckTimer = 0.f;

	return bStuck;
}

static bool FindClosestFreeSlot(
	const USpaceShipAttackSlotManager* SlotMgr,
	const FVector& MonsterLoc,
	float& OutDist2D)
{
	OutDist2D = MAX_FLT;

	if (!SlotMgr)
	{
		return false;
	}

	const TArray<FAttackSlot>& Slots = SlotMgr->GetSlots();
	for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
	{
		const FAttackSlot& Slot = Slots[SlotIdx];
		if (Slot.State != ESlotState::Free)
		{
			continue;
		}

		if (FMath::Abs(MonsterLoc.Z - Slot.WorldLocation.Z) > SlotMgr->MaxSlotAssignmentHeightDelta)
		{
			continue;
		}

		const FVector SlotReferenceLocation = Slot.SurfaceLocation.IsZero() ? Slot.WorldLocation : Slot.SurfaceLocation;
		const float Dist2D = FVector::Dist2D(MonsterLoc, SlotReferenceLocation);
		if (Dist2D < OutDist2D)
		{
			OutDist2D = Dist2D;
		}
	}

	return OutDist2D < MAX_FLT;
}

static float ComputeMaxSlotReservationTravelDistance(
	const USpaceShipAttackSlotManager* SlotMgr,
	float SlotReserveRadius,
	float AcceptanceRadius)
{
	float MaxSlotSurfaceOffset = 0.f;
	if (SlotMgr)
	{
		if (const AActor* Owner = SlotMgr->GetOwner())
		{
			for (const FAttackSlot& Slot : SlotMgr->GetSlots())
			{
				const FVector SlotReferenceLocation = Slot.SurfaceLocation.IsZero() ? Slot.WorldLocation : Slot.SurfaceLocation;
				MaxSlotSurfaceOffset = FMath::Max(MaxSlotSurfaceOffset, ComputeSurfaceDistance(SlotReferenceLocation, Owner));
			}

			if (MaxSlotSurfaceOffset <= 0.f)
			{
				MaxSlotSurfaceOffset = SlotMgr->MaxRadius;
			}
		}
	}

	const float SlotTravelAllowance = MaxSlotSurfaceOffset * 4.f + AcceptanceRadius + 120.f;
	return FMath::Max(SlotReserveRadius, SlotTravelAllowance);
}

// ============================================================================
// 헬퍼: AddMovementInput 실패 시 강제 이동 (SetActorLocation + sweep)
//   CMC 바닥 검증을 우회하여 NavMesh 엣지에서도 이동 가능.
//   Sweep=true로 콜리전은 존중.
// ============================================================================
bool ForceDirectMove(APawn* Pawn, const FVector& Direction, float DeltaTime)
{
	if (!IsValid(Pawn) || Direction.IsNearlyZero()) return false;

	const ACharacter* Character = Cast<ACharacter>(Pawn);
	float MoveSpeed = 400.f; // 기본 이동 속도
	if (Character)
	{
		if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
		{
			MoveSpeed = CMC->MaxWalkSpeed;
		}
	}

	const FVector Delta = Direction.GetSafeNormal2D() * MoveSpeed * DeltaTime;
	FHitResult Hit;
	const bool bMoved = Pawn->SetActorLocation(Pawn->GetActorLocation() + Delta, true, &Hit);

	// 쓰로틀링: 같은 Pawn에서 2초마다만 로그
	{
		static TMap<uint32, double> ForceMovLogTimes;
		const uint32 PawnId = Pawn->GetUniqueID();
		const double Now = FPlatformTime::Seconds();
		double& LastTime = ForceMovLogTimes.FindOrAdd(PawnId, 0.0);
		if (Now - LastTime >= 2.0)
		{
			LastTime = Now;
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][ForceDirectMove] Pawn=%s Moved=%d Speed=%.0f Delta=%s Hit=%d Pos=%s"),
				*Pawn->GetName(), (int32)bMoved, MoveSpeed,
				*Delta.ToString(), (int32)Hit.bBlockingHit,
				*Pawn->GetActorLocation().ToString());
		}
	}

	return bMoved;
}

} // namespace ChaseTargetTestHelpers
// Unity Build에서 ChaseTargetHelpers와 이름 충돌 방지를 위해
// using namespace 대신 명시적 네임스페이스 사용
namespace CTH = ChaseTargetTestHelpers;

// ============================================================================
// EnterState
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget_Test::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);

	AAIController* AIC = D.AIController;
	if (!IsValid(AIC))
	{
		UE_LOG(LogChaseTarget, Warning, TEXT("[Chase] EnterState - AIController 무효. Failed 반환"));
		return EStateTreeRunStatus::Failed;
	}

	const FHellunaAITargetData& TD = D.TargetData;

	// 인스턴스 데이터 초기화
	D.TimeSinceRepath       = 0.f;
	D.TimeUntilNextEQS      = 0.f;
	D.StuckCheckTimer       = 0.f;
	D.ConsecutiveStuckCount = 0;
	D.DetourDirectionSign   = 0;
	D.AssignedSlotIndex     = -1;
	D.AssignedSectorIndex   = -1;
	D.AssignedSlotLocation  = FVector::ZeroVector;
	D.AssignedSectorLocation = FVector::ZeroVector;
	D.SlotRetryTimer        = 0.f;
	D.bSlotArrived          = false;
	D.bSectorArrived        = false;
	D.WaitingDestination    = FVector::ZeroVector;
	D.bHasWaitingDestination = false;
	D.ConsecutiveSlotFailures = 0;
	D.AssignedSlotMoveRetryCount = 0;
	D.SimpleMoveDetourGoal = FVector::ZeroVector;
	D.bSimpleMoveDetourActive = false;
	D.SimpleMoveDetourDirectionSign = 0;

	APawn* Pawn = AIC->GetPawn();
	if (IsValid(Pawn))
		D.LastCheckedLocation = Pawn->GetActorLocation();

	if (!TD.HasValidTarget() && !IsValid(CTH::ResolveFallbackSpaceShip(AIC)))
	{
		UE_LOG(LogChaseTarget, Log, TEXT("[Chase] EnterState - [%s] 유효한 타겟 없음. Running 대기"),
			IsValid(Pawn) ? *Pawn->GetName() : TEXT("NoPawn"));
		D.LastMoveTarget = nullptr;
		return EStateTreeRunStatus::Running;
	}

	AActor* TargetActor = TD.TargetActor.Get();
	if (!IsValid(TargetActor))
	{
		TargetActor = CTH::ResolveFallbackSpaceShip(AIC);
	}
	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	UE_LOG(LogChaseTarget, Log,
		TEXT("[Chase] EnterState - [%s] -> [%s] | 타입=%s | 거리=%.1f"),
		IsValid(Pawn) ? *Pawn->GetName() : TEXT("NoPawn"),
		*TargetActor->GetName(),
		bIsSpaceShip ? TEXT("우주선") : TEXT("플레이어"),
		TD.DistanceToTarget);

	if (!IsValid(Pawn))
	{
		D.LastMoveTarget = TargetActor;
		return EStateTreeRunStatus::Running;
	}

	// ── 이동 시작 ────────────────────────────────────────────
	if (bIsSpaceShip)
	{
		const float SurfaceDist = CTH::ComputeSurfaceDistance(Pawn->GetActorLocation(), TargetActor);

		// 이미 공격 범위 안이면 Running 대기 (Attack State 전환 대기)
		if (SurfaceDist <= AttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] 우주선 공격 범위 내 (표면거리=%.1f). Running 대기"),
				*Pawn->GetName(), SurfaceDist);
			D.LastMoveTarget = TargetActor;
			return EStateTreeRunStatus::Running;
		}

		if (bUseSimpleMoveToActorTest)
		{
			CTH::IssueMoveToActorDirect(AIC, TargetActor, AcceptanceRadius);
			D.LastMoveTarget = TargetActor;
			return EStateTreeRunStatus::Running;
		}

		USpaceShipAttackSlotManager* EngagementMgr = CTH::GetSlotManager(TargetActor);
		USpaceShipAttackSlotManager* SlotMgr = (bUseSlotSystem && EngagementMgr) ? EngagementMgr : nullptr;
		USpaceShipAttackSlotManager* SectorMgr =
			(bUseSectorDistribution && EngagementMgr && EngagementMgr->bEnableSectorDistribution) ? EngagementMgr : nullptr;
		const float SlotReserveRadius = FMath::Clamp(SlotEngageRadius * 0.6f, AcceptanceRadius + 100.f, SlotEngageRadius);
		if (SlotMgr && SlotMgr->GetSlots().Num() == 0)
		{
			SlotMgr->TriggerBuildSlotsIfEmpty();
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][TestSlotBuildRetry] Enemy=%s Context=EnterState"),
				*GetNameSafe(Pawn));
			if (SlotMgr->GetSlots().Num() == 0)
			{
				SlotMgr = nullptr;
			}
		}

		auto TryAssignSectorMove = [&]() -> bool
		{
			if (!SectorMgr)
			{
				return false;
			}

			int32 SectorIdx = INDEX_NONE;
			FVector SectorLoc = FVector::ZeroVector;
			if (!SectorMgr->RequestSectorPosition(Pawn, SectorIdx, SectorLoc))
			{
				return false;
			}

			D.AssignedSectorIndex = SectorIdx;
			D.AssignedSectorLocation = SectorLoc;
			D.bSectorArrived = false;
			CTH::IssueMoveToLocation(AIC, SectorLoc, AcceptanceRadius);
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] sector assigned: %d -> %s"),
				*Pawn->GetName(), SectorIdx, *SectorLoc.ToString());
			return true;
		};

		// 슬롯 진입 판정: 표면거리 기준 (우주선이 크면 중심거리가 항상 크므로)
		if (SlotMgr && SurfaceDist <= SlotReserveRadius)
		{
			const float MaxSlotTravelDist = CTH::ComputeMaxSlotReservationTravelDistance(SlotMgr, SlotReserveRadius, AcceptanceRadius);
			float ClosestFreeSlotDist = MAX_FLT;
			const bool bHasFreeSlot = CTH::FindClosestFreeSlot(SlotMgr, Pawn->GetActorLocation(), ClosestFreeSlotDist);

			if (bHasFreeSlot && ClosestFreeSlotDist > MaxSlotTravelDist)
			{
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
			D.LastMoveTarget = TargetActor;
			return EStateTreeRunStatus::Running;
		}

			// 슬롯 진입 범위 안: 슬롯 배정 시도
			int32 SlotIdx = -1;
			FVector SlotLoc;
			if (SlotMgr->RequestSlot(Pawn, SlotIdx, SlotLoc))
			{
				D.AssignedSlotIndex    = SlotIdx;
				D.AssignedSlotLocation = SlotLoc;
				D.bSlotArrived         = false;
				D.ConsecutiveSlotFailures = 0;
				D.AssignedSlotMoveRetryCount = 0;
				CTH::IssueMoveToLocation(AIC, SlotLoc, AcceptanceRadius);
				UE_LOG(LogChaseTarget, Log,
					TEXT("[Chase] EnterState - [%s] 슬롯 배정 성공: %d -> %s"),
					*Pawn->GetName(), SlotIdx, *SlotLoc.ToString());
			}
			else
			{
				if (!TryAssignSectorMove())
				{
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
				}
			}
		}
		else
		{
			// 원거리 또는 슬롯 미사용: NavMesh 경유점 접근
			if (!SlotMgr && SurfaceDist <= SlotReserveRadius && TryAssignSectorMove())
			{
				D.LastMoveTarget = TargetActor;
				return EStateTreeRunStatus::Running;
			}

			CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
		}
	}
	else
	{
		const float ActualPlayerDist = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());

		// 플레이어: 이미 공격 범위면 즉시 Succeeded
		if (ActualPlayerDist <= PlayerAttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] 플레이어 공격 범위 내. Succeeded"),
				*Pawn->GetName());
			return EStateTreeRunStatus::Succeeded;
		}

		AIC->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	}

	D.LastMoveTarget = TargetActor;
	return EStateTreeRunStatus::Running;
}

// ============================================================================
// Tick
// ============================================================================
EStateTreeRunStatus FSTTask_ChaseTarget_Test::Tick(
	FStateTreeExecutionContext& Context,
	float DeltaTime) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);

	AAIController* AIC = D.AIController;
	if (!IsValid(AIC)) return EStateTreeRunStatus::Failed;

	const FHellunaAITargetData& TD = D.TargetData;

	APawn* Pawn = AIC->GetPawn();
	if (!IsValid(Pawn)) return EStateTreeRunStatus::Failed;

	AActor* TargetActor = TD.TargetActor.Get();
	if (!IsValid(TargetActor))
	{
		TargetActor = CTH::ResolveFallbackSpaceShip(AIC);
		if (!IsValid(TargetActor))
		{
			return EStateTreeRunStatus::Failed;
		}
	}

	const AActor* PreviousTargetActor = D.LastMoveTarget.Get();
	const bool bTargetChangedActual = PreviousTargetActor != TargetActor;
	const bool bPreviousTargetWasShip = IsValid(PreviousTargetActor)
		&& (Cast<AResourceUsingObject_SpaceShip>(PreviousTargetActor) != nullptr
			|| PreviousTargetActor->FindComponentByClass<USpaceShipAttackSlotManager>() != nullptr);
	if (bTargetChangedActual && bPreviousTargetWasShip)
	{
		if (USpaceShipAttackSlotManager* PreviousSlotMgr = CTH::GetSlotManager(const_cast<AActor*>(PreviousTargetActor)))
		{
			PreviousSlotMgr->ReleaseEngagementReservation(Pawn);
		}

		D.AssignedSlotIndex = -1;
		D.AssignedSectorIndex = -1;
		D.AssignedSlotLocation = FVector::ZeroVector;
		D.AssignedSectorLocation = FVector::ZeroVector;
		D.bSlotArrived = false;
		D.bSectorArrived = false;
		D.SlotRetryTimer = 0.f;
		D.ConsecutiveSlotFailures = 0;
		D.AssignedSlotMoveRetryCount = 0;
		D.WaitingDestination = FVector::ZeroVector;
		D.bHasWaitingDestination = false;
		D.SimpleMoveDetourGoal = FVector::ZeroVector;
		D.bSimpleMoveDetourActive = false;
		D.SimpleMoveDetourDirectionSign = 0;
		D.ConsecutiveStuckCount = 0;
		D.DetourDirectionSign = 0;
		D.StuckCheckTimer = 0.f;
		D.LastCheckedLocation = Pawn->GetActorLocation();
		AIC->StopMovement();

		UE_LOG(LogChaseTarget, Log,
			TEXT("[Chase] Tick - [%s] target changed from ship to %s. Reset previous ship engagement state."),
			*Pawn->GetName(),
			IsValid(TargetActor) ? *TargetActor->GetName() : TEXT("null"));
	}

	const bool bIsSpaceShip = (Cast<AResourceUsingObject_SpaceShip>(TargetActor) != nullptr);

	D.TimeSinceRepath  += DeltaTime;
	D.TimeUntilNextEQS -= DeltaTime;

	if (bIsSpaceShip)
	{
		// ══════════════════════════════════════════════════════
		// 우주선 이동
		// ══════════════════════════════════════════════════════

		const FVector PawnLoc = Pawn->GetActorLocation();
		const float SurfaceDist = CTH::ComputeSurfaceDistance(PawnLoc, TargetActor);
		const float CenterDist = FVector::Dist(PawnLoc, TargetActor->GetActorLocation());
		const bool bAttackLocked = CTH::IsAttacking(Pawn);

		if (bAttackLocked && SurfaceDist <= AttackRange + AcceptanceRadius + 100.f)
		{
			AIC->StopMovement();
			D.LastMoveTarget = TargetActor;
			return EStateTreeRunStatus::Running;
		}

		// ── 디버그 로그 (2초마다) ────────────────────────────
		static float ShipDbgTimer = 0.f;
		ShipDbgTimer += DeltaTime;
		if (ShipDbgTimer >= 2.f)
		{
			ShipDbgTimer = 0.f;
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] Tick - [%s] 우주선 | 표면거리=%.1f 중심거리=%.1f | SlotIdx=%d SlotArrived=%d | PFC=%d"),
				*Pawn->GetName(), SurfaceDist, CenterDist,
				D.AssignedSlotIndex, (int)D.bSlotArrived,
				AIC->GetPathFollowingComponent() ? (int32)AIC->GetPathFollowingComponent()->GetStatus() : -1);
		}

		// ── Stuck 감지 (토글: bUseStuckDetour) ──────────────
		const bool bStuck = bUseStuckDetour
			? CTH::CheckPositionBasedStuck(D, PawnLoc, SurfaceDist, AttackRange,
				DeltaTime, StuckCheckInterval, StuckDistThreshold)
			: false;

		if (bStuck)
		{
			UE_LOG(LogChaseTarget, Warning,
				TEXT("[Chase] Tick - [%s] *** STUCK *** | 연속=%d | 표면거리=%.1f"),
				*Pawn->GetName(), D.ConsecutiveStuckCount, SurfaceDist);
		}

		// ── Idle 감지 ───────────────────────────────────────
		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		// ── 슬롯 시스템 분기 ────────────────────────────────
		if (bUseSimpleMoveToActorTest)
		{
			const bool bTargetChanged = D.LastMoveTarget.Get() != TargetActor;
			if (bTargetChanged)
			{
				D.ConsecutiveStuckCount = 0;
				D.DetourDirectionSign = 0;
				D.bSimpleMoveDetourActive = false;
				D.SimpleMoveDetourGoal = FVector::ZeroVector;
				D.SimpleMoveDetourDirectionSign = 0;
			}

			if (SurfaceDist <= AttackRange)
			{
				AIC->StopMovement();
				D.bSimpleMoveDetourActive = false;
				D.SimpleMoveDetourGoal = FVector::ZeroVector;
				return EStateTreeRunStatus::Running;
			}

			if (D.bSimpleMoveDetourActive)
			{
				const float DistToDetour2D = FVector::Dist2D(PawnLoc, D.SimpleMoveDetourGoal);
				if (DistToDetour2D <= AcceptanceRadius + 30.f)
				{
					D.bSimpleMoveDetourActive = false;
					D.SimpleMoveDetourGoal = FVector::ZeroVector;
					CTH::IssueMoveToActorDirect(AIC, TargetActor, AcceptanceRadius);
					D.TimeSinceRepath = 0.f;
					D.LastMoveTarget = TargetActor;
					return EStateTreeRunStatus::Running;
				}

				if (bIdle)
				{
					CTH::IssueMoveToLocation(AIC, D.SimpleMoveDetourGoal, AcceptanceRadius);
					D.TimeSinceRepath = 0.f;
				}

				D.LastMoveTarget = TargetActor;
				return EStateTreeRunStatus::Running;
			}

			if (bStuck)
			{
				if (D.SimpleMoveDetourDirectionSign == 0)
				{
					D.SimpleMoveDetourDirectionSign = FMath::RandBool() ? 1 : -1;
				}

				D.SimpleMoveDetourGoal = CTH::ComputeDetourGoal(
					PawnLoc,
					TargetActor->GetActorLocation(),
					AIC,
					SimpleMoveToActorDetourDistance,
					D.SimpleMoveDetourDirectionSign);
				D.bSimpleMoveDetourActive = true;
				D.ConsecutiveStuckCount = 0;
				D.StuckCheckTimer = 0.f;
				D.LastCheckedLocation = PawnLoc;
				CTH::IssueMoveToLocation(AIC, D.SimpleMoveDetourGoal, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
				D.LastMoveTarget = TargetActor;
				return EStateTreeRunStatus::Running;
			}

			if (bTargetChanged || bIdle || D.TimeSinceRepath >= RepathInterval)
			{
				CTH::IssueMoveToActorDirect(AIC, TargetActor, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
			}

			D.LastMoveTarget = TargetActor;
			return EStateTreeRunStatus::Running;
		}

		USpaceShipAttackSlotManager* EngagementMgr = CTH::GetSlotManager(TargetActor);
		USpaceShipAttackSlotManager* SlotMgr = (bUseSlotSystem && EngagementMgr) ? EngagementMgr : nullptr;
		USpaceShipAttackSlotManager* SectorMgr =
			(bUseSectorDistribution && EngagementMgr && EngagementMgr->bEnableSectorDistribution) ? EngagementMgr : nullptr;
		if (SlotMgr && SlotMgr->GetSlots().Num() == 0)
		{
			SlotMgr->TriggerBuildSlotsIfEmpty();
			// 쓰로틀링: 5초마다
			{
				static TMap<uint32, double> LastLogTimes;
				const uint32 PawnId = Pawn->GetUniqueID();
				const double Now = FPlatformTime::Seconds();
				double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
				if (Now - LastTime >= 5.0)
				{
					LastTime = Now;
					UE_LOG(LogTemp, Warning,
						TEXT("[enemybugreport][TestSlotBuildRetry] Enemy=%s Context=Tick"),
						*GetNameSafe(Pawn));
				}
			}
			if (SlotMgr->GetSlots().Num() == 0)
			{
				SlotMgr = nullptr;
			}
		}

		if (SlotMgr || SectorMgr)
		{
			// ─── 슬롯 시스템 ON ─────────────────────────────

			// 공격 범위 근처: 슬롯 점유 + 직접 이동 계속
			// (정지하면 AttackZone 박스가 안 닿으므로 멈추지 않고 우주선 표면까지 접근)
			if (SurfaceDist <= AttackRange)
			{
				if (D.AssignedSlotIndex >= 0)
				{
					SlotMgr->OccupySlot(D.AssignedSlotIndex, Pawn);
				}

				D.bSlotArrived = true;
				D.bSectorArrived = (D.AssignedSectorIndex >= 0);
				D.ConsecutiveSlotFailures = 0;

				// 우주선을 바라보면서 직접 전진 (AttackZone 트리거될 때까지)
				const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
				if (!ToShip.IsNearlyZero())
				{
					AIC->SetFocalPoint(PawnLoc + ToShip * 1000.f);
					// 항상 전진 — 우주선 콜리전이 자연스럽게 막아줌
					Pawn->AddMovementInput(ToShip, 1.f);
				}

				// 쓰로틀링: 2초마다
				{
					static TMap<uint32, double> LastLogTimes;
					const uint32 PawnId = Pawn->GetUniqueID();
					const double Now = FPlatformTime::Seconds();
					double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
					if (Now - LastTime >= 2.0)
					{
						LastTime = Now;
						UE_LOG(LogTemp, Warning,
							TEXT("[enemybugreport][InAttackRange-Push] Monster=%s SurfDist=%.1f AttackRange=%.1f Slot=%d Pos=%s"),
							*Pawn->GetName(), SurfaceDist, AttackRange,
							D.AssignedSlotIndex, *PawnLoc.ToString());
					}
				}
				return EStateTreeRunStatus::Running;
			}

			// 슬롯/섹터 도착 후: 우주선 방향으로 계속 전진
			// (InAttackZone 조건이 트리거될 때까지 접근)
			if (D.bSlotArrived || D.bSectorArrived)
			{
				const bool bWasIdle = bIdle;
				const bool bRepathDue = D.TimeSinceRepath >= RepathInterval;

				// Stuck 감지: 콜리전에 막혀 위치 변화 없음
				if (bStuck)
				{
					// 쓰로틀링: 2초마다
					{
						static TMap<uint32, double> LastLogTimes;
						const uint32 PawnId = Pawn->GetUniqueID();
						const double Now = FPlatformTime::Seconds();
						double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
						if (Now - LastTime >= 2.0)
						{
							LastTime = Now;
							UE_LOG(LogTemp, Warning,
								TEXT("[enemybugreport][PostSlotAdvance-Stuck] Monster=%s SurfDist=%.1f StuckCount=%d Pos=%s"),
								*Pawn->GetName(), SurfaceDist,
								D.ConsecutiveStuckCount, *PawnLoc.ToString());
						}
					}

					// PathFollowing 정지 후 우주선 방향 직접 밀기
					AIC->StopMovement();
					const FVector DirToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
					if (!DirToShip.IsNearlyZero())
					{
						// AddMovementInput이 2회 이상 실패 → SetActorLocation(sweep)으로 강제 이동
						if (D.ConsecutiveStuckCount >= 2)
						{
							CTH::ForceDirectMove(Pawn, DirToShip, DeltaTime);
						}
						else
						{
							Pawn->AddMovementInput(DirToShip, 1.f);
						}
					}
					D.TimeSinceRepath = 0.f;
					return EStateTreeRunStatus::Running;
				}

				// 슬롯/섹터 도착 후: MoveToActor로 우주선에 접근
				// Idle이면 NavMesh 한계 → AddMovementInput으로 직접 전진
				if (bIdle || bRepathDue)
				{
					// 쓰로틀링: 2초마다
					{
						static TMap<uint32, double> LastLogTimes;
						const uint32 PawnId = Pawn->GetUniqueID();
						const double Now = FPlatformTime::Seconds();
						double& LastTime = LastLogTimes.FindOrAdd(PawnId, 0.0);
						if (Now - LastTime >= 2.0)
						{
							LastTime = Now;
							UE_LOG(LogTemp, Warning,
								TEXT("[enemybugreport][PostSlotAdvance-Advance] Monster=%s SurfDist=%.1f CenterDist=%.1f Idle=%d Repath=%d Pos=%s"),
								*Pawn->GetName(), SurfaceDist, CenterDist,
								(int32)bWasIdle, (int32)bRepathDue,
								*PawnLoc.ToString());
						}
					}

					// MoveToActor 시도 (pathfinding ON, partial path 허용)
					const EPathFollowingRequestResult::Type MoveResult =
						AIC->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);

					// AlreadyAtGoal = NavMesh 경계 도달, 더 갈 수 없음 → 직접 전진
					if (MoveResult == EPathFollowingRequestResult::AlreadyAtGoal)
					{
						const FVector DirToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
						if (!DirToShip.IsNearlyZero())
						{
							// Idle+AlreadyAtGoal이 반복 = AddMovementInput 무력화 상태
							if (D.ConsecutiveStuckCount >= 2)
							{
								CTH::ForceDirectMove(Pawn, DirToShip, DeltaTime);
							}
							else
							{
								Pawn->AddMovementInput(DirToShip, 1.f);
							}
						}
					}
					D.TimeSinceRepath = 0.f;
				}
				else
				{
					// MoveToActor 실행 중이더라도 매 틱 AddMovementInput 보조
					// (partial path 끝점에 도달하면 Idle이 되기 전까지 시간이 걸릴 수 있음)
					const FVector DirToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
					if (!DirToShip.IsNearlyZero() && SurfaceDist < AttackRange * 1.5f)
					{
						Pawn->AddMovementInput(DirToShip, 0.5f);
					}
				}
				return EStateTreeRunStatus::Running;
			}

			// 슬롯 미배정 상태
			if (D.AssignedSectorIndex >= 0 && SectorMgr)
			{
				const float DistToSector2D = FVector::Dist2D(PawnLoc, D.AssignedSectorLocation);
				const float DistToSector3D = FVector::Dist(PawnLoc, D.AssignedSectorLocation);
				const float SectorHeightDelta = FMath::Abs(PawnLoc.Z - D.AssignedSectorLocation.Z);
				if (DistToSector2D <= AcceptanceRadius + 30.f)
				{
					// 섹터 방향 도착 → 우주선 방향으로 계속 전진
					D.bSectorArrived = true;

					UE_LOG(LogChaseTarget, Log,
						TEXT("[Chase] Tick - [%s] sector arrived -> continue toward ship | Sector=%d Dist2D=%.1f SurfDist=%.1f"),
						*Pawn->GetName(), D.AssignedSectorIndex, DistToSector2D, SurfaceDist);

					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
					D.TimeSinceRepath = 0.f;
					return EStateTreeRunStatus::Running;
				}

				if (bStuck || (bIdle && DistToSector2D > AcceptanceRadius + 200.f))
				{
					SectorMgr->ReleaseSectorReservation(Pawn);
					D.AssignedSectorIndex = -1;
					D.AssignedSectorLocation = FVector::ZeroVector;
					D.bSectorArrived = false;
					D.SlotRetryTimer = bStuck ? 0.5f : 2.f;
					D.ConsecutiveSlotFailures++;
				}
				else if (bIdle || D.TimeSinceRepath >= RepathInterval)
				{
					CTH::IssueMoveToLocation(AIC, D.AssignedSectorLocation, AcceptanceRadius);
					D.TimeSinceRepath = 0.f;
				}

				return EStateTreeRunStatus::Running;
			}

			if (SlotMgr && D.AssignedSlotIndex < 0)
			{
				// 슬롯 진입 범위 밖: 우주선 방향 접근 (표면거리 기준)
				const float SlotReserveRadius = FMath::Clamp(SlotEngageRadius * 0.6f, AcceptanceRadius + 100.f, SlotEngageRadius);
				const float MaxSlotTravelDist = CTH::ComputeMaxSlotReservationTravelDistance(SlotMgr, SlotReserveRadius, AcceptanceRadius);
				float ClosestFreeSlotDist = MAX_FLT;
				const bool bHasFreeSlot = CTH::FindClosestFreeSlot(SlotMgr, PawnLoc, ClosestFreeSlotDist);
				if (SurfaceDist > SlotReserveRadius)
				{
					if (D.TimeSinceRepath >= RepathInterval || bIdle || bStuck)
					{
						if (bStuck)
						{
							const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
							const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
							CTH::IssueMoveToLocation(AIC,
								CTH::ComputeDetourGoal(PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign),
								AcceptanceRadius);
						}
						else
						{
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				if (bHasFreeSlot && ClosestFreeSlotDist > MaxSlotTravelDist)
				{
					if (D.TimeSinceRepath >= RepathInterval || bIdle || bStuck)
					{
						if (bStuck)
						{
							const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
							const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
							CTH::IssueMoveToLocation(AIC,
								CTH::ComputeDetourGoal(PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign),
								AcceptanceRadius);
						}
						else
						{
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				// 연속 실패 3회 이상: 슬롯 포기, 자유 이동
				if (D.ConsecutiveSlotFailures >= 3)
				{
					if (D.TimeSinceRepath >= RepathInterval || bIdle || bStuck)
					{
						bool bAssignedSector = false;
						if (SectorMgr)
						{
							int32 SectorIdx = INDEX_NONE;
							FVector SectorLoc = FVector::ZeroVector;
							if (SectorMgr->RequestSectorPosition(Pawn, SectorIdx, SectorLoc))
							{
								D.AssignedSectorIndex = SectorIdx;
								D.AssignedSectorLocation = SectorLoc;
								D.bSectorArrived = false;
								CTH::IssueMoveToLocation(AIC, SectorLoc, AcceptanceRadius);
								bAssignedSector = true;
							}
						}

						if (!bAssignedSector)
						{
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				// 슬롯 진입 범위 안: 배정 재시도 (2초 쿨다운)
				D.SlotRetryTimer -= DeltaTime;
				if (D.SlotRetryTimer <= 0.f)
				{
					D.SlotRetryTimer = 2.f;
					int32 SlotIdx = -1;
					FVector SlotLoc;
					const bool bSlotRequested = SlotMgr->RequestSlot(Pawn, SlotIdx, SlotLoc);
					if (!bSlotRequested)
					{
						bool bAssignedSector = false;
						if (SectorMgr)
						{
							int32 SectorIdx = INDEX_NONE;
							FVector SectorLoc = FVector::ZeroVector;
							if (SectorMgr->RequestSectorPosition(Pawn, SectorIdx, SectorLoc))
							{
								D.AssignedSectorIndex = SectorIdx;
								D.AssignedSectorLocation = SectorLoc;
								D.bSectorArrived = false;
								CTH::IssueMoveToLocation(AIC, SectorLoc, AcceptanceRadius);
								bAssignedSector = true;
							}
						}

						if (!bAssignedSector)
						{
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
						}
						return EStateTreeRunStatus::Running;
					}

					if (bSlotRequested)
					{
						D.AssignedSlotIndex    = SlotIdx;
						D.AssignedSlotLocation = SlotLoc;
						D.bSlotArrived         = false;
						D.bHasWaitingDestination = false;
						D.ConsecutiveSlotFailures = 0;
						D.AssignedSlotMoveRetryCount = 0;
						CTH::IssueMoveToLocation(AIC, SlotLoc, AcceptanceRadius);
					}
					else
					{
						// 슬롯 없음: 우주선 방향 자유 이동 (멍때림 방지)
						CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
					}
				}

				// 대기 중(슬롯 미배정 + Idle): 우주선 방향 접근
				if (bIdle && SurfaceDist > AttackRange + 100.f)
				{
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
				}
				else if (bIdle)
				{
					// 공격 범위 내 대기: 우주선을 바라보고 서있기
					const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
					if (!ToShip.IsNearlyZero())
					{
						Pawn->SetActorRotation(FMath::RInterpTo(
							Pawn->GetActorRotation(),
							FRotator(0.f, ToShip.Rotation().Yaw, 0.f),
							DeltaTime, 5.f));
					}
				}

				return EStateTreeRunStatus::Running;
			}

			// 슬롯 배정됨 → 도착 판정
			if (!SlotMgr)
			{
				bool bMoving = false;
				if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
				{
					bMoving = (PFC->GetStatus() == EPathFollowingStatus::Moving);
				}

				const float SlotReserveRadius = FMath::Clamp(SlotEngageRadius * 0.6f, AcceptanceRadius + 100.f, SlotEngageRadius);
				if (D.AssignedSectorIndex < 0 && SurfaceDist <= SlotReserveRadius &&
					(D.TimeSinceRepath >= RepathInterval || bIdle || bStuck))
				{
					int32 SectorIdx = INDEX_NONE;
					FVector SectorLoc = FVector::ZeroVector;
					if (SectorMgr && SectorMgr->RequestSectorPosition(Pawn, SectorIdx, SectorLoc))
					{
						D.AssignedSectorIndex = SectorIdx;
						D.AssignedSectorLocation = SectorLoc;
						D.bSectorArrived = false;
						CTH::IssueMoveToLocation(AIC, SectorLoc, AcceptanceRadius);
						D.TimeSinceRepath = 0.f;
						return EStateTreeRunStatus::Running;
					}
				}

				if (bStuck)
				{
					const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
					const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
					const FVector DetourGoal = CTH::ComputeDetourGoal(
						PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign);
					CTH::IssueMoveToLocation(AIC, DetourGoal, AcceptanceRadius);
					D.TimeSinceRepath = 0.f;
				}
				else if (bIdle && SurfaceDist > AttackRange + 100.f)
				{
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
					D.TimeSinceRepath = 0.f;
				}
				else if (!bMoving && D.TimeSinceRepath >= RepathInterval)
				{
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
					D.TimeSinceRepath = 0.f;
				}

				return EStateTreeRunStatus::Running;
			}

			const float DistToSlot2D = FVector::Dist2D(PawnLoc, D.AssignedSlotLocation);
			const float DistToSlot3D = FVector::Dist(PawnLoc, D.AssignedSlotLocation);
			const float SlotHeightDelta = FMath::Abs(PawnLoc.Z - D.AssignedSlotLocation.Z);
			if (DistToSlot2D <= AcceptanceRadius + 30.f)
			{
				// 슬롯 방향 도착 → 점유 후 우주선 방향으로 계속 전진
				D.bSlotArrived = true;
				D.ConsecutiveSlotFailures = 0;
				D.AssignedSlotMoveRetryCount = 0;
				SlotMgr->OccupySlot(D.AssignedSlotIndex, Pawn);

				UE_LOG(LogChaseTarget, Log,
					TEXT("[Chase] Tick - [%s] Slot occupied -> continue toward ship | Slot=%d Dist2D=%.1f SurfDist=%.1f"),
					*Pawn->GetName(), D.AssignedSlotIndex, DistToSlot2D, SurfaceDist);

				// 정지하지 않고 우주선 방향으로 계속 이동
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
				D.TimeSinceRepath = 0.f;
				return EStateTreeRunStatus::Running;
			}

			if (!bIdle)
			{
				D.AssignedSlotMoveRetryCount = 0;
			}

			// Idle/Stuck = NavMesh로 슬롯에 도달 불가 → 우주선 방향 전진
			if ((bIdle || bStuck) && DistToSlot2D <= AcceptanceRadius + 200.f)
			{
				AIC->StopMovement();
				const FVector DirToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
				if (!DirToShip.IsNearlyZero())
				{
					if (D.ConsecutiveStuckCount >= 2)
					{
						CTH::ForceDirectMove(Pawn, DirToShip, DeltaTime);
					}
					else
					{
						Pawn->AddMovementInput(DirToShip, 1.f);
					}
				}
				return EStateTreeRunStatus::Running;
			}

			if (bIdle && DistToSlot2D > AcceptanceRadius + 200.f && D.TimeSinceRepath >= RepathInterval)
			{
				int32 RefreshedSlotIdx = D.AssignedSlotIndex;
				FVector RefreshedSlotLoc = D.AssignedSlotLocation;
				const bool bRefreshedSlot = SlotMgr->RequestSlot(Pawn, RefreshedSlotIdx, RefreshedSlotLoc)
					&& RefreshedSlotIdx == D.AssignedSlotIndex;

				if (bRefreshedSlot)
				{
					D.AssignedSlotLocation = RefreshedSlotLoc;
				}

				CTH::IssueMoveToLocation(AIC, D.AssignedSlotLocation, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
				D.AssignedSlotMoveRetryCount++;

				UE_LOG(LogChaseTarget, Warning,
					TEXT("[Chase] Tick - [%s] 슬롯 이동 재시도 | Slot=%d Retry=%d Refreshed=%d Dist2D=%.1f Goal=%s"),
					*Pawn->GetName(),
					D.AssignedSlotIndex,
					D.AssignedSlotMoveRetryCount,
					(int32)bRefreshedSlot,
					DistToSlot2D,
					*D.AssignedSlotLocation.ToString());

				if (D.AssignedSlotMoveRetryCount < 3)
				{
					return EStateTreeRunStatus::Running;
				}
			}

			// Stuck: 슬롯 반납하지 않고 우주선 방향 전진
			if (bStuck)
			{
				AIC->StopMovement();
				const FVector DirToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
				if (!DirToShip.IsNearlyZero())
				{
					if (D.ConsecutiveStuckCount >= 2)
					{
						CTH::ForceDirectMove(Pawn, DirToShip, DeltaTime);
					}
					else
					{
						Pawn->AddMovementInput(DirToShip, 1.f);
					}
				}
				return EStateTreeRunStatus::Running;
			}

			// Idle: 슬롯 반납 후 재배정
			bool bNeedReassign = false;
			if (bIdle && DistToSlot2D > AcceptanceRadius + 200.f && D.AssignedSlotMoveRetryCount >= 3)
				bNeedReassign = true;

			if (bNeedReassign)
			{
				const bool bIdleRelease = !bStuck;

				UE_LOG(LogChaseTarget, Warning,
					TEXT("[Chase] Tick - [%s] 슬롯 재배정 (이유: %s), 이전 슬롯=%d, 연속실패=%d"),
					*Pawn->GetName(), bStuck ? TEXT("Stuck") : TEXT("Idle"),
					D.AssignedSlotIndex, D.ConsecutiveSlotFailures);

				SlotMgr->ReleaseSlotByIndex(D.AssignedSlotIndex);
				D.AssignedSlotIndex     = -1;
				D.ConsecutiveStuckCount = 0;
				D.DetourDirectionSign   = 0;
				D.AssignedSlotMoveRetryCount = 0;

				if (bIdleRelease)
				{
					// PFC=Idle = 경로 미발견 → 쿨다운 부여 (매 프레임 재배정 방지)
					D.ConsecutiveSlotFailures++;
					D.SlotRetryTimer = 2.f;

					// 대기 중 우주선 방향으로 자유 이동 (멍때림 방지)
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
				}
				else
				{
					// Stuck: 빠른 재시도 허용
					D.SlotRetryTimer = 0.5f;
				}
			}
		}
		else
		{
			// ─── 슬롯 시스템 OFF (자유 이동) ────────────────
			// 이동 중(Moving)이면 Repath를 건너뛰어 경로 리셋 진동 방지
			bool bMoving = false;
			if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
				bMoving = (PFC->GetStatus() == EPathFollowingStatus::Moving);

			if (bStuck)
			{
				const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
				const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
				const FVector DetourGoal = CTH::ComputeDetourGoal(
					PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign);

				UE_LOG(LogChaseTarget, Log,
					TEXT("[Chase] Tick - [%s] 우주선 Stuck 우회 | 배수=%.1f"),
					*Pawn->GetName(), Mult);

				CTH::IssueMoveToLocation(AIC, DetourGoal, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
			}
			else if (bIdle && SurfaceDist > AttackRange + 100.f)
			{
				// Idle인데 아직 멀면 재이동
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
				D.TimeSinceRepath = 0.f;
			}
			else if (!bMoving && D.TimeSinceRepath >= RepathInterval)
			{
				// 이동 중이 아닐 때만 Repath (이동 중 Repath는 경로 리셋 진동 유발)
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, D.ConsecutiveStuckCount, DeltaTime);
				D.TimeSinceRepath = 0.f;
			}
		}

		// 우주선 방향 회전 (슬롯 도착 상태가 아닐 때)
		// CharacterMovement의 bOrientRotationToMovement가 이동 방향 회전을 처리하므로
		// 이동 중에는 수동 회전을 하지 않는다 (두 시스템이 충돌하면 움찔거림 발생).
		// 정지 상태(Idle)에서만 우주선을 향해 회전한다.
		if (!D.bSlotArrived && bIdle)
		{
			const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
			if (!ToShip.IsNearlyZero())
			{
				Pawn->SetActorRotation(FMath::RInterpTo(
					Pawn->GetActorRotation(),
					FRotator(0.f, ToShip.Rotation().Yaw, 0.f),
					DeltaTime, 5.f));
			}
		}
	}
	else
	{
		// ══════════════════════════════════════════════════════
		// 플레이어 이동
		// ══════════════════════════════════════════════════════

		const float DistToTarget = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());

		// 공격 범위 도달 → Succeeded
		if (DistToTarget <= PlayerAttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] Tick - [%s] 플레이어 공격 범위 도달 (거리=%.1f). Succeeded"),
				*Pawn->GetName(), DistToTarget);
			AIC->StopMovement();
			return EStateTreeRunStatus::Succeeded;
		}

		// Stuck 감지 (토글: bUseStuckDetour)
		const bool bStuck = bUseStuckDetour
			? CTH::CheckPositionBasedStuck(D, Pawn->GetActorLocation(), DistToTarget, PlayerAttackRange,
				DeltaTime, StuckCheckInterval, StuckDistThreshold)
			: false;

		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		const bool bRepathDue     = D.TimeSinceRepath >= RepathInterval;
		const bool bTargetChanged = D.LastMoveTarget.Get() != TargetActor;
		const bool bAttackLocked = CTH::IsAttacking(Pawn);

		if (bTargetChanged)
		{
			D.ConsecutiveStuckCount = 0;
			D.DetourDirectionSign = 0;
			D.TimeUntilNextEQS = 0.f;
			D.StuckCheckTimer = 0.f;
			D.LastCheckedLocation = Pawn->GetActorLocation();
			AIC->StopMovement();
		}

		if (bAttackLocked && DistToTarget <= PlayerAttackRange + AcceptanceRadius + 100.f)
		{
			AIC->StopMovement();
			D.LastMoveTarget = TargetActor;
			return EStateTreeRunStatus::Running;
		}

		if (bTargetChanged || bRepathDue || bStuck || (bIdle && DistToTarget > PlayerAttackRange + 100.f))
		{
			if (bStuck)
			{
				const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
				const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
				CTH::IssueMoveToLocation(AIC,
					CTH::ComputeDetourGoal(Pawn->GetActorLocation(), TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign),
					AcceptanceRadius);
			}
			else if (bUseEQS && AttackPositionQuery && D.TimeUntilNextEQS <= 0.f)
			{
				CTH::RunPlayerAttackEQS(AttackPositionQuery, AIC, AcceptanceRadius, TargetActor);
				D.TimeUntilNextEQS = EQSInterval;
			}
			else
			{
				AIC->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
			}

			D.LastMoveTarget  = TargetActor;
			D.TimeSinceRepath = 0.f;
		}

		// 플레이어를 향해 회전 (정지 상태에서만 — 이동 중은 CharacterMovement가 처리)
		if (bIdle)
		{
			const FVector ToTarget = (TargetActor->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
			if (!ToTarget.IsNearlyZero())
			{
				Pawn->SetActorRotation(FMath::RInterpTo(
					Pawn->GetActorRotation(),
					FRotator(0.f, ToTarget.Rotation().Yaw, 0.f),
					DeltaTime, 5.f));
			}
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
	FInstanceDataType& D = Context.GetInstanceData(*this);

	// 슬롯 반납
	if ((D.AssignedSlotIndex >= 0 || D.AssignedSectorIndex >= 0) && IsValid(D.AIController))
	{
		const FHellunaAITargetData& TD = D.TargetData;
		if (TD.HasValidTarget())
		{
			if (USpaceShipAttackSlotManager* SlotMgr = CTH::GetSlotManager(TD.TargetActor.Get()))
			{
				if (IsValid(D.AIController))
				{
					SlotMgr->ReleaseEngagementReservation(D.AIController->GetPawn());
					UE_LOG(LogChaseTarget, Log,
						TEXT("[Chase] ExitState 슬롯 반납: %d"), D.AssignedSlotIndex);
				}
			}
		}
		D.AssignedSlotIndex = -1;
		D.AssignedSectorIndex = -1;
		D.AssignedSlotLocation = FVector::ZeroVector;
		D.AssignedSectorLocation = FVector::ZeroVector;
		D.bSlotArrived = false;
		D.bSectorArrived = false;
		D.AssignedSlotMoveRetryCount = 0;
	}

	if (IsValid(D.AIController))
	{
		UE_LOG(LogChaseTarget, Log,
			TEXT("[Chase] ExitState - [%s] | StuckCount=%d"),
			IsValid(D.AIController->GetPawn()) ? *D.AIController->GetPawn()->GetName() : TEXT("NoPawn"),
			D.ConsecutiveStuckCount);

		D.AIController->StopMovement();
	}
}
