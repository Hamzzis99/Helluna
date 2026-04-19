/**
 * STTask_ChaseTarget_Test.cpp
 *
 * ?怨쀯폒?????쟿??곷선 ???? ?곕뗄??Task (v2).
 *
 * ?????? ?怨쀯폒????猷?(筌왖?類ㅻ퓠 ?얠궢???怨쀯폒?????? ????????????????????????????????????
 *
 *  [Phase 1: ?癒?탢?? 餓λ쵐?뽩쳞怨뺚봺 > SlotEngageRadius
 *    NavMesh ??餓λ쵌而?野껋럩??癒?몵嚥??怨쀯폒??獄쎻뫚堉??臾롫젏.
 *    NavMesh 野껋럡???袁⑤뼎 ??pathfinding OFF 筌욊낯????猷?
 *
 *  [Phase 2: 域뱀눊援끿뵳????? bUseSlotSystem && SlotManager ?醫륁뒞
 *    SlotManager?癒?퐣 獄쏄퀣?숃쳸?? NavMesh 野꺜筌??袁⑺뒄嚥???猷?
 *    ?袁⑷컩 ??OccupySlot ??Running ?醫? (Attack State ?袁れ넎 ??疫?.
 *    Stuck/野껋럥以??쎈솭 ??????獄쏆꼶沅?????媛??
 *
 *  [Phase 2': 域뱀눊援끿뵳??癒??] ????OFF ?癒?뮉 ??????곸벉
 *    NavMesh 野껋럩????臾롫젏 + NavMesh 野껋럡????筌욊낯????猷?
 *
 * ?????? ???쟿??곷선 ??猷???????????????????????????????????????????????????????????????????????????????????????
 *  RepathInterval筌띾뜄??MoveToActor / EQS ??而??
 *  Stuck ????뺣쑁 ?怨좎돳 ???????
 *  DistanceToTarget <= PlayerAttackRange ??Succeeded.
 *
 * @author 繹먃沃섏눘??
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
#include "Helluna.h"  // HELLUNA_DEBUG_ENEMY

DEFINE_LOG_CATEGORY_STATIC(LogChaseTarget, Log, All);

namespace ChaseTargetTestHelpers {

bool ForceDirectMove(APawn* Pawn, const FVector& Direction, float DeltaTime);

const TCHAR* PathRequestResultToString(EPathFollowingRequestResult::Type Result)
{
	switch (Result)
	{
	case EPathFollowingRequestResult::Failed:
		return TEXT("Failed");
	case EPathFollowingRequestResult::AlreadyAtGoal:
		return TEXT("AlreadyAtGoal");
	case EPathFollowingRequestResult::RequestSuccessful:
		return TEXT("RequestSuccessful");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* PathFollowingStatusToString(EPathFollowingStatus::Type Status)
{
	switch (Status)
	{
	case EPathFollowingStatus::Idle:
		return TEXT("Idle");
	case EPathFollowingStatus::Waiting:
		return TEXT("Waiting");
	case EPathFollowingStatus::Paused:
		return TEXT("Paused");
	case EPathFollowingStatus::Moving:
		return TEXT("Moving");
	default:
		return TEXT("Unknown");
	}
}

void LogMoveToActorCall(
	AAIController* AIC,
	APawn* Pawn,
	AActor* TargetActor,
	const TCHAR* Phase,
	EPathFollowingRequestResult::Type Result,
	float SurfaceDist,
	float CenterDist,
	int32 SlotIndex)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	EPathFollowingStatus::Type PFCStatus = EPathFollowingStatus::Idle;
	if (UPathFollowingComponent* PFC = IsValid(AIC) ? AIC->GetPathFollowingComponent() : nullptr)
	{
		PFCStatus = PFC->GetStatus();
	}

	const FString Signature = FString::Printf(TEXT("%s|%d|%d"), Phase, (int32)Result, SlotIndex);
	static TMap<uint32, FString> LastSignatureByPawn;
	const uint32 PawnId = Pawn->GetUniqueID();
	if (const FString* LastSignature = LastSignatureByPawn.Find(PawnId))
	{
		if (*LastSignature == Signature)
		{
			return;
		}
	}
	LastSignatureByPawn.Add(PawnId, Signature);

#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Warning,
		TEXT("[enemybugreport][MoveToActorCall] Pawn=%s Phase=%s Slot=%d Result=%s PFC=%s SurfDist=%.1f CenterDist=%.1f Pos=%s Target=%s"),
		*Pawn->GetName(),
		Phase,
		SlotIndex,
		PathRequestResultToString(Result),
		PathFollowingStatusToString(PFCStatus),
		SurfaceDist,
		CenterDist,
		*Pawn->GetActorLocation().ToString(),
		IsValid(TargetActor) ? *TargetActor->GetName() : TEXT("null"));
#endif
}

void LogMoveToActorStopReason(
	AAIController* AIC,
	APawn* Pawn,
	AActor* TargetActor,
	const TCHAR* Reason,
	float SurfaceDist,
	float CenterDist,
	int32 SlotIndex,
	bool bIdle,
	bool bStuck,
	bool bAttackLocked)
{
	if (!IsValid(Pawn))
	{
		return;
	}

	EPathFollowingStatus::Type PFCStatus = EPathFollowingStatus::Idle;
	if (UPathFollowingComponent* PFC = IsValid(AIC) ? AIC->GetPathFollowingComponent() : nullptr)
	{
		PFCStatus = PFC->GetStatus();
	}

	const FString Signature = FString::Printf(
		TEXT("%s|%d|%d|%d|%d|%s"),
		Reason,
		SlotIndex,
		(int32)bIdle,
		(int32)bStuck,
		(int32)bAttackLocked,
		PathFollowingStatusToString(PFCStatus));
	static TMap<uint32, FString> LastSignatureByPawn;
	const uint32 PawnId = Pawn->GetUniqueID();
	if (const FString* LastSignature = LastSignatureByPawn.Find(PawnId))
	{
		if (*LastSignature == Signature)
		{
			return;
		}
	}
	LastSignatureByPawn.Add(PawnId, Signature);

#if HELLUNA_DEBUG_ENEMY
	UE_LOG(LogTemp, Warning,
		TEXT("[enemybugreport][MoveToActorStopReason] Pawn=%s Reason=%s Slot=%d PFC=%s Idle=%d Stuck=%d AttackLocked=%d SurfDist=%.1f CenterDist=%.1f Pos=%s Target=%s"),
		*Pawn->GetName(),
		Reason,
		SlotIndex,
		PathFollowingStatusToString(PFCStatus),
		(int32)bIdle,
		(int32)bStuck,
		(int32)bAttackLocked,
		SurfaceDist,
		CenterDist,
		*Pawn->GetActorLocation().ToString(),
		IsValid(TargetActor) ? *TargetActor->GetName() : TEXT("null"));
#endif
}

void IssueDirectMoveTowardLocation(AAIController* AIC, APawn* Pawn, const FVector& Goal, float AcceptanceRadius, bool bNeedForceMove, float DeltaTime)
{
	if (!IsValid(AIC) || !IsValid(Pawn))
	{
		return;
	}

	const FVector PawnLoc = Pawn->GetActorLocation();
	FVector GoalPoint = Goal;
	GoalPoint.Z = PawnLoc.Z;

	const float DistToGoal = FVector::Dist2D(PawnLoc, GoalPoint);
	if (DistToGoal < 10.f)
	{
		return;
	}

	bool bIsIdle = true;
	if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
	{
		bIsIdle = (PFC->GetStatus() != EPathFollowingStatus::Moving);
	}

	const FVector DirToGoal = (GoalPoint - PawnLoc).GetSafeNormal2D();
	if (DirToGoal.IsNearlyZero())
	{
		return;
	}

	if (bIsIdle)
	{
		if (bNeedForceMove)
		{
			ForceDirectMove(Pawn, DirToGoal, DeltaTime);
		}
		else
		{
			Pawn->AddMovementInput(DirToGoal, 1.f);
		}
	}
	else
	{
		FAIMoveRequest Req;
		Req.SetGoalLocation(GoalPoint);
		Req.SetAcceptanceRadius(AcceptanceRadius);
		Req.SetUsePathfinding(false);
		Req.SetAllowPartialPath(true);
		AIC->MoveTo(Req);
	}
}

FVector ComputeDetourGoal(
	const FVector& PawnLoc,
	const FVector& TargetLoc,
	AAIController* AIC,
	float OffsetAmount,
	int32 DirectionSign = 0);

int32 ResolveDetourDirectionSign(
	FSTTask_ChaseTarget_TestInstanceData& D,
	bool bUsePersistentDirection);

// ?袁④컩 ?醫롫섧: ??猷?+ ??뺛늺 筌ㅼ뮄??臾믪젎 + pathfinding OFF 筌욊낯????猷?
bool IssueMoveToLocation(AAIController* AIC, const FVector& Goal, float Radius);
FVector ComputeClosestSurfacePoint(const FVector& FromLoc, const AActor* ToActor);
void IssueDirectMoveTowardShipSurface(AAIController* AIC, APawn* Pawn, AActor* Ship, float AcceptanceRadius, bool bNeedForceMove = false, float DeltaTime = 0.016f);
void IssueDirectMoveTowardLocation(AAIController* AIC, APawn* Pawn, const FVector& Goal, float AcceptanceRadius, bool bNeedForceMove = false, float DeltaTime = 0.016f);
bool TryComputePostEngagementGoal(
	AAIController* AIC,
	APawn* Pawn,
	AActor* Ship,
	const USpaceShipAttackSlotManager* SlotMgr,
	int32 SlotIndex,
	float AttackRange,
	FVector& OutGoal);
bool ForceDirectMove(APawn* Pawn, const FVector& Direction, float DeltaTime);

// ?袁④컩 ?醫롫섧: ?怨쀯폒??獄??袁⑹뵥筌왖 筌ｋ똾寃??롫뮉 ?⑤벊????λ땾
static bool IsUnderOrOnShip(UWorld* World, AActor* Ship, const FVector& Location)
{
	if (!World || !Ship) return false;

	FCollisionQueryParams TraceParams;

	// ?袁⑥쨮 Trace: ?怨쀯폒??獄쏅쵐?ㅿ쭪?
	{
		const FVector UpStart = Location + FVector(0, 0, 10.f);
		const FVector UpEnd   = Location + FVector(0, 0, 1500.f);
		FHitResult UpHit;
		if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, TraceParams))
		{
			if (UpHit.GetActor() == Ship) return true;
		}
	}

	// ?袁⑥삋嚥?Trace: ?怨쀯폒???袁⑹뵥筌왖
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
// ???? ?怨쀯폒??獄쎻뫚堉?NavMesh ??餓λ쵌而?野껋럩????④쑴沅?
//   ?怨쀯폒??獄??袁⑥쨮 ?????롫뮉 野껋럩??癒? 椰꾧퀡???랁? ????野껋럩??癒?뱽 ??뺣즲??뺣뼄.
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

	// ????椰꾧퀡?곭몴???뺣즲??곴퐣 ?醫륁뒞??野껋럩??癒?뱽 筌≪뼔???
	// (揶쎛繹먮슣??野껉퍓??? 80%, 60%, 40%, 20% 椰꾧퀡??
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
		if (StepDist < 80.f) continue;  // ??댭?揶쎛繹먮슣??쭖???쎄땁

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

		// ?袁⑹삺 ?袁⑺뒄?? ??댭?揶쎛繹먮슣??쭖???쎄땁
		if (FVector::Dist(PawnLoc, NavGoal.Location) < 80.f)
			continue;

		// ?怨쀯폒??獄??袁⑹뵥筌왖 筌ｋ똾寃?
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

	// 筌뤴뫀諭?筌욊낯苑?野껋럩??癒?뵠 ??쎈솭 ???袁⑹삺 ?袁⑺뒄 獄쏆꼹??(?類?)
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
// ???? ?袁⑺뒄嚥???猷?(MoveTo ??쎈솭 ??餓λ쵌而?野껋럩?????媛?
// ============================================================================
// IssueMoveToLocation 獄쏆꼹?싧첎? ??쇱젫 ??猷????뽰삂??뤿??遺? ???
// false = AlreadyAtGoal ?癒?뮉 Failed (??猷??븍뜃? ?怨밴묶)
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

		// ?怨뺤쨮??筌? 揶쏆늿? Pawn?癒?퐣 2?λ뜄彛??살춸 嚥≪뮄??

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
		// ?怨뺤쨮??筌? 揶쏆늿? Pawn?癒?퐣 2?λ뜄彛??살춸 嚥≪뮄??
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
// ???? ?怨쀯폒??獄쎻뫚堉???猷?
//   NavMesh 野껋럡????袁⑤뼎??롢늺 ?얜????띿쓺 筌욊낯彛??? ??꾪??類???뺣뼄.
//   (pathfinding OFF 筌욊낯彛?? ?怨쀯폒??筌롫뗄???袁⑥삋嚥??????뺣뮉 ?얜챷???醫딆뻣)
// ============================================================================
void IssueMoveTowardShip(
	AAIController* AIC,
	APawn* Pawn,
	AActor* Ship,
	float AcceptanceRadius,
	bool bNeedForceMove = false,
	float DeltaTime = 0.016f)
{
	const FVector PawnLoc = Pawn->GetActorLocation();
	const FVector ShipLoc = Ship->GetActorLocation();

	// ?怨뺤쨮??筌띻낯??嚥≪뮄??????
	auto ThrottledLog = [&](const TCHAR* Method, const FVector& Goal, float ExtraDist = -1.f)
	{
		(void)Method;
		(void)Goal;
		(void)ExtraDist;
	};

	FVector StableApproachGoal = FVector::ZeroVector;
	if (TryComputeStableShipApproachGoal(AIC, Pawn, Ship, StableApproachGoal))
	{
		const float GoalDist = FVector::Dist2D(PawnLoc, StableApproachGoal);
		ThrottledLog(TEXT("StableApproach"), StableApproachGoal, GoalDist);
		const bool bMoveStarted = IssueMoveToLocation(AIC, StableApproachGoal, AcceptanceRadius);
		if (!bMoveStarted)
		{
			// NavMesh 野껋럡???袁⑤뼎: AlreadyAtGoal ?癒?뮉 Failed ??pathfinding OFF 筌욊낯????猷?
			ThrottledLog(TEXT("DirectFallback(StableApproachFailed)"), ShipLoc);
			IssueDirectMoveTowardShipSurface(AIC, Pawn, Ship, AcceptanceRadius, bNeedForceMove, DeltaTime);
		}
		return;
	}

	const FVector NavGoal = ComputeNavGoalTowardShip(PawnLoc, ShipLoc, AIC, Ship);

	// 野껋럩??癒?뵠 ?袁⑹삺 ?袁⑺뒄?? 揶쏆늿?앾쭖?(??뽰쁽??獄쏆꼹?? ??NavMesh 野껋럡???袁⑤뼎, 筌욊낯????猷?
	if (FVector::Dist(PawnLoc, NavGoal) < 50.f)
	{
		ThrottledLog(TEXT("DirectFallback(NavGoalTooClose)"), ShipLoc);
		IssueDirectMoveTowardShipSurface(AIC, Pawn, Ship, AcceptanceRadius, bNeedForceMove, DeltaTime);
		return;
	}

	ThrottledLog(TEXT("NavGoal"), NavGoal, FVector::Dist2D(PawnLoc, NavGoal));
	const bool bMoveStarted = IssueMoveToLocation(AIC, NavGoal, AcceptanceRadius);
	if (!bMoveStarted)
	{
		// NavMesh ??猷???쎈솭 ??pathfinding OFF 筌욊낯????猷?
		ThrottledLog(TEXT("DirectFallback(NavGoalFailed)"), ShipLoc);
		IssueDirectMoveTowardShipSurface(AIC, Pawn, Ship, AcceptanceRadius, bNeedForceMove, DeltaTime);
	}
}

// ============================================================================
// ???? ?怨쀯폒????뺛늺 筌ㅼ뮄??臾믪젎 ?④쑴沅?
// ============================================================================
FVector ComputeClosestSurfacePoint(const FVector& FromLoc, const AActor* ToActor)
{
	if (!ToActor) return FromLoc;

	TArray<UPrimitiveComponent*> Prims;
	ToActor->GetComponents<UPrimitiveComponent>(Prims);

	float MinDist = MAX_FLT;
	FVector BestPoint = ToActor->GetActorLocation();

	// ShipCombatCollision ??볥젃 ?怨쀪퐨
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

	// ??媛? 筌뤴뫀諭??꾩뮆????뚮똾猷??곕뱜
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

bool TryComputePostEngagementGoal(
	AAIController* AIC,
	APawn* Pawn,
	AActor* Ship,
	const USpaceShipAttackSlotManager* SlotMgr,
	int32 SlotIndex,
	float AttackRange,
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

	FVector SurfacePoint = ComputeClosestSurfacePoint(PawnLoc, Ship);
	FVector Outward = (PawnLoc - SurfacePoint).GetSafeNormal2D();

	if (SlotMgr)
	{
		const TArray<FAttackSlot>& Slots = SlotMgr->GetSlots();
		if (Slots.IsValidIndex(SlotIndex))
		{
			const FAttackSlot& Slot = Slots[SlotIndex];
			const FVector SlotSurface = Slot.SurfaceLocation.IsZero() ? SurfacePoint : Slot.SurfaceLocation;
			SurfacePoint = SlotSurface;

			if (!Slot.SurfaceNormal.IsNearlyZero())
			{
				Outward = Slot.SurfaceNormal.GetSafeNormal2D();
			}
			else if (Outward.IsNearlyZero())
			{
				Outward = (SlotSurface - ShipLoc).GetSafeNormal2D();
			}
		}
	}

	if (Outward.IsNearlyZero())
	{
		Outward = (SurfacePoint - ShipLoc).GetSafeNormal2D();
	}

	if (Outward.IsNearlyZero())
	{
		return false;
	}

	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Outward).GetSafeNormal();
	const float DesiredSurfaceOffset = FMath::Clamp(AttackRange * 0.45f, 70.f, 140.f);
	const float RelaxedSurfaceOffset = FMath::Clamp(AttackRange * 0.75f, DesiredSurfaceOffset + 20.f, 220.f);
	const float LateralOffset = 90.f;

	TArray<FVector> CandidateGoals;
	CandidateGoals.Reserve(8);
	CandidateGoals.Add(SurfacePoint + Outward * DesiredSurfaceOffset);
	CandidateGoals.Add(SurfacePoint + Outward * RelaxedSurfaceOffset);
	if (!Tangent.IsNearlyZero())
	{
		CandidateGoals.Add(SurfacePoint + Outward * DesiredSurfaceOffset + Tangent * LateralOffset);
		CandidateGoals.Add(SurfacePoint + Outward * DesiredSurfaceOffset - Tangent * LateralOffset);
		CandidateGoals.Add(SurfacePoint + Outward * RelaxedSurfaceOffset + Tangent * LateralOffset);
		CandidateGoals.Add(SurfacePoint + Outward * RelaxedSurfaceOffset - Tangent * LateralOffset);
	}
	CandidateGoals.Add(FVector(SurfacePoint.X, SurfacePoint.Y, PawnLoc.Z));
	CandidateGoals.Add(PawnLoc + (SurfacePoint - PawnLoc).GetSafeNormal2D() * FMath::Max(AttackRange * 0.5f, 120.f));

	const FVector ProjectionExtent(240.f, 240.f, 1200.f);
	float BestScore = MAX_FLT;
	FVector BestGoal = FVector::ZeroVector;

	for (const FVector& CandidateGoal : CandidateGoals)
	{
		FNavLocation NavLocation;
		if (!NavSys->ProjectPointToNavigation(CandidateGoal, NavLocation, ProjectionExtent))
		{
			continue;
		}

		if (IsUnderOrOnShip(AIC->GetWorld(), Ship, NavLocation.Location))
		{
			continue;
		}

		const float MoveDist2D = FVector::Dist2D(PawnLoc, NavLocation.Location);
		if (MoveDist2D < 10.f)
		{
			continue;
		}

		const float SurfaceGap = FVector::Dist2D(NavLocation.Location, SurfacePoint);
		const float Score =
			FMath::Abs(SurfaceGap - DesiredSurfaceOffset) * 4.f +
			MoveDist2D * 0.35f +
			FMath::Abs(NavLocation.Location.Z - PawnLoc.Z) * 0.2f;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestGoal = NavLocation.Location;
		}
	}

	if (BestScore == MAX_FLT)
	{
		return false;
	}

	OutGoal = BestGoal;
	return true;
}

// ============================================================================
// ???? NavMesh 野껋럡???袁⑤뼎 ???怨쀯폒????뺛늺繹먮슣? 筌욊낯????猷?(pathfinding OFF)
//   ?怨쀯폒??筌롫뗄????뺛늺??筌ㅼ뮄??臾믪젎??筌뤴뫚紐닸에?筌욊낯苑???猷?
//   CharacterMovementComponent揶쎛 ?꾩뮆???筌ｌ꼶????嚥?筌롫뗄???온????곸벉.
// ============================================================================
void IssueDirectMoveTowardShipSurface(AAIController* AIC, APawn* Pawn, AActor* Ship, float AcceptanceRadius, bool bNeedForceMove, float DeltaTime)
{
	if (!IsValid(AIC) || !IsValid(Pawn) || !IsValid(Ship)) return;

	const FVector PawnLoc = Pawn->GetActorLocation();
	FVector SurfacePoint = ComputeClosestSurfacePoint(PawnLoc, Ship);

	// ??뺛늺?癒?벥 Z??Pawn ?誘れ뵠嚥?癰귣똻??(筌왖???袁⑥삋嚥??????뺣뮉 野?獄쎻뫗?)
	SurfacePoint.Z = PawnLoc.Z;

	const float DistToSurface = FVector::Dist2D(PawnLoc, SurfacePoint);
	if (DistToSurface < 10.f) return;

	// Idle ?怨밴묶(MoveTo ??쑵????????춸 AddMovementInput ????
	// Moving ?怨밴묶筌?MoveTo揶쎛 ??뽯선 餓λ쵐?좄첋?嚥?揶쏄쑴苑??? ??놁벉
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
			// AddMovementInput??2????곴맒 ??쎈솭 ??SetActorLocation(sweep)??곗쨮 揶쏅벡????猷?
			if (bNeedForceMove)
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
		// Moving ?怨밴묶: MoveTo(pathfinding OFF)嚥?筌뤴뫚紐?揶쏄퉮?딉쭕?
		FAIMoveRequest Req;
		Req.SetGoalLocation(SurfacePoint);
		Req.SetAcceptanceRadius(AcceptanceRadius);
		Req.SetUsePathfinding(false);
		Req.SetAllowPartialPath(true);
		AIC->MoveTo(Req);
	}

	// ?怨뺤쨮??筌? 揶쏆늿? Pawn?癒?퐣 2?λ뜄彛??살춸 嚥≪뮄??
}

// ============================================================================
// ???? ?怨좎돳 ??쎈늄??筌뤴뫚紐??④쑴沅?(??????뺣쑁)
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
// ???? EQS ??쎈뻬 ??野껉퀗???袁⑺뒄嚥???猷?(???쟿??곷선 ?袁⑹뒠)
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
// ???? ?⑤벀爰???볥젃 ?類ㅼ뵥
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
// ???? ??뺛늺 椰꾧퀡???④쑴沅?
//   ?怨쀯폒?醫롮퓗????由겼첎? ??Actor??餓λ쵐???椰꾧퀡?곩첎? ?袁⑤빒 筌롫뗄????뺛늺繹먮슣? 筌ㅼ뮆??椰꾧퀡??
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
// ???? SpaceShipAttackSlotManager 揶쎛?紐꾩궎疫?
// ============================================================================
USpaceShipAttackSlotManager* GetSlotManager(AActor* SpaceShip)
{
	if (!SpaceShip) return nullptr;
	return SpaceShip->FindComponentByClass<USpaceShipAttackSlotManager>();
}

// ============================================================================
// ???? ?袁⑺뒄 癰궰?遺얠쎗 疫꿸퀡而?Stuck 揶쏅Ŋ?
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

// ============================================================================
// 헬퍼: 우주선 방향 진척도 기반 Stuck 감지
//   위치가 변해도 SurfaceDist가 줄지 않으면 "무진척" 판정.
//   진동(oscillation)을 정확히 잡아냄.
// ============================================================================
bool CheckProgressTowardTarget(
	FSTTask_ChaseTarget_TestInstanceData& D,
	float CurrentSurfaceDist,
	float DeltaTime,
	float CheckInterval = 1.0f,
	float MinProgressDist = 20.f)
{
	D.ProgressCheckTimer += DeltaTime;

	if (D.ProgressCheckTimer < CheckInterval)
		return false; // 체크 주기 아직 안 됨

	// SurfaceDist가 MinProgressDist 이상 줄었으면 진척 있음
	const float ProgressMade = D.LastProgressSurfaceDist - CurrentSurfaceDist;
	const bool bNoProgress = (ProgressMade < MinProgressDist);

	if (bNoProgress)
		D.ConsecutiveNoProgressCount++;
	else
		D.ConsecutiveNoProgressCount = 0;

	D.LastProgressSurfaceDist = CurrentSurfaceDist;
	D.ProgressCheckTimer = 0.f;

	return bNoProgress;
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
// ???? AddMovementInput ??쎈솭 ??揶쏅벡????猷?(SetActorLocation + sweep)
//   CMC 獄쏅뗀??野꺜筌앹빘???怨좎돳??뤿연 NavMesh ?節??癒?퐣????猷?揶쎛??
//   Sweep=true嚥??꾩뮆??袁? 鈺곕똻夷?
// ============================================================================
// CMC MOVE_None toggle - disable CMC physics entirely so SetActorLocation sticks
void SetForceMoveCMC(APawn* Pawn, bool bEnable)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character) return;
	UCharacterMovementComponent* CMC = Character->GetCharacterMovement();
	if (!CMC) return;

	if (bEnable && CMC->MovementMode != EMovementMode::MOVE_None)
	{
		CMC->DisableMovement(); // MOVE_None: no physics tick, no position snapback
		UE_LOG(LogTemp, Warning, TEXT("[enemybugreport][CMC] %s -> MOVE_None (ForceMove)"), *Pawn->GetName());
	}
	else if (!bEnable && CMC->MovementMode == EMovementMode::MOVE_None)
	{
		CMC->SetMovementMode(EMovementMode::MOVE_Walking);
		UE_LOG(LogTemp, Warning, TEXT("[enemybugreport][CMC] %s -> Walking (progress resumed)"), *Pawn->GetName());
	}
}

bool ForceDirectMove(APawn* Pawn, const FVector& Direction, float DeltaTime)
{
	if (!IsValid(Pawn) || Direction.IsNearlyZero()) return false;

	ACharacter* Character = Cast<ACharacter>(Pawn);
	float MoveSpeed = 400.f;
	if (Character)
	{
		if (const UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
		{
			MoveSpeed = CMC->MaxWalkSpeed;
		}
	}

	const FVector PrevLoc = Pawn->GetActorLocation();
	const FVector HorizDir = Direction.GetSafeNormal2D();

	// 1st try: horizontal + slight upward (slope handling)
	const FVector MoveDir = (HorizDir + FVector(0.f, 0.f, 0.15f)).GetSafeNormal();
	const FVector Delta = MoveDir * MoveSpeed * DeltaTime;
	FHitResult Hit;
	bool bMoved = Pawn->SetActorLocation(PrevLoc + Delta, true, &Hit);

	// 2nd try: steeper upward
	if (!bMoved || Hit.bBlockingHit)
	{
		const FVector SteepDir = (HorizDir + FVector(0.f, 0.f, 0.5f)).GetSafeNormal();
		const FVector SteepDelta = SteepDir * MoveSpeed * DeltaTime;
		FHitResult SteepHit;
		const bool bSteepMoved = Pawn->SetActorLocation(PrevLoc + SteepDelta, true, &SteepHit);
		if (bSteepMoved && !SteepHit.bBlockingHit)
		{
			bMoved = true;
		}
	}

	// 3rd try: purely horizontal (no Z offset, in case slope logic overshoots)
	if (!bMoved || (Hit.bBlockingHit && FVector::Dist(PrevLoc, Pawn->GetActorLocation()) < 1.f))
	{
		const FVector FlatDelta = HorizDir * MoveSpeed * DeltaTime;
		FHitResult FlatHit;
		const bool bFlatMoved = Pawn->SetActorLocation(PrevLoc + FlatDelta, true, &FlatHit);
		if (bFlatMoved && !FlatHit.bBlockingHit)
		{
			bMoved = true;
		}
	}

	const float ActualMoveDist = FVector::Dist(PrevLoc, Pawn->GetActorLocation());

	{
		static TMap<uint32, double> ForceMovLogTimes;
		const uint32 PawnId = Pawn->GetUniqueID();
		const double Now = FPlatformTime::Seconds();
		double& LastTime = ForceMovLogTimes.FindOrAdd(PawnId, 0.0);
		if (Now - LastTime >= 2.0)
		{
			LastTime = Now;
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][ForceDirectMove] Pawn=%s Moved=%d ActualDist=%.1f Speed=%.0f Hit=%d Pos=%s Dir=%s"),
				*Pawn->GetName(), (int32)bMoved, ActualMoveDist, MoveSpeed,
				(int32)Hit.bBlockingHit,
				*Pawn->GetActorLocation().ToString(),
				*HorizDir.ToString());
		}
	}

	return bMoved;
}

} // namespace ChaseTargetTestHelpers
// Unity Build?癒?퐣 ChaseTargetHelpers?? ??已??겸뫖猷?獄쎻뫗????袁る퉸
// using namespace ????筌뤿굞?????쇱뿫??쎈읂??곷뮞 ????
namespace CTH = ChaseTargetTestHelpers;

// ============================================================================
// EnterState
// ============================================================================
#if 0
EStateTreeRunStatus FSTTask_ChaseTarget_Test::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);

	AAIController* AIC = D.AIController;
	if (!IsValid(AIC))
	{
		UE_LOG(LogChaseTarget, Warning, TEXT("[Chase] EnterState - AIController ?얜똾?? Failed 獄쏆꼹??));
		return EStateTreeRunStatus::Failed;
	}

	const FHellunaAITargetData& TD = D.TargetData;

	// ?紐꾨뮞??곷뮞 ?怨쀬뵠???λ뜃由??
	D.TimeSinceRepath       = 0.f;
	D.TimeUntilNextEQS      = 0.f;
	D.StuckCheckTimer       = 0.f;
	D.ConsecutiveStuckCount = 0;
	D.DetourDirectionSign   = 0;
	D.LastProgressSurfaceDist = MAX_FLT;
	D.ProgressCheckTimer    = 0.f;
	D.ConsecutiveNoProgressCount = 0;
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
		UE_LOG(LogChaseTarget, Log, TEXT("[Chase] EnterState - [%s] ?醫륁뒞????野???곸벉. Running ??疫?),
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
		TEXT("[Chase] EnterState - [%s] -> [%s] | ????%s | 椰꾧퀡??%.1f"),
		IsValid(Pawn) ? *Pawn->GetName() : TEXT("NoPawn"),
		*TargetActor->GetName(),
		bIsSpaceShip ? TEXT("?怨쀯폒??) : TEXT("???쟿??곷선"),
		TD.DistanceToTarget);

	if (!IsValid(Pawn))
	{
		D.LastMoveTarget = TargetActor;
		return EStateTreeRunStatus::Running;
	}

	// ???? ??猷???뽰삂 ????????????????????????????????????????????????????????????????????????????????????????
	if (bIsSpaceShip)
	{
		const float SurfaceDist = CTH::ComputeSurfaceDistance(Pawn->GetActorLocation(), TargetActor);

		// ??? ?⑤벀爰?甕곕뗄????됱뵠筌?Running ??疫?(Attack State ?袁れ넎 ??疫?
		if (SurfaceDist <= AttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] ?怨쀯폒???⑤벀爰?甕곕뗄????(??뺛늺椰꾧퀡??%.1f). Running ??疫?),
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

		// ????筌욊쑴???癒?젟: ??뺛늺椰꾧퀡??疫꿸퀣? (?怨쀯폒?醫롮뵠 ????餓λ쵐?뽩쳞怨뺚봺揶쎛 ??湲????嚥?
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

			// ????筌욊쑴??甕곕뗄???? ????獄쏄퀣????뺣즲
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
					TEXT("[Chase] EnterState - [%s] ????獄쏄퀣???源껊궗: %d -> %s"),
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
			// ?癒?탢???癒?뮉 ????沃섎챷沅?? NavMesh 野껋럩????臾롫젏
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

		// ???쟿??곷선: ??? ?⑤벀爰?甕곕뗄?욑쭖?筌앸맩??Succeeded
		if (ActualPlayerDist <= PlayerAttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] ???쟿??곷선 ?⑤벀爰?甕곕뗄???? Succeeded"),
				*Pawn->GetName());
			return EStateTreeRunStatus::Succeeded;
		}

		AIC->MoveToActor(TargetActor, AcceptanceRadius, true, true, false, nullptr, true);
	}

	D.LastMoveTarget = TargetActor;
	return EStateTreeRunStatus::Running;
}
#endif

EStateTreeRunStatus FSTTask_ChaseTarget_Test::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& D = Context.GetInstanceData(*this);

	AAIController* AIC = D.AIController;
	if (!IsValid(AIC))
	{
		UE_LOG(LogChaseTarget, Warning, TEXT("[Chase] EnterState - AIController is null. Failed."));
		return EStateTreeRunStatus::Failed;
	}

	const FHellunaAITargetData& TD = D.TargetData;

	D.TimeSinceRepath = 0.f;
	D.TimeUntilNextEQS = 0.f;
	D.StuckCheckTimer = 0.f;
	D.ConsecutiveStuckCount = 0;
	D.DetourDirectionSign = 0;
	D.LastProgressSurfaceDist = MAX_FLT;
	D.ProgressCheckTimer = 0.f;
	D.ConsecutiveNoProgressCount = 0;
	D.AssignedSlotIndex = -1;
	D.AssignedSectorIndex = -1;
	D.AssignedSlotLocation = FVector::ZeroVector;
	D.AssignedSectorLocation = FVector::ZeroVector;
	D.SlotRetryTimer = 0.f;
	D.bSlotArrived = false;
	D.bSectorArrived = false;
	D.WaitingDestination = FVector::ZeroVector;
	D.bHasWaitingDestination = false;
	D.ConsecutiveSlotFailures = 0;
	D.AssignedSlotMoveRetryCount = 0;
	D.SimpleMoveDetourGoal = FVector::ZeroVector;
	D.bSimpleMoveDetourActive = false;
	D.SimpleMoveDetourDirectionSign = 0;

	APawn* Pawn = AIC->GetPawn();
	if (IsValid(Pawn))
	{
		D.LastCheckedLocation = Pawn->GetActorLocation();
	}

	if (!TD.HasValidTarget() && !IsValid(CTH::ResolveFallbackSpaceShip(AIC)))
	{
		UE_LOG(LogChaseTarget, Log,
			TEXT("[Chase] EnterState - [%s] no valid target yet. Keep running."),
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
		TEXT("[Chase] EnterState - [%s] -> [%s] | Type=%s | Dist=%.1f"),
		IsValid(Pawn) ? *Pawn->GetName() : TEXT("NoPawn"),
		*TargetActor->GetName(),
		bIsSpaceShip ? TEXT("SpaceShip") : TEXT("Player"),
		TD.DistanceToTarget);

	if (!IsValid(Pawn))
	{
		D.LastMoveTarget = TargetActor;
		return EStateTreeRunStatus::Running;
	}

	if (bIsSpaceShip)
	{
		const float SurfaceDist = CTH::ComputeSurfaceDistance(Pawn->GetActorLocation(), TargetActor);

		if (SurfaceDist <= AttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] already in spaceship attack range (surfaceDist=%.1f). Keep running."),
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

			int32 SlotIdx = -1;
			FVector SlotLoc = FVector::ZeroVector;
			if (SlotMgr->RequestSlot(Pawn, SlotIdx, SlotLoc))
			{
				D.AssignedSlotIndex = SlotIdx;
				D.AssignedSlotLocation = SlotLoc;
				D.bSlotArrived = false;
				D.ConsecutiveSlotFailures = 0;
				D.AssignedSlotMoveRetryCount = 0;
				CTH::IssueMoveToLocation(AIC, SlotLoc, AcceptanceRadius);
				UE_LOG(LogChaseTarget, Log,
					TEXT("[Chase] EnterState - [%s] slot assigned: %d -> %s"),
					*Pawn->GetName(), SlotIdx, *SlotLoc.ToString());
			}
			else if (!TryAssignSectorMove())
			{
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius);
			}
		}
		else
		{
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
		if (ActualPlayerDist <= PlayerAttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
				TEXT("[Chase] EnterState - [%s] already in player attack range. Succeeded."),
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

	// Version marker (once per game session)
	{
		static bool bVersionLogged = false;
		if (!bVersionLogged)
		{
			bVersionLogged = true;
			UE_LOG(LogTemp, Warning, TEXT("[enemybugreport][ChaseTarget_Version] v6-DetourPathfinding ACTIVE"));
		}
	}

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
		// ?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름
		// ?怨쀯폒????猷?
		// ?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름

		const FVector PawnLoc = Pawn->GetActorLocation();
		const float SurfaceDist = CTH::ComputeSurfaceDistance(PawnLoc, TargetActor);
		const float CenterDist = FVector::Dist(PawnLoc, TargetActor->GetActorLocation());
		const bool bAttackLocked = CTH::IsAttacking(Pawn);

		if (bAttackLocked && SurfaceDist <= AttackRange + AcceptanceRadius + 100.f)
		{
			CTH::LogMoveToActorStopReason(
				AIC,
				Pawn,
				TargetActor,
				TEXT("AttackLockedNearShip"),
				SurfaceDist,
				CenterDist,
				D.AssignedSlotIndex,
				false,
				false,
				true);
			AIC->StopMovement();
			D.LastMoveTarget = TargetActor;
			return EStateTreeRunStatus::Running;
		}

		// ???? ?遺얠쒔域?嚥≪뮄??(2?λ뜄彛?? ????????????????????????????????????????????????????????
		static float ShipDbgTimer = 0.f;
		ShipDbgTimer += DeltaTime;
		if (ShipDbgTimer >= 2.f)
		{
			ShipDbgTimer = 0.f;
			UE_LOG(LogChaseTarget, Log,
                TEXT("[Chase] Tick - [%s] ship chase | SurfaceDist=%.1f CenterDist=%.1f | SlotIdx=%d SlotArrived=%d | PFC=%d"),
				*Pawn->GetName(), SurfaceDist, CenterDist,
				D.AssignedSlotIndex, (int)D.bSlotArrived,
				AIC->GetPathFollowingComponent() ? (int32)AIC->GetPathFollowingComponent()->GetStatus() : -1);
		}

        // Position-based stuck detection.
		const bool bStuck = bUseStuckDetour
			? CTH::CheckPositionBasedStuck(D, PawnLoc, SurfaceDist, AttackRange,
				DeltaTime, StuckCheckInterval, StuckDistThreshold)
			: false;

		if (bStuck)
		{
			UE_LOG(LogChaseTarget, Warning,
                TEXT("[Chase] Tick - [%s] *** STUCK *** | Count=%d | SurfaceDist=%.1f"),
				*Pawn->GetName(), D.ConsecutiveStuckCount, SurfaceDist);
		}

		// ── 진척도 체크 (우주선 접근 중에만) ────────────────
		// AttackRange 이내 = 우주선 충분히 가까움 → 진척도 체크 무의미, 계속 밀어붙이기
		// AttackRange 밖 = 아직 접근 중 → SurfaceDist 감소 여부 체크
		bool bNoProgress = false;
		if (SurfaceDist > AttackRange)
		{
			bNoProgress = CTH::CheckProgressTowardTarget(D, SurfaceDist, DeltaTime, 1.0f, 20.f);
		}
		else
		{
			// 우주선 근처 도착 → 진척 카운터 리셋 (AttackZone이 전환 판단)
			D.ConsecutiveNoProgressCount = 0;
			D.ProgressCheckTimer = 0.f;
			D.LastProgressSurfaceDist = SurfaceDist;
		}
		const bool bNeedForceMove = (D.ConsecutiveNoProgressCount >= 3);

#if HELLUNA_DEBUG_ENEMY
		if (bNoProgress)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][NoProgress] Pawn=%s NoProgressCount=%d SurfaceDist=%.1f NeedForce=%d"),
				*Pawn->GetName(), D.ConsecutiveNoProgressCount, SurfaceDist, (int32)bNeedForceMove);
		}
#endif

		// (MOVE_None 불필요 — 우회는 정상 pathfinding 사용)

		// ── Idle 감지 ───────────────────────────────────────
		bool bIdle = false;
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			bIdle = (PFC->GetStatus() == EPathFollowingStatus::Idle);

		// ???? ??????뽯뮞???브쑨由?????????????????????????????????????????????????????????????????
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

		// ═══════════════════════════════════════════════════════════
		// ★ bNeedForceMove: 같은 NavMesh 데드존에 막힘 → 큰 측면 우회
		//   MOVE_None/SetActorLocation 대신 정상 pathfinding으로 접근 각도 변경
		//   (광폭화 시 다양한 각도로 접근하면 도달하는 것과 같은 원리)
		// ═══════════════════════════════════════════════════════════
		if (bNeedForceMove)
		{
			// 큰 우회 (600~800cm) → NavMesh 위 다른 지점으로 이동 후 재접근
			const int32 DetourDir = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
			const float DetourDist = 600.f + FMath::FRandRange(0.f, 200.f);
			const FVector DetourGoal = CTH::ComputeDetourGoal(
				PawnLoc, TargetActor->GetActorLocation(), AIC, DetourDist, DetourDir);

			CTH::IssueMoveToLocation(AIC, DetourGoal, AcceptanceRadius);

#if HELLUNA_DEBUG_ENEMY
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][ForceDetour] Pawn=%s Detour=%s Dist=%.0f Dir=%d SurfDist=%.1f"),
				*Pawn->GetName(), *DetourGoal.ToString(), DetourDist, DetourDir, SurfaceDist);
#endif

			// 카운터 리셋 → 우회에 시간 줌 (다음 3초간 정상 pathfinding)
			D.ConsecutiveNoProgressCount = 0;
			D.ProgressCheckTimer = 0.f;
			D.LastProgressSurfaceDist = SurfaceDist;
			D.TimeSinceRepath = 0.f;
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
			// ?怨뺤쨮??筌? 5?λ뜄彛??
			if (SlotMgr->GetSlots().Num() == 0)
			{
				SlotMgr = nullptr;
			}
		}

		if (SlotMgr || SectorMgr)
		{
			// ?????? ??????뽯뮞??ON ??????????????????????????????????????????????????????????

			// ?⑤벀爰?甕곕뗄??域뱀눘荑? ?????癒?? + 筌욊낯????猷??④쑴??
			// (?類???롢늺 AttackZone 獄쏅벡?ゅ첎? ????곗몵沃샕嚥?筌롫뜆?쏉쭪? ??꾪??怨쀯폒????뺛늺繹먮슣? ?臾롫젏)
			if (SurfaceDist <= AttackRange)
			{
				D.WaitingDestination = FVector::ZeroVector;
				D.bHasWaitingDestination = false;

				if (D.AssignedSlotIndex >= 0)
				{
					SlotMgr->OccupySlot(D.AssignedSlotIndex, Pawn);
				}

				D.bSlotArrived = true;
				D.bSectorArrived = (D.AssignedSectorIndex >= 0);
				D.ConsecutiveSlotFailures = 0;

				// ?怨쀯폒?醫롮뱽 獄쏅뗀?よ퉪????筌욊낯???袁⑹춭 (AttackZone ?紐꺿봺椰꾧퀡留????돱筌왖)
				const FVector ToShip = (TargetActor->GetActorLocation() - PawnLoc).GetSafeNormal2D();
				if (!ToShip.IsNearlyZero())
				{
					AIC->SetFocalPoint(PawnLoc + ToShip * 1000.f);
					// 진척 없으면 ForceDirectMove, 아니면 AddMovementInput
					if (bNeedForceMove)
					{
						AIC->StopMovement();
						CTH::ForceDirectMove(Pawn, ToShip, DeltaTime);
					}
					else
					{
						Pawn->AddMovementInput(ToShip, 1.f);
					}
				}

				return EStateTreeRunStatus::Running;
			}

			// ?????諭곴숲 ?袁⑷컩 ?? ?怨쀯폒??獄쎻뫚堉??곗쨮 ?④쑴???袁⑹춭
			// (InAttackZone 鈺곌퀗援???紐꺿봺椰꾧퀡留????돱筌왖 ?臾롫젏)
			if (D.bSlotArrived || D.bSectorArrived)
			{

				const bool bRepathDue = D.TimeSinceRepath >= RepathInterval;

				if (bStuck)
				{
					CTH::LogMoveToActorStopReason(
						AIC,
						Pawn,
						TargetActor,
						TEXT("PostSlotAdvanceStuck"),
						SurfaceDist,
						CenterDist,
						D.AssignedSlotIndex,
						bIdle,
						true,
						bAttackLocked);

					AIC->StopMovement();
					const FVector DirectGoal =
						D.bHasWaitingDestination ? D.WaitingDestination : CTH::ComputeClosestSurfacePoint(PawnLoc, TargetActor);
					const FVector DirToGoal = (DirectGoal - PawnLoc).GetSafeNormal2D();
					if (!DirToGoal.IsNearlyZero())
					{
						CTH::ForceDirectMove(Pawn, DirToGoal, DeltaTime);
					}
					D.TimeSinceRepath = 0.f;
					return EStateTreeRunStatus::Running;
				}

				if (bIdle || bRepathDue)
				{
					FVector PostEngagementGoal = FVector::ZeroVector;
					if (CTH::TryComputePostEngagementGoal(
						AIC,
						Pawn,
						TargetActor,
						SlotMgr,
						D.AssignedSlotIndex,
						AttackRange,
						PostEngagementGoal))
					{
						D.WaitingDestination = PostEngagementGoal;
						D.bHasWaitingDestination = true;

						const bool bMoveStarted = CTH::IssueMoveToLocation(AIC, PostEngagementGoal, AcceptanceRadius);
						if (!bMoveStarted)
						{
							CTH::IssueDirectMoveTowardLocation(
								AIC,
								Pawn,
								PostEngagementGoal,
								AcceptanceRadius,
								bNeedForceMove,
								DeltaTime);
						}
					}
					else
					{
						D.WaitingDestination = FVector::ZeroVector;
						D.bHasWaitingDestination = false;
						CTH::IssueDirectMoveTowardShipSurface(
							AIC,
							Pawn,
							TargetActor,
							AcceptanceRadius,
							bNeedForceMove,
							DeltaTime);
					}
					D.TimeSinceRepath = 0.f;
				}
				else
				{
					// MoveToActor 실행 중이더라도 진척 없으면 ForceDirectMove 보조
					const FVector AssistGoal =
						D.bHasWaitingDestination ? D.WaitingDestination : CTH::ComputeClosestSurfacePoint(PawnLoc, TargetActor);
					const FVector DirToGoal = (AssistGoal - PawnLoc).GetSafeNormal2D();
					if (!DirToGoal.IsNearlyZero())
					{
						if (bNeedForceMove)
						{
							AIC->StopMovement();
							CTH::ForceDirectMove(Pawn, DirToGoal, DeltaTime);
						}
						else if (SurfaceDist < AttackRange * 1.8f)
						{
							Pawn->AddMovementInput(DirToGoal, 0.5f);
						}
					}
				}
				return EStateTreeRunStatus::Running;
			}

			// ????沃섎챶媛???怨밴묶
			if (D.AssignedSectorIndex >= 0 && SectorMgr)
			{
				const float DistToSector2D = FVector::Dist2D(PawnLoc, D.AssignedSectorLocation);
				if (DistToSector2D <= AcceptanceRadius + 30.f)
				{
					// ?諭곴숲 獄쎻뫚堉??袁⑷컩 ???怨쀯폒??獄쎻뫚堉??곗쨮 ?④쑴???袁⑹춭
					D.bSectorArrived = true;

					UE_LOG(LogChaseTarget, Log,
						TEXT("[Chase] Tick - [%s] sector arrived -> continue toward ship | Sector=%d Dist2D=%.1f SurfDist=%.1f"),
						*Pawn->GetName(), D.AssignedSectorIndex, DistToSector2D, SurfaceDist);

					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
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

			#if 0
			if (SlotMgr && D.AssignedSlotIndex < 0)
			{
				// ????筌욊쑴??甕곕뗄??獄? ?怨쀯폒??獄쎻뫚堉??臾롫젏 (??뺛늺椰꾧퀡??疫꿸퀣?)
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
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
					D.WaitingDestination = FVector::ZeroVector;

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
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				// ?怨쀫꺗 ??쎈솭 3????곴맒: ??????由? ?癒?? ??猷?
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
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				// ????筌욊쑴??甕곕뗄???? 獄쏄퀣???????(2???묅뫀???
				D.SlotRetryTimer -= DeltaTime;
				if (D.SlotRetryTimer <= 0.f)
				{
					D.SlotRetryTimer = 2.f;
					int32 SlotIdx = -1;
					FVector SlotLoc = FVector::ZeroVector;
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
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
						}
						return EStateTreeRunStatus::Running;
					}

					D.AssignedSlotIndex = SlotIdx;
					D.AssignedSlotLocation = SlotLoc;
					D.bSlotArrived = false;
					D.AssignedSlotMoveRetryCount = 0;

					const bool bMoveStarted = CTH::IssueMoveToLocation(AIC, SlotLoc, AcceptanceRadius);
					if (!bMoveStarted)
					{
						CTH::IssueDirectMoveTowardLocation(
							AIC,
							Pawn,
							SlotLoc,
							AcceptanceRadius,
							bNeedForceMove,
							DeltaTime);
					}
					D.TimeSinceRepath = 0.f;
				}

				const float DistToSlot2D = FVector::Dist2D(PawnLoc, D.AssignedSlotLocation);
				if (DistToSlot2D <= AcceptanceRadius + 30.f)
				{
					D.bSlotArrived = true;
					D.ConsecutiveSlotFailures = 0;
					D.AssignedSlotMoveRetryCount = 0;
					SlotMgr->OccupySlot(D.AssignedSlotIndex, Pawn);

					UE_LOG(LogChaseTarget, Log,
						TEXT("[Chase] Tick - [%s] Slot occupied -> continue toward ship | Slot=%d Dist2D=%.1f SurfDist=%.1f"),
						*Pawn->GetName(), D.AssignedSlotIndex, DistToSlot2D, SurfaceDist);

					FVector PostEngagementGoal = FVector::ZeroVector;
					if (CTH::TryComputePostEngagementGoal(
						AIC,
						Pawn,
						TargetActor,
						SlotMgr,
						D.AssignedSlotIndex,
						AttackRange,
						PostEngagementGoal))
					{
						D.WaitingDestination = PostEngagementGoal;
						D.bHasWaitingDestination = true;

						const bool bMoveStarted = CTH::IssueMoveToLocation(AIC, PostEngagementGoal, AcceptanceRadius);
						if (!bMoveStarted)
						{
							CTH::IssueDirectMoveTowardLocation(
								AIC,
								Pawn,
								PostEngagementGoal,
								AcceptanceRadius,
								bNeedForceMove,
								DeltaTime);
						}
					}
					else
					{
						D.WaitingDestination = FVector::ZeroVector;
						D.bHasWaitingDestination = false;
						CTH::IssueDirectMoveTowardShipSurface(
							AIC,
							Pawn,
							TargetActor,
							AcceptanceRadius,
							bNeedForceMove,
							DeltaTime);
					}
					D.TimeSinceRepath = 0.f;
					return EStateTreeRunStatus::Running;
				}

				if (!bIdle)
				{
					D.AssignedSlotMoveRetryCount = 0;
				}

				if ((bIdle || bStuck) && DistToSlot2D <= AcceptanceRadius + 200.f)
				{
					AIC->StopMovement();
					const FVector DirToSlot = (D.AssignedSlotLocation - PawnLoc).GetSafeNormal2D();
					if (!DirToSlot.IsNearlyZero())
					{
						if (bNeedForceMove)
						{
							CTH::ForceDirectMove(Pawn, DirToSlot, DeltaTime);
						}
						else
						{
							Pawn->AddMovementInput(DirToSlot, 1.f);
						}
					}
					return EStateTreeRunStatus::Running;
				}

				if (bIdle && DistToSlot2D > AcceptanceRadius + 200.f && D.TimeSinceRepath >= RepathInterval)
				{
					int32 RefreshedSlotIdx = D.AssignedSlotIndex;
					FVector RefreshedSlotLoc = D.AssignedSlotLocation;
					const bool bRefreshedSlot =
						SlotMgr->RequestSlot(Pawn, RefreshedSlotIdx, RefreshedSlotLoc) &&
						RefreshedSlotIdx == D.AssignedSlotIndex;

					if (bRefreshedSlot)
					{
						D.AssignedSlotLocation = RefreshedSlotLoc;
					}

					const bool bMoveStarted = CTH::IssueMoveToLocation(AIC, D.AssignedSlotLocation, AcceptanceRadius);
					if (!bMoveStarted)
					{
						CTH::IssueDirectMoveTowardLocation(
							AIC,
							Pawn,
							D.AssignedSlotLocation,
							AcceptanceRadius,
							bNeedForceMove,
							DeltaTime);
					}
					D.TimeSinceRepath = 0.f;
					D.AssignedSlotMoveRetryCount++;

					UE_LOG(LogChaseTarget, Warning,
						TEXT("[Chase] Tick - [%s] slot move retry | Slot=%d Retry=%d Refreshed=%d Dist2D=%.1f Goal=%s"),
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

            if (bStuck)
            {
                AIC->StopMovement();
                const FVector DirectGoal =
                    D.bHasWaitingDestination ? D.WaitingDestination : CTH::ComputeClosestSurfacePoint(PawnLoc, TargetActor);
                const FVector DirToGoal = (DirectGoal - PawnLoc).GetSafeNormal2D();
                if (!DirToGoal.IsNearlyZero())
                {
                    if (bNeedForceMove)
                    {
                        CTH::ForceDirectMove(Pawn, DirToGoal, DeltaTime);
                    }
                    else
                    {
                        Pawn->AddMovementInput(DirToGoal, 1.f);
                    }
                }
                return EStateTreeRunStatus::Running;
            }

            bool bNeedReassign = false;
            if (bIdle && DistToSlot2D > AcceptanceRadius + 200.f && D.AssignedSlotMoveRetryCount >= 3)
                bNeedReassign = true;

            if (bNeedReassign)
            {
                const bool bIdleRelease = !bStuck;

                UE_LOG(LogChaseTarget, Warning,
                    TEXT("[Chase] Tick - [%s] ?? ??? (??: %s), ?? ??=%d, ????=%d"),
                    *Pawn->GetName(), bStuck ? TEXT("Stuck") : TEXT("Idle"),
                    D.AssignedSlotIndex, D.ConsecutiveSlotFailures);

                SlotMgr->ReleaseSlotByIndex(D.AssignedSlotIndex);
                D.AssignedSlotIndex = -1;
                D.ConsecutiveStuckCount = 0;
                D.DetourDirectionSign = 0;
                D.AssignedSlotMoveRetryCount = 0;
                D.bSlotArrived = false;
                D.WaitingDestination = FVector::ZeroVector;
                D.bHasWaitingDestination = false;

                if (bIdleRelease)
                {
                    D.ConsecutiveSlotFailures++;
                    D.SlotRetryTimer = 2.f;
                    CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
                }
                else
                {
                    D.SlotRetryTimer = 0.5f;
                }
            }
        }
			#endif
			if (SlotMgr && D.AssignedSlotIndex < 0)
			{
				const float SlotReserveRadius = FMath::Clamp(SlotEngageRadius * 0.6f, AcceptanceRadius + 100.f, SlotEngageRadius);
				const float MaxSlotTravelDist = CTH::ComputeMaxSlotReservationTravelDistance(SlotMgr, SlotReserveRadius, AcceptanceRadius);
				float ClosestFreeSlotDist = MAX_FLT;
				const bool bHasFreeSlot = CTH::FindClosestFreeSlot(SlotMgr, PawnLoc, ClosestFreeSlotDist);

				if (SurfaceDist > SlotReserveRadius)
				{
					D.WaitingDestination = FVector::ZeroVector;
					D.bHasWaitingDestination = false;

					if (D.TimeSinceRepath >= RepathInterval || bIdle || bStuck)
					{
						if (bStuck)
						{
							const float Mult = FMath::Min((float)D.ConsecutiveStuckCount, 3.f);
							const int32 DetourDirectionSign = CTH::ResolveDetourDirectionSign(D, bUsePersistentDetourDirection);
							CTH::IssueMoveToLocation(
								AIC,
								CTH::ComputeDetourGoal(PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign),
								AcceptanceRadius);
						}
						else
						{
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
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
							CTH::IssueMoveToLocation(
								AIC,
								CTH::ComputeDetourGoal(PawnLoc, TargetActor->GetActorLocation(), AIC, DetourOffset * Mult, DetourDirectionSign),
								AcceptanceRadius);
						}
						else
						{
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

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
							CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
						}
						D.TimeSinceRepath = 0.f;
					}
					return EStateTreeRunStatus::Running;
				}

				D.SlotRetryTimer -= DeltaTime;
				if (D.SlotRetryTimer > 0.f)
				{
					return EStateTreeRunStatus::Running;
				}

				D.SlotRetryTimer = 2.f;
				int32 SlotIdx = -1;
				FVector SlotLoc = FVector::ZeroVector;
				if (!SlotMgr->RequestSlot(Pawn, SlotIdx, SlotLoc))
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
						CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
					}
					return EStateTreeRunStatus::Running;
				}

				D.AssignedSlotIndex = SlotIdx;
				D.AssignedSlotLocation = SlotLoc;
				D.bSlotArrived = false;
				D.AssignedSlotMoveRetryCount = 0;

				const bool bMoveStarted = CTH::IssueMoveToLocation(AIC, SlotLoc, AcceptanceRadius);
				if (!bMoveStarted)
				{
					CTH::IssueDirectMoveTowardLocation(
						AIC,
						Pawn,
						SlotLoc,
						AcceptanceRadius,
						bNeedForceMove,
						DeltaTime);
				}
				D.TimeSinceRepath = 0.f;
				return EStateTreeRunStatus::Running;
			}
			if (SlotMgr && D.AssignedSlotIndex >= 0)
			{
				const float DistToSlot2D = FVector::Dist2D(PawnLoc, D.AssignedSlotLocation);
				if (DistToSlot2D <= AcceptanceRadius + 30.f)
				{
					D.bSlotArrived = true;
					D.ConsecutiveSlotFailures = 0;
					D.AssignedSlotMoveRetryCount = 0;
					SlotMgr->OccupySlot(D.AssignedSlotIndex, Pawn);

					UE_LOG(LogChaseTarget, Log,
						TEXT("[Chase] Tick - [%s] Slot occupied -> continue toward ship | Slot=%d Dist2D=%.1f SurfDist=%.1f"),
						*Pawn->GetName(), D.AssignedSlotIndex, DistToSlot2D, SurfaceDist);

					FVector PostEngagementGoal = FVector::ZeroVector;
					if (CTH::TryComputePostEngagementGoal(
						AIC,
						Pawn,
						TargetActor,
						SlotMgr,
						D.AssignedSlotIndex,
						AttackRange,
						PostEngagementGoal))
					{
						D.WaitingDestination = PostEngagementGoal;
						D.bHasWaitingDestination = true;

						const bool bMoveStarted = CTH::IssueMoveToLocation(AIC, PostEngagementGoal, AcceptanceRadius);
						if (!bMoveStarted)
						{
							CTH::IssueDirectMoveTowardLocation(
								AIC,
								Pawn,
								PostEngagementGoal,
								AcceptanceRadius,
								bNeedForceMove,
								DeltaTime);
						}
					}
					else
					{
						D.WaitingDestination = FVector::ZeroVector;
						D.bHasWaitingDestination = false;
						CTH::IssueDirectMoveTowardShipSurface(
							AIC,
							Pawn,
							TargetActor,
							AcceptanceRadius,
							bNeedForceMove,
							DeltaTime);
					}
					D.TimeSinceRepath = 0.f;
					return EStateTreeRunStatus::Running;
				}

				if (!bIdle)
				{
					D.AssignedSlotMoveRetryCount = 0;
				}

				if ((bIdle || bStuck) && DistToSlot2D <= AcceptanceRadius + 200.f)
				{
					AIC->StopMovement();
					const FVector DirToSlot = (D.AssignedSlotLocation - PawnLoc).GetSafeNormal2D();
					if (!DirToSlot.IsNearlyZero())
					{
						if (bNeedForceMove)
						{
							CTH::ForceDirectMove(Pawn, DirToSlot, DeltaTime);
						}
						else
						{
							Pawn->AddMovementInput(DirToSlot, 1.f);
						}
					}
					return EStateTreeRunStatus::Running;
				}

				if (bIdle && DistToSlot2D > AcceptanceRadius + 200.f && D.TimeSinceRepath >= RepathInterval)
				{
					int32 RefreshedSlotIdx = D.AssignedSlotIndex;
					FVector RefreshedSlotLoc = D.AssignedSlotLocation;
					const bool bRefreshedSlot =
						SlotMgr->RequestSlot(Pawn, RefreshedSlotIdx, RefreshedSlotLoc) &&
						RefreshedSlotIdx == D.AssignedSlotIndex;

					if (bRefreshedSlot)
					{
						D.AssignedSlotLocation = RefreshedSlotLoc;
					}

					const bool bMoveStarted = CTH::IssueMoveToLocation(AIC, D.AssignedSlotLocation, AcceptanceRadius);
					if (!bMoveStarted)
					{
						CTH::IssueDirectMoveTowardLocation(
							AIC,
							Pawn,
							D.AssignedSlotLocation,
							AcceptanceRadius,
							bNeedForceMove,
							DeltaTime);
					}
					D.TimeSinceRepath = 0.f;
					D.AssignedSlotMoveRetryCount++;

					UE_LOG(LogChaseTarget, Warning,
						TEXT("[Chase] Tick - [%s] slot move retry | Slot=%d Retry=%d Refreshed=%d Dist2D=%.1f Goal=%s"),
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

				if (bStuck)
				{
					AIC->StopMovement();
					const FVector DirectGoal =
						D.bHasWaitingDestination ? D.WaitingDestination : CTH::ComputeClosestSurfacePoint(PawnLoc, TargetActor);
					const FVector DirToGoal = (DirectGoal - PawnLoc).GetSafeNormal2D();
					if (!DirToGoal.IsNearlyZero())
					{
						// Stuck 상태 = 항상 ForceDirectMove (AddMovementInput은 여기서 무력)
						CTH::ForceDirectMove(Pawn, DirToGoal, DeltaTime);
					}
					return EStateTreeRunStatus::Running;
				}

				bool bNeedReassign = false;
				if (bIdle && DistToSlot2D > AcceptanceRadius + 200.f && D.AssignedSlotMoveRetryCount >= 3)
				{
					bNeedReassign = true;
				}

				if (bNeedReassign)
				{
					UE_LOG(LogChaseTarget, Warning,
						TEXT("[Chase] Tick - [%s] slot reassigned (reason=%s), previousSlot=%d, consecutiveFailures=%d"),
						*Pawn->GetName(),
						bStuck ? TEXT("Stuck") : TEXT("Idle"),
						D.AssignedSlotIndex,
						D.ConsecutiveSlotFailures);

					SlotMgr->ReleaseSlotByIndex(D.AssignedSlotIndex);
					D.AssignedSlotIndex = -1;
					D.ConsecutiveStuckCount = 0;
					D.DetourDirectionSign = 0;
					D.AssignedSlotMoveRetryCount = 0;
					D.bSlotArrived = false;
					D.WaitingDestination = FVector::ZeroVector;
					D.bHasWaitingDestination = false;
					D.ConsecutiveSlotFailures++;
					D.SlotRetryTimer = 2.f;
					CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
					return EStateTreeRunStatus::Running;
				}

			return EStateTreeRunStatus::Running;
		}
		}
		else
		{
			// ?????? ??????뽯뮞??OFF (?癒?? ??猷? ????????????????????????????????
			// ??猷?餓?Moving)????Repath??椰꾨?瑗?怨쀫선 野껋럥以??귐딅?筌욊쑬猷?獄쎻뫗?
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
                    TEXT("[Chase] Tick - [%s] ship stuck detour | Mult=%.1f"),
					*Pawn->GetName(), Mult);

				CTH::IssueMoveToLocation(AIC, DetourGoal, AcceptanceRadius);
				D.TimeSinceRepath = 0.f;
			}
			else if (bIdle && SurfaceDist > AttackRange + 100.f)
			{
                // Re-issue move if idle and still out of range.
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
				D.TimeSinceRepath = 0.f;
			}
			else if (!bMoving && D.TimeSinceRepath >= RepathInterval)
			{
                // Repath only when not already moving.
				CTH::IssueMoveTowardShip(AIC, Pawn, TargetActor, AcceptanceRadius, bNeedForceMove, DeltaTime);
				D.TimeSinceRepath = 0.f;
			}
		}

		// ?怨쀯폒??獄쎻뫚堉????읈 (?????袁⑷컩 ?怨밴묶揶쎛 ?袁⑤빜 ??
		// CharacterMovement??bOrientRotationToMovement揶쎛 ??猷?獄쎻뫚堉????읈??筌ｌ꼶????嚥?
		// ??猷?餓λ쵐肉????롫짗 ???읈????? ??낅뮉??(????뽯뮞??뽰뵠 ?겸뫖猷??롢늺 ??筌〓떽援끿뵳?獄쏆뮇源?.
		// ?類? ?怨밴묶(Idle)?癒?퐣筌??怨쀯폒?醫롮뱽 ?館鍮????읈??뺣뼄.
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
		// ?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름
		// ???쟿??곷선 ??猷?
		// ?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름?癒λ름

		const float DistToTarget = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());

        // Succeed once the player is inside attack range.
		if (DistToTarget <= PlayerAttackRange)
		{
			UE_LOG(LogChaseTarget, Log,
                TEXT("[Chase] Tick - [%s] player attack range reached (Dist=%.1f). Succeeded"),
				*Pawn->GetName(), DistToTarget);
			AIC->StopMovement();
			return EStateTreeRunStatus::Succeeded;
		}

		// Stuck 揶쏅Ŋ? (?醫?: bUseStuckDetour)
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

		// ???쟿??곷선???館鍮????읈 (?類? ?怨밴묶?癒?퐣筌?????猷?餓λ쵐? CharacterMovement揶쎛 筌ｌ꼶??
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

    // Release reserved slot or sector state.
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
                        TEXT("[Chase] ExitState released slot reservation: %d"), D.AssignedSlotIndex);
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
