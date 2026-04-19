 /**
 * SpaceShipAttackSlotManager.cpp
 */

#include "AI/SpaceShipAttackSlotManager.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "NavigationSystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Actor.h"

namespace SpaceShipSlotHelpers
{
static float NormalizeAngleDegrees(float AngleDegrees)
{
	const float Normalized = FMath::Fmod(AngleDegrees, 360.f);
	return Normalized < 0.f ? Normalized + 360.f : Normalized;
}

static float ComputeShortestAngleDeltaDegrees(float A, float B)
{
	return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B));
}

static void GatherShipCollisionPrimitives(const AActor* Owner, TArray<UPrimitiveComponent*>& OutPrimitives)
{
	OutPrimitives.Reset();

	if (!Owner)
	{
		return;
	}

	TArray<UPrimitiveComponent*> AllPrimitives;
	Owner->GetComponents<UPrimitiveComponent>(AllPrimitives);

	bool bFoundTagged = false;
	for (UPrimitiveComponent* Primitive : AllPrimitives)
	{
		if (!Primitive || Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}

		if (!Primitive->ComponentHasTag(TEXT("ShipCombatCollision")))
		{
			continue;
		}

		OutPrimitives.Add(Primitive);
		bFoundTagged = true;
	}

	if (bFoundTagged)
	{
		return;
	}

	for (UPrimitiveComponent* Primitive : AllPrimitives)
	{
		if (!Primitive || Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}

		OutPrimitives.Add(Primitive);
	}
}

static float GetShipSurfaceQueryDistance(const AActor* Owner, float ExtraPadding)
{
	if (!Owner)
	{
		return ExtraPadding;
	}

	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	Owner->GetActorBounds(true, BoundsOrigin, BoundsExtent);

	return BoundsExtent.Size2D() + ExtraPadding + 300.f;
}

static bool FindSurfacePointForDirection(const AActor* Owner, const FVector& Direction2D, float QueryDistance, FVector& OutSurfacePoint)
{
	if (!Owner || Direction2D.IsNearlyZero())
	{
		return false;
	}

	const FVector Center = Owner->GetActorLocation();
	const FVector QueryPoint = Center + Direction2D.GetSafeNormal2D() * QueryDistance;

	TArray<UPrimitiveComponent*> CollisionPrimitives;
	GatherShipCollisionPrimitives(Owner, CollisionPrimitives);

	float BestDistSq = MAX_FLT;
	bool bFound = false;

	for (const UPrimitiveComponent* Primitive : CollisionPrimitives)
	{
		if (!Primitive)
		{
			continue;
		}

		FVector ClosestPoint = FVector::ZeroVector;
		const float DistanceToCollision = Primitive->GetClosestPointOnCollision(QueryPoint, ClosestPoint);
		if (DistanceToCollision < 0.f)
		{
			continue;
		}

		const FVector FromCenter = ClosestPoint - Center;
		if (!FromCenter.IsNearlyZero() &&
			FVector::DotProduct(FromCenter.GetSafeNormal2D(), Direction2D.GetSafeNormal2D()) < 0.2f)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared2D(QueryPoint, ClosestPoint);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutSurfacePoint = ClosestPoint;
			bFound = true;
		}
	}

	if (bFound)
	{
		return true;
	}

	FVector BoundsOrigin = FVector::ZeroVector;
	FVector BoundsExtent = FVector::ZeroVector;
	Owner->GetActorBounds(true, BoundsOrigin, BoundsExtent);
	const FVector Dir = Direction2D.GetSafeNormal2D();
	const float ExtentAlongDir = FMath::Abs(Dir.X) * BoundsExtent.X + FMath::Abs(Dir.Y) * BoundsExtent.Y;
	OutSurfacePoint = FVector(Center.X + Dir.X * ExtentAlongDir, Center.Y + Dir.Y * ExtentAlongDir, Center.Z);
	return true;
}

static bool TraceGroundCandidate(UWorld* World, const AActor* Owner, const FVector& XYLocation, float TraceHalfHeight, FVector& OutGroundPoint)
{
	if (!World)
	{
		return false;
	}

	const FVector TraceStart = XYLocation + FVector(0.f, 0.f, TraceHalfHeight);
	const FVector TraceEnd = XYLocation - FVector(0.f, 0.f, TraceHalfHeight);

	FHitResult HitResult;
	FCollisionQueryParams TraceParams;
	if (Owner)
	{
		TraceParams.AddIgnoredActor(Owner);
	}

	if (!World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
	{
		return false;
	}

	OutGroundPoint = HitResult.ImpactPoint + FVector(0.f, 0.f, 10.f);
	return true;
}

static bool IsTooCloseToExistingSlots(const TArray<FAttackSlot>& Slots, const FVector& Candidate, float MinDistance2D)
{
	const float MinDistSq = FMath::Square(MinDistance2D);

	for (const FAttackSlot& Slot : Slots)
	{
		if (FVector::DistSquared2D(Slot.WorldLocation, Candidate) < MinDistSq)
		{
			return true;
		}
	}

	return false;
}

static bool ShouldReleaseReservationForActor(const AActor* OccupyingActor)
{
	if (!IsValid(OccupyingActor) || OccupyingActor->IsActorBeingDestroyed())
	{
		return true;
	}

	if (OccupyingActor->IsHidden() || !OccupyingActor->GetActorEnableCollision())
	{
		return true;
	}

	if (const AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(OccupyingActor))
	{
		if (const UHellunaHealthComponent* HealthComponent = Enemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			return HealthComponent->IsDead();
		}
	}

	return false;
}
}

float USpaceShipAttackSlotManager::ComputeSectorAngleDegrees(const FVector& WorldLocation) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 0.f;
	}

	const FVector Dir = (WorldLocation - Owner->GetActorLocation()).GetSafeNormal2D();
	return Dir.IsNearlyZero() ? 0.f : SpaceShipSlotHelpers::NormalizeAngleDegrees(Dir.Rotation().Yaw);
}

float USpaceShipAttackSlotManager::FindOrRememberPreferredSectorAngle(AActor* Monster, const FVector& ReferenceLocation)
{
	if (!Monster)
	{
		return 0.f;
	}

	const TWeakObjectPtr<AActor> MonsterKey = Monster;
	if (const float* Existing = PreferredSectorAngles.Find(MonsterKey))
	{
		return *Existing;
	}

	const float SectorAngle = ComputeSectorAngleDegrees(ReferenceLocation);
	PreferredSectorAngles.Add(MonsterKey, SectorAngle);
	return SectorAngle;
}

void USpaceShipAttackSlotManager::RememberPreferredSectorAngle(AActor* Monster, float SectorAngleDegrees)
{
	if (!Monster)
	{
		return;
	}

	const TWeakObjectPtr<AActor> MonsterKey = Monster;
	PreferredSectorAngles.FindOrAdd(MonsterKey) = SpaceShipSlotHelpers::NormalizeAngleDegrees(SectorAngleDegrees);
}

bool USpaceShipAttackSlotManager::GetPreferredSectorAngle(AActor* Monster, float& OutSectorAngleDegrees) const
{
	if (!Monster)
	{
		return false;
	}

	const TWeakObjectPtr<AActor> MonsterKey = Monster;
	if (const float* Existing = PreferredSectorAngles.Find(MonsterKey))
	{
		OutSectorAngleDegrees = *Existing;
		return true;
	}

	return false;
}

void USpaceShipAttackSlotManager::CleanupPreferredSectorAngles()
{
	for (auto It = PreferredSectorAngles.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void USpaceShipAttackSlotManager::CleanupSectorReservations()
{
	if (!bEnableSectorDistribution || SectorCount < 2)
	{
		ReservedSectorIndices.Reset();
		return;
	}

	for (auto It = ReservedSectorIndices.CreateIterator(); It; ++It)
	{
		AActor* ReservedActor = It.Key().Get();
		const int32 ReservedSector = It.Value();
		if (!It.Key().IsValid()
			|| SpaceShipSlotHelpers::ShouldReleaseReservationForActor(ReservedActor)
			|| ReservedSector < 0
			|| ReservedSector >= SectorCount)
		{
			It.RemoveCurrent();
		}
	}
}

float USpaceShipAttackSlotManager::GetSectorAngleDegrees(int32 SectorIndex) const
{
	if (SectorCount <= 0)
	{
		return 0.f;
	}

	const float SectorStep = 360.f / static_cast<float>(SectorCount);
	return SpaceShipSlotHelpers::NormalizeAngleDegrees(SectorStep * SectorIndex);
}

int32 USpaceShipAttackSlotManager::GetReservedSectorOccupancy(int32 SectorIndex, const AActor* IgnoredMonster) const
{
	int32 Occupancy = 0;
	for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ReservedSectorIndices)
	{
		if (Pair.Value != SectorIndex)
		{
			continue;
		}

		AActor* ReservedActor = Pair.Key.Get();
		if (!ReservedActor || ReservedActor == IgnoredMonster)
		{
			continue;
		}

		++Occupancy;
	}

	return Occupancy;
}

bool USpaceShipAttackSlotManager::FindApproachLocationForSector(
	int32 SectorIndex,
	const AActor* Monster,
	const FVector& MonsterLocation,
	FVector& OutApproachLocation) const
{
	if (!bEnableSectorDistribution || SectorCount < 2)
	{
		return false;
	}

	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	UNavigationSystemV1* NavSys = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (!World || !Owner || !NavSys)
	{
		return false;
	}

	const float AngleDegrees = GetSectorAngleDegrees(SectorIndex);
	uint32 MonsterHash = 0;
	if (Monster)
	{
		MonsterHash = PointerHash(Monster);
	}

	const float JitterAlpha = (MonsterHash % 1000) / 999.f;
	const float SignedJitter = (JitterAlpha * 2.f) - 1.f;
	const float JitteredAngle = SpaceShipSlotHelpers::NormalizeAngleDegrees(
		AngleDegrees + SectorAngleJitter * SignedJitter);
	const FVector Direction2D(
		FMath::Cos(FMath::DegreesToRadians(JitteredAngle)),
		FMath::Sin(FMath::DegreesToRadians(JitteredAngle)),
		0.f);
	if (Direction2D.IsNearlyZero())
	{
		return false;
	}

	const float EffectiveMinDistance = FMath::Min(SectorApproachDistanceMin, SectorApproachDistanceMax);
	const float EffectiveMaxDistance = FMath::Max(SectorApproachDistanceMin, SectorApproachDistanceMax);
	const float QueryDistance = SpaceShipSlotHelpers::GetShipSurfaceQueryDistance(Owner, EffectiveMaxDistance + 200.f);

	FVector SurfacePoint = FVector::ZeroVector;
	if (!SpaceShipSlotHelpers::FindSurfacePointForDirection(Owner, Direction2D, QueryDistance, SurfacePoint))
	{
		return false;
	}

	const FVector Outward = Direction2D.GetSafeNormal2D();
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Outward).GetSafeNormal();
	const float PreferredSide = FVector::DotProduct(Tangent, (MonsterLocation - SurfacePoint).GetSafeNormal2D()) >= 0.f ? 1.f : -1.f;
	const float DistanceStep = FMath::Max(40.f, (EffectiveMaxDistance - EffectiveMinDistance) / 3.f);
	const float LateralOffsets[] =
	{
		0.f,
		SectorLateralSpread * PreferredSide,
		-SectorLateralSpread * PreferredSide,
		SectorLateralSpread * 2.f * PreferredSide,
		-SectorLateralSpread * 2.f * PreferredSide
	};

	float BestScore = MAX_FLT;
	FVector BestLocation = FVector::ZeroVector;

	auto ConsiderLocation = [&](const FVector& Location, float CollisionDistanceScale)
	{
		if (FMath::Abs(Location.Z - MonsterLocation.Z) > MaxSlotAssignmentHeightDelta + 250.f)
		{
			return;
		}

		if (IsNearOwnerCollision(Location, MeshOverlapRadius))
		{
			return;
		}

		const float CollisionDistance = ComputeOwnerCollisionDistance(Location);
		if (CollisionDistance < 0.f || CollisionDistance > EffectiveMaxDistance + 180.f)
		{
			return;
		}

		const float Score =
			FVector::DistSquared2D(MonsterLocation, Location) +
			FMath::Square(CollisionDistance * CollisionDistanceScale);
		if (Score < BestScore)
		{
			BestScore = Score;
			BestLocation = Location;
		}
	};

	for (float Distance = EffectiveMinDistance; Distance <= EffectiveMaxDistance + KINDA_SMALL_NUMBER; Distance += DistanceStep)
	{
		for (const float LateralOffset : LateralOffsets)
		{
			const FVector CandidateXY = SurfacePoint + Outward * Distance + Tangent * LateralOffset;

			FVector Candidate = FVector::ZeroVector;
			if (!SpaceShipSlotHelpers::TraceGroundCandidate(World, Owner, CandidateXY, 3000.f, Candidate))
			{
				continue;
			}

			FVector NavLocation = FVector::ZeroVector;
			if (!ValidateSlotCandidate(Candidate, NavLocation))
			{
				continue;
			}

			ConsiderLocation(NavLocation, 3.0f);
		}
	}

	if (BestScore >= MAX_FLT)
	{
		const FVector ProjectionExtent(
			FMath::Max(NavExtent * 2.f, 240.f),
			FMath::Max(NavExtent * 2.f, 240.f),
			FMath::Max(NavExtent * 10.f, 1200.f));
		const float MidDistance = (EffectiveMinDistance + EffectiveMaxDistance) * 0.5f;
		const FVector Seeds[] =
		{
			SurfacePoint + Outward * MidDistance,
			SurfacePoint + Outward * EffectiveMaxDistance,
			SurfacePoint + Outward * EffectiveMinDistance + Tangent * SectorLateralSpread * PreferredSide,
			SurfacePoint + Outward * EffectiveMinDistance - Tangent * SectorLateralSpread * PreferredSide,
			SurfacePoint
		};

		for (const FVector& Seed : Seeds)
		{
			FNavLocation NavLoc;
			if (!NavSys->ProjectPointToNavigation(Seed, NavLoc, ProjectionExtent))
			{
				continue;
			}

			ConsiderLocation(NavLoc.Location, 2.0f);
		}
	}

	if (BestScore >= MAX_FLT)
	{
		return false;
	}

	OutApproachLocation = BestLocation;
	return true;
}

float USpaceShipAttackSlotManager::ComputeOwnerCollisionDistance(const FVector& Location) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return -1.f;
	}

	TArray<UPrimitiveComponent*> CollisionPrimitives;
	SpaceShipSlotHelpers::GatherShipCollisionPrimitives(Owner, CollisionPrimitives);

	float BestDistance = MAX_FLT;
	for (const UPrimitiveComponent* Primitive : CollisionPrimitives)
	{
		if (!Primitive)
		{
			continue;
		}

		FVector ClosestPoint = FVector::ZeroVector;
		const float DistanceToCollision = Primitive->GetClosestPointOnCollision(Location, ClosestPoint);
		if (DistanceToCollision >= 0.f && DistanceToCollision < BestDistance)
		{
			BestDistance = DistanceToCollision;
		}
	}

	return BestDistance < MAX_FLT ? BestDistance : -1.f;
}

bool USpaceShipAttackSlotManager::FindApproachLocationForSlot(
	const FAttackSlot& Slot,
	const FVector& MonsterLocation,
	FVector& OutApproachLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys)
	{
		return false;
	}

	const FVector Outward = Slot.SurfaceNormal.IsNearlyZero()
		? (Slot.SurfaceLocation - Owner->GetActorLocation()).GetSafeNormal2D()
		: Slot.SurfaceNormal.GetSafeNormal2D();
	if (Outward.IsNearlyZero())
	{
		return false;
	}

	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Outward).GetSafeNormal();
	const float PreferredSide = FVector::DotProduct(Tangent, (MonsterLocation - Slot.SurfaceLocation).GetSafeNormal2D()) >= 0.f ? 1.f : -1.f;
	const float EffectiveApproachMin = FMath::Min(ApproachDistanceMin, ApproachDistanceMax);
	const float EffectiveApproachMax = FMath::Max(ApproachDistanceMin, ApproachDistanceMax);
	const float DistanceStep = FMath::Max(20.f, (EffectiveApproachMax - EffectiveApproachMin) / 2.f);
	const float MaxAllowedCollisionDistance = EffectiveApproachMax + 40.f;
	const float MaxAllowedSurfaceHeightDelta = FMath::Max(120.f, MaxNavProjectionHeightDelta);
	const float RelaxedApproachMax = FMath::Max(EffectiveApproachMax + 120.f, EffectiveApproachMax * 1.75f);
	const float RelaxedCollisionDistance = RelaxedApproachMax + 20.f;
	const float RelaxedMonsterHeightDelta = MaxSlotAssignmentHeightDelta + 160.f;
	const float RelaxedSurfaceHeightDelta = MaxAllowedSurfaceHeightDelta + 180.f;
	const float LateralOffsets[] =
	{
		0.f,
		ApproachLateralSpread * PreferredSide,
		-ApproachLateralSpread * PreferredSide,
		ApproachLateralSpread * 2.f * PreferredSide,
		-ApproachLateralSpread * 2.f * PreferredSide
	};
	const float RelaxedLateralOffsets[] =
	{
		0.f,
		ApproachLateralSpread * PreferredSide,
		-ApproachLateralSpread * PreferredSide,
		ApproachLateralSpread * 2.f * PreferredSide,
		-ApproachLateralSpread * 2.f * PreferredSide,
		ApproachLateralSpread * 3.f * PreferredSide,
		-ApproachLateralSpread * 3.f * PreferredSide
	};
	const FVector RelaxedProjectionExtent(
		FMath::Max(NavExtent * 2.f, 220.f),
		FMath::Max(NavExtent * 2.f, 220.f),
		FMath::Max(NavExtent * 12.f, 1400.f));

	float BestDistSq = MAX_FLT;
	FVector BestLocation = FVector::ZeroVector;

	auto PassesShipVerticalChecks = [&](const FVector& Location) -> bool
	{
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(SpaceShipApproachVerticalCheck), false);

		const FVector UpStart = Location + FVector(0.f, 0.f, 10.f);
		const FVector UpEnd = Location + FVector(0.f, 0.f, 1500.f);
		FHitResult UpHit;
		if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, TraceParams)
			&& UpHit.GetActor() == Owner)
		{
			return false;
		}

		const FVector DownStart = Location + FVector(0.f, 0.f, 10.f);
		const FVector DownEnd = Location - FVector(0.f, 0.f, 500.f);
		FHitResult DownHit;
		if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_Visibility, TraceParams)
			&& DownHit.GetActor() == Owner)
		{
			return false;
		}

		return true;
	};

	auto ConsiderLocation = [&](const FVector& NavLocation, float MaxMonsterHeightDelta, float MaxSurfaceHeightDelta, float MaxCollisionDistance) -> bool
	{
		if (FMath::Abs(NavLocation.Z - MonsterLocation.Z) > MaxMonsterHeightDelta)
		{
			return false;
		}

		if (FMath::Abs(NavLocation.Z - Slot.SurfaceLocation.Z) > MaxSurfaceHeightDelta)
		{
			return false;
		}

		if (IsNearOwnerCollision(NavLocation, MeshOverlapRadius))
		{
			return false;
		}

		if (!PassesShipVerticalChecks(NavLocation))
		{
			return false;
		}

		const float CollisionDistance = ComputeOwnerCollisionDistance(NavLocation);
		if (CollisionDistance < 0.f || CollisionDistance > MaxCollisionDistance)
		{
			return false;
		}

		const float DistSq = FVector::DistSquared2D(MonsterLocation, NavLocation);
		const float Score = DistSq + FMath::Square(CollisionDistance * 4.f);
		if (Score < BestDistSq)
		{
			BestDistSq = Score;
			BestLocation = NavLocation;
			return true;
		}

		return false;
	};

	auto ConsiderLooseLocation = [&](const FVector& NavLocation, float MaxMonsterHeightDelta, float MaxSurfaceHeightDelta, float MaxCollisionDistance) -> bool
	{
		if (FMath::Abs(NavLocation.Z - MonsterLocation.Z) > MaxMonsterHeightDelta)
		{
			return false;
		}

		if (FMath::Abs(NavLocation.Z - Slot.SurfaceLocation.Z) > MaxSurfaceHeightDelta)
		{
			return false;
		}

		const float CollisionDistance = ComputeOwnerCollisionDistance(NavLocation);
		if (CollisionDistance >= 0.f && CollisionDistance > MaxCollisionDistance)
		{
			return false;
		}

		const float EffectiveCollisionDistance = CollisionDistance >= 0.f ? CollisionDistance : 0.f;
		const float DistSq = FVector::DistSquared2D(MonsterLocation, NavLocation);
		const float SurfaceDistSq = FVector::DistSquared2D(NavLocation, Slot.SurfaceLocation);
		const float Score = DistSq + SurfaceDistSq * 0.35f + FMath::Square(EffectiveCollisionDistance * 2.f);
		if (Score < BestDistSq)
		{
			BestDistSq = Score;
			BestLocation = NavLocation;
			return true;
		}

		return false;
	};

	for (float Distance = EffectiveApproachMin; Distance <= EffectiveApproachMax + KINDA_SMALL_NUMBER; Distance += DistanceStep)
	{
		for (const float LateralOffset : LateralOffsets)
		{
			const FVector CandidateXY = Slot.SurfaceLocation + Outward * Distance + Tangent * LateralOffset;

			FVector Candidate = FVector::ZeroVector;
			if (!SpaceShipSlotHelpers::TraceGroundCandidate(World, Owner, CandidateXY, 3000.f, Candidate))
			{
				continue;
			}

			FVector NavLocation = FVector::ZeroVector;
			if (!ValidateSlotCandidate(Candidate, NavLocation))
			{
				continue;
			}

			ConsiderLocation(NavLocation, MaxSlotAssignmentHeightDelta, MaxAllowedSurfaceHeightDelta, MaxAllowedCollisionDistance);
		}
	}

	if (BestDistSq >= MAX_FLT)
	{
		const float RelaxedDistanceStep = FMath::Max(30.f, (RelaxedApproachMax - EffectiveApproachMin) / 3.f);
		for (float Distance = EffectiveApproachMin; Distance <= RelaxedApproachMax + KINDA_SMALL_NUMBER; Distance += RelaxedDistanceStep)
		{
			for (const float LateralOffset : RelaxedLateralOffsets)
			{
				const FVector CandidateXY = Slot.SurfaceLocation + Outward * Distance + Tangent * LateralOffset;
				const FVector CandidateSeeds[] =
				{
					FVector(CandidateXY.X, CandidateXY.Y, MonsterLocation.Z),
					FVector(CandidateXY.X, CandidateXY.Y, Slot.SurfaceLocation.Z),
					FVector(CandidateXY.X, CandidateXY.Y, FMath::Lerp(MonsterLocation.Z, Slot.SurfaceLocation.Z, 0.5f))
				};

				for (const FVector& CandidateSeed : CandidateSeeds)
				{
					FNavLocation NavLoc;
					if (!NavSys->ProjectPointToNavigation(CandidateSeed, NavLoc, RelaxedProjectionExtent))
					{
						continue;
					}

					ConsiderLocation(NavLoc.Location, RelaxedMonsterHeightDelta, RelaxedSurfaceHeightDelta, RelaxedCollisionDistance);
				}
			}
		}
	}

	if (BestDistSq >= MAX_FLT)
	{
		const float LooseCollisionDistance = FMath::Max(RelaxedCollisionDistance + 180.f, 420.f);
		const float LooseMonsterHeightDelta = RelaxedMonsterHeightDelta + 120.f;
		const float LooseSurfaceHeightDelta = RelaxedSurfaceHeightDelta + 120.f;
		const FVector LooseProjectionExtent(
			FMath::Max(NavExtent * 3.f, 320.f),
			FMath::Max(NavExtent * 3.f, 320.f),
			FMath::Max(NavExtent * 16.f, 2000.f));
		const FVector LooseSeeds[] =
		{
			Slot.SurfaceLocation,
			Slot.WorldLocation,
			FVector(Slot.SurfaceLocation.X, Slot.SurfaceLocation.Y, MonsterLocation.Z),
			Slot.SurfaceLocation + Outward * EffectiveApproachMin,
			Slot.SurfaceLocation + Outward * EffectiveApproachMax,
			Slot.SurfaceLocation + Tangent * ApproachLateralSpread * PreferredSide,
			Slot.SurfaceLocation - Tangent * ApproachLateralSpread * PreferredSide
		};

		for (const FVector& LooseSeed : LooseSeeds)
		{
			FNavLocation NavLoc;
			if (!NavSys->ProjectPointToNavigation(LooseSeed, NavLoc, LooseProjectionExtent))
			{
				continue;
			}

			ConsiderLooseLocation(NavLoc.Location, LooseMonsterHeightDelta, LooseSurfaceHeightDelta, LooseCollisionDistance);
		}
	}

	if (BestDistSq >= MAX_FLT)
	{
		return false;
	}

	OutApproachLocation = BestLocation;
	return true;
}

USpaceShipAttackSlotManager::USpaceShipAttackSlotManager()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // 0.1초마다 디버그 갱신
}

void USpaceShipAttackSlotManager::BeginPlay()
{
	Super::BeginPlay();

	// 데디케이티드 서버: AI는 서버에서만 실행되므로 클라이언트에서는 슬롯 생성 불필요
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	// NavMesh 초기화 완료 후 슬롯 생성
	// World Partition 맵에서는 NavMesh가 스트리밍으로 로드되므로
	// 첫 시도에서 실패하면 ScheduleSlotRetry()가 자동으로 재시도한다.
	SlotRetryCount = 0;
	FTimerHandle TimerHandle;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(TimerHandle, this,
			&USpaceShipAttackSlotManager::BuildSlots, 1.0f, false);
	}
}

// ============================================================================
// BuildSlots — 우주선 주변 NavMesh 위 유효 위치를 슬롯으로 등록
// ============================================================================
void USpaceShipAttackSlotManager::BuildSlots()
{
	Slots.Empty();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	UWorld* World = GetWorld();
	if (!World) return;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][SlotBuildFail] Reason=MissingNavMesh"));
		return;
	}

	const FVector Center = Owner->GetActorLocation();
	float EffectiveMinRadius = MinRadius;
	float EffectiveMaxRadius = MaxRadius;
	GetEffectiveSlotRadii(EffectiveMinRadius, EffectiveMaxRadius);

	// 외곽 표면 방향 × 바깥 오프셋 링으로 후보 생성
	const float EffectiveAngleStep = FMath::Clamp(AngleStep, 8.f, 45.f);
	const int32 AngleCount = FMath::Max(1, FMath::RoundToInt(360.f / EffectiveAngleStep));
	const float RadiusStep = (RadiusRings > 1)
		? (EffectiveMaxRadius - EffectiveMinRadius) / (RadiusRings - 1)
		: 0.f;
	const float SurfaceQueryDistance = SpaceShipSlotHelpers::GetShipSurfaceQueryDistance(Owner, EffectiveMaxRadius);
	const float GroundTraceHalfHeight = 3000.f;
	const float MinSlotSpacing = FMath::Max(MeshOverlapRadius * 2.25f, 90.f);

	int32 ValidCount = 0;
	int32 TotalCount = 0;

	for (int32 i = 0; i < AngleCount; ++i)
	{
		const float AngleRad = FMath::DegreesToRadians(i * EffectiveAngleStep);
		const FVector Direction2D(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.f);
		FVector SurfacePoint = FVector::ZeroVector;
		if (!SpaceShipSlotHelpers::FindSurfacePointForDirection(Owner, Direction2D, SurfaceQueryDistance, SurfacePoint))
		{
			continue;
		}

		for (int32 Ring = 0; Ring < RadiusRings; ++Ring)
		{
			++TotalCount;
			const float SurfaceOffset = EffectiveMinRadius + RadiusStep * Ring;
			const FVector CandidateXY = FVector(
				SurfacePoint.X + Direction2D.X * SurfaceOffset,
				SurfacePoint.Y + Direction2D.Y * SurfaceOffset,
				Center.Z);

			FVector Candidate = FVector::ZeroVector;
			if (!SpaceShipSlotHelpers::TraceGroundCandidate(World, Owner, CandidateXY, GroundTraceHalfHeight, Candidate))
			{
				continue;
			}

			FVector NavLocation;
			if (!ValidateSlotCandidate(Candidate, NavLocation))
			{
				continue;
			}

			if (SpaceShipSlotHelpers::IsTooCloseToExistingSlots(Slots, NavLocation, MinSlotSpacing))
			{
				continue;
			}

			FAttackSlot Slot;
			Slot.WorldLocation = SurfacePoint - Direction2D * SurfaceOffset;
			Slot.SurfaceLocation = SurfacePoint;
			Slot.SurfaceNormal = Direction2D;
			Slot.State = ESlotState::Free;
			Slot.SectorAngleDegrees = SpaceShipSlotHelpers::NormalizeAngleDegrees(Direction2D.Rotation().Yaw);
			Slots.Add(Slot);
			++ValidCount;
		}
	}

	// 후보/유효 비율 로그 — 경사 지형에서 슬롯 후보 전부 탈락 시 진단용
	UE_LOG(LogTemp, Log,
		TEXT("[enemybugreport][SlotBuildRatio] Valid=%d Total=%d Ratio=%.1f NavExtent=%.0f AngleStep=%.1f MinRadius=%.1f MaxRadius=%.1f"),
		ValidCount, TotalCount,
		TotalCount > 0 ? (float)ValidCount / TotalCount * 100.f : 0.f,
		NavExtent,
		EffectiveAngleStep,
		EffectiveMinRadius,
		EffectiveMaxRadius);

	if (ValidCount == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][SlotBuildFail] Reason=NoValidSlots Try=%d MaxTry=%d MinRadius=%.0f MaxRadius=%.0f"),
			SlotRetryCount + 1, MaxSlotRetryCount, EffectiveMinRadius, EffectiveMaxRadius);

		// World Partition에서 NavMesh가 아직 스트리밍되지 않았을 수 있음 → 재시도
		ScheduleSlotRetry();
		return;
	}

	// 성공: 재시도 타이머 정리
	if (UWorld* TimerWorld = GetWorld())
	{
		TimerWorld->GetTimerManager().ClearTimer(SlotRetryTimerHandle);
	}
	SlotRetryCount = 0;

	UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotBuildSuccess] Valid=%d Total=%d Owner=%s"),
		ValidCount, TotalCount, *Center.ToString());

	// 디버그 드로잉
	if (bDebugDraw)
	{
		for (const FAttackSlot& Slot : Slots)
		{
			DrawDebugSphere(World, Slot.WorldLocation, 30.f, 8, FColor::Cyan, false, DebugDrawDuration);
		}
	}
}

void USpaceShipAttackSlotManager::GetEffectiveSlotRadii(float& OutMinRadius, float& OutMaxRadius) const
{
	OutMinRadius = FMath::Clamp(MinRadius, 40.f, 140.f);
	OutMaxRadius = FMath::Clamp(MaxRadius, OutMinRadius + 20.f, 220.f);
}

bool USpaceShipAttackSlotManager::IsNearOwnerCollision(const FVector& Location, float Clearance) const
{
	if (Clearance <= 0.f)
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	TArray<UPrimitiveComponent*> TaggedPrimitiveComponents;
	Owner->GetComponents<UPrimitiveComponent>(TaggedPrimitiveComponents);

	bool bCheckedTaggedComponents = false;
	for (const UPrimitiveComponent* Primitive : TaggedPrimitiveComponents)
	{
		if (!Primitive || Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}

		if (!Primitive->ComponentHasTag(TEXT("ShipCombatCollision")))
		{
			continue;
		}

		bCheckedTaggedComponents = true;

		FVector ClosestPoint = FVector::ZeroVector;
		const float DistanceToCollision = Primitive->GetClosestPointOnCollision(Location, ClosestPoint);
		if (DistanceToCollision >= 0.f && DistanceToCollision < Clearance)
		{
			return true;
		}
	}

	if (bCheckedTaggedComponents)
	{
		return false;
	}

	if (const UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
	{
		if (RootPrimitive->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			FVector ClosestPoint = FVector::ZeroVector;
			const float DistanceToCollision = RootPrimitive->GetClosestPointOnCollision(Location, ClosestPoint);
			if (DistanceToCollision >= 0.f && DistanceToCollision < Clearance)
			{
				return true;
			}
		}
	}

	return false;
}

// ============================================================================
// ValidateSlotCandidate — NavMesh 투영 + 메시 겹침 체크
// ============================================================================
bool USpaceShipAttackSlotManager::ValidateSlotCandidate(const FVector& Candidate, FVector& OutNavLocation) const
{
	UWorld* World = GetWorld();
	if (!World) return false;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	// 1. NavMesh 투영 — Z 허용 범위를 크게 잡아 경사 지형 대응
	FNavLocation NavLoc;
	const FVector Extent(NavExtent, NavExtent, NavExtent * 8.f);
	if (!NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent))
		return false;

	// Ground trace 위치와 NavMesh 투영 결과의 높이 차가 너무 크면 공중 슬롯로 판단한다.
	if (FMath::Abs(NavLoc.Location.Z - Candidate.Z) > MaxNavProjectionHeightDelta)
		return false;

	if (IsNearOwnerCollision(NavLoc.Location, MeshOverlapRadius))
		return false;

	AActor* Owner = GetOwner();
	if (Owner)
	{
		FCollisionQueryParams TraceParams;
		TraceParams.AddIgnoredActor(nullptr);

		// 2. 우주선 밑인지 체크 — 위로 Trace 쏴서 우주선에 막히면 거부
		{
			const FVector UpStart = NavLoc.Location + FVector(0, 0, 10.f);
			const FVector UpEnd   = NavLoc.Location + FVector(0, 0, 1500.f);
			FHitResult UpHit;
			if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, TraceParams))
			{
				if (UpHit.GetActor() == Owner)
					return false;
			}
		}

		// 3. 우주선 위인지 체크 — 아래로 Trace 쏴서 우주선에 맞으면 거부
		//    (슬롯이 우주선 상부 표면 위에 생성되는 것 방지)
		{
			const FVector DownStart = NavLoc.Location + FVector(0, 0, 10.f);
			const FVector DownEnd   = NavLoc.Location - FVector(0, 0, 500.f);
			FHitResult DownHit;
			if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_Visibility, TraceParams))
			{
				if (DownHit.GetActor() == Owner)
					return false;
			}
		}
	}

	OutNavLocation = NavLoc.Location;
	return true;
}

// ============================================================================
// RequestSlot — 가장 가까운 Free 슬롯 배정
// ============================================================================
bool USpaceShipAttackSlotManager::RequestSlot(AActor* Monster, int32& OutSlotIndex, FVector& OutLocation)
{
	if (!Monster) return false;

	CleanupPreferredSectorAngles();

	// 이미 같은 몬스터가 슬롯을 갖고 있으면 그대로 재사용한다.
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].OccupyingMonster.Get() != Monster)
		{
			continue;
		}

		OutSlotIndex = i;
		if (!FindApproachLocationForSlot(Slots[i], Monster->GetActorLocation(), OutLocation))
		{
			OutLocation = Slots[i].SurfaceLocation.IsZero() ? Slots[i].WorldLocation : Slots[i].SurfaceLocation;
			// 폴백 위치의 Z가 지하(우주선 높이)일 수 있으므로 몬스터 Z로 보정
			OutLocation.Z = Monster->GetActorLocation().Z;
		}
		UE_LOG(LogTemp, Log,
			TEXT("[enemybugreport][SlotReuse] Monster=%s Slot=%d State=%s SlotLoc=%s"),
			*GetNameSafe(Monster),
			i,
			Slots[i].State == ESlotState::Reserved ? TEXT("Reserved") :
			(Slots[i].State == ESlotState::Occupied ? TEXT("Occupied") : TEXT("Free")),
			*OutLocation.ToCompactString());
		return true;
	}

	const FVector MonsterLoc = Monster->GetActorLocation();
	const float PreferredSectorAngle = FindOrRememberPreferredSectorAngle(Monster, MonsterLoc);

	int32 BestIdx = -1;
	float BestScore = MAX_FLT;
	FVector BestApproachLocation = FVector::ZeroVector;
	int32 FreeCount = 0;
	int32 ReservedCount = 0;
	int32 OccupiedCount = 0;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].State == ESlotState::Free) ++FreeCount;
		else if (Slots[i].State == ESlotState::Reserved) ++ReservedCount;
		else if (Slots[i].State == ESlotState::Occupied) ++OccupiedCount;

		if (Slots[i].State != ESlotState::Free) continue;

		FVector ApproachLocation = FVector::ZeroVector;
		if (!FindApproachLocationForSlot(Slots[i], MonsterLoc, ApproachLocation))
		{
			ApproachLocation = Slots[i].SurfaceLocation.IsZero() ? Slots[i].WorldLocation : Slots[i].SurfaceLocation;
			// 폴백 위치의 Z가 지하(우주선 높이)일 수 있으므로 몬스터 Z로 보정
			ApproachLocation.Z = MonsterLoc.Z;
		}

		const float HeightDelta = FMath::Abs(MonsterLoc.Z - ApproachLocation.Z);
		if (HeightDelta > MaxSlotAssignmentHeightDelta + 1500.f)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared2D(MonsterLoc, ApproachLocation);
		const float SectorDelta = SpaceShipSlotHelpers::ComputeShortestAngleDeltaDegrees(
			PreferredSectorAngle,
			Slots[i].SectorAngleDegrees);
		const float SectorPenalty = FMath::Square(SectorDelta * 18.f);
		const float Score = DistSq + SectorPenalty;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestIdx = i;
			BestApproachLocation = ApproachLocation;
		}
	}

	if (BestIdx < 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[enemybugreport][SlotRequestFail] Monster=%s Total=%d Free=%d Reserved=%d Occupied=%d"),
			*GetNameSafe(Monster),
			Slots.Num(),
			FreeCount,
			ReservedCount,
			OccupiedCount);
		return false;
	}

	Slots[BestIdx].State = ESlotState::Reserved;
	Slots[BestIdx].OccupyingMonster = Monster;
	RememberPreferredSectorAngle(Monster, Slots[BestIdx].SectorAngleDegrees);

	OutSlotIndex = BestIdx;
	OutLocation  = BestApproachLocation;

	UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotReserveVerbose] Slot=%d Anchor=%s MoveLoc=%s Monster=%s PreferredSector=%.1f SlotSector=%.1f"),
		BestIdx, *Slots[BestIdx].WorldLocation.ToString(), *OutLocation.ToString(), *Monster->GetName(), PreferredSectorAngle, Slots[BestIdx].SectorAngleDegrees);
	UE_LOG(LogTemp, Warning,
		TEXT("[enemybugreport][SlotRequestSuccess] Monster=%s Slot=%d SlotLoc=%s Dist=%.1f Free=%d Reserved=%d Occupied=%d"),
		*GetNameSafe(Monster),
		BestIdx,
		*OutLocation.ToCompactString(),
		FMath::Sqrt(FVector::DistSquared2D(MonsterLoc, OutLocation)),
		FreeCount,
		ReservedCount,
		OccupiedCount);

	return true;
}

// ============================================================================
// OccupySlot — Reserved → Occupied
// ============================================================================
void USpaceShipAttackSlotManager::OccupySlot(int32 SlotIndex, AActor* Monster)
{
	if (!Slots.IsValidIndex(SlotIndex)) return;
	if (Slots[SlotIndex].OccupyingMonster.Get() != Monster) return;

	// 이미 Occupied 상태면 중복 호출 무시 (매 틱 호출 시 로그 폭발 방지)
	if (Slots[SlotIndex].State == ESlotState::Occupied) return;

	Slots[SlotIndex].State = ESlotState::Occupied;

	UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotOccupy] Slot=%d Monster=%s"),
		SlotIndex, Monster ? *Monster->GetName() : TEXT("nullptr"));
}

// ============================================================================
// ReleaseSlot — 몬스터 기준으로 슬롯 반납
// ============================================================================
void USpaceShipAttackSlotManager::ReleaseSlot(AActor* Monster)
{
	if (!Monster) return;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].OccupyingMonster.Get() == Monster)
		{
			const ESlotState OldState = Slots[i].State;
			Slots[i].State = ESlotState::Free;
			Slots[i].OccupyingMonster = nullptr;

			UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotRelease] Slot=%d PrevState=%s Monster=%s"),
				i,
				OldState == ESlotState::Reserved ? TEXT("Reserved") : TEXT("Occupied"),
				*Monster->GetName());
			return;
		}
	}
}

// ============================================================================
// ReleaseSlotByIndex
// ============================================================================
void USpaceShipAttackSlotManager::ReleaseSlotByIndex(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex)) return;

	Slots[SlotIndex].State = ESlotState::Free;
	Slots[SlotIndex].OccupyingMonster = nullptr;
}

bool USpaceShipAttackSlotManager::RequestSectorPosition(AActor* Monster, int32& OutSectorIndex, FVector& OutLocation)
{
	OutSectorIndex = INDEX_NONE;
	OutLocation = FVector::ZeroVector;

	if (!Monster || !bEnableSectorDistribution || SectorCount < 2)
	{
		return false;
	}

	CleanupPreferredSectorAngles();
	CleanupSectorReservations();

	const TWeakObjectPtr<AActor> MonsterKey = Monster;

	if (const int32* ExistingSector = ReservedSectorIndices.Find(MonsterKey))
	{
		if (FindApproachLocationForSector(*ExistingSector, Monster, Monster->GetActorLocation(), OutLocation))
		{
			OutSectorIndex = *ExistingSector;
			return true;
		}

		ReservedSectorIndices.Remove(MonsterKey);
	}

	const FVector MonsterLocation = Monster->GetActorLocation();
	const float PreferredSectorAngle = FindOrRememberPreferredSectorAngle(Monster, MonsterLocation);
	const float OccupancyPenaltyBase = FMath::Square(FMath::Max(260.f, SectorApproachDistanceMax));

	float BestScore = MAX_FLT;
	int32 BestSectorIndex = INDEX_NONE;
	FVector BestLocation = FVector::ZeroVector;

	for (int32 SectorIndex = 0; SectorIndex < SectorCount; ++SectorIndex)
	{
		FVector CandidateLocation = FVector::ZeroVector;
		if (!FindApproachLocationForSector(SectorIndex, Monster, MonsterLocation, CandidateLocation))
		{
			continue;
		}

		const int32 Occupancy = GetReservedSectorOccupancy(SectorIndex, Monster);
		const float SectorDelta = SpaceShipSlotHelpers::ComputeShortestAngleDeltaDegrees(
			PreferredSectorAngle,
			GetSectorAngleDegrees(SectorIndex));
		const float Score =
			FVector::DistSquared2D(MonsterLocation, CandidateLocation) +
			FMath::Square(SectorDelta * 10.f) +
			Occupancy * OccupancyPenaltyBase * 4.f;

		if (Score < BestScore)
		{
			BestScore = Score;
			BestSectorIndex = SectorIndex;
			BestLocation = CandidateLocation;
		}
	}

	if (BestSectorIndex == INDEX_NONE)
	{
		return false;
	}

	ReservedSectorIndices.FindOrAdd(MonsterKey) = BestSectorIndex;
	RememberPreferredSectorAngle(Monster, GetSectorAngleDegrees(BestSectorIndex));

	OutSectorIndex = BestSectorIndex;
	OutLocation = BestLocation;
	return true;
}

void USpaceShipAttackSlotManager::ReleaseSectorReservation(AActor* Monster)
{
	if (!Monster)
	{
		return;
	}

	const TWeakObjectPtr<AActor> MonsterKey = Monster;
	ReservedSectorIndices.Remove(MonsterKey);
}

void USpaceShipAttackSlotManager::ReleaseEngagementReservation(AActor* Monster)
{
	if (!Monster)
	{
		return;
	}

	ReleaseSlot(Monster);
	ReleaseSectorReservation(Monster);
	ReleaseTopSlot(Monster);
}

// ─── [ShipTopV1 / ShipJumpSpreadV1] 상단 점프 슬롯 ──────────────────────────────

void USpaceShipAttackSlotManager::CleanupTopSlotReservations()
{
	for (auto It = TopSlotAssignments.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool USpaceShipAttackSlotManager::TryReserveTopSlot(AActor* Monster)
{
	int32 UnusedIndex = INDEX_NONE;
	return TryReserveTopSlotIndexed(Monster, UnusedIndex);
}

bool USpaceShipAttackSlotManager::TryReserveTopSlotIndexed(AActor* Monster, int32& OutSlotIndex)
{
	OutSlotIndex = INDEX_NONE;

	if (!Monster)
	{
		return false;
	}

	CleanupTopSlotReservations();

	const TWeakObjectPtr<AActor> Key = Monster;

	// 이미 예약된 몬스터면 기존 인덱스 반환 (멱등).
	for (const TPair<int32, TWeakObjectPtr<AActor>>& Pair : TopSlotAssignments)
	{
		if (Pair.Value == Key)
		{
			OutSlotIndex = Pair.Key;
			return true;
		}
	}

	if (TopSlotAssignments.Num() >= MaxTopSlots)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipJumpSpreadV1] TryReserveTopSlot DENIED (full) Monster=%s Usage=%d/%d"),
			*Monster->GetName(), TopSlotAssignments.Num(), MaxTopSlots);
		return false;
	}

	// [ShipJumpSpreadV1] 0..MaxTopSlots-1 중 가장 작은 빈 인덱스 부여.
	for (int32 Candidate = 0; Candidate < MaxTopSlots; ++Candidate)
	{
		if (!TopSlotAssignments.Contains(Candidate))
		{
			TopSlotAssignments.Add(Candidate, Key);
			OutSlotIndex = Candidate;
			UE_LOG(LogTemp, Warning,
				TEXT("[ShipJumpSpreadV1] TryReserveTopSlot GRANTED Monster=%s SlotIndex=%d Usage=%d/%d"),
				*Monster->GetName(), Candidate, TopSlotAssignments.Num(), MaxTopSlots);
			return true;
		}
	}

	// 도달 불가 (위에서 풀 검사 완료) — 안전 폴백.
	return false;
}

void USpaceShipAttackSlotManager::ReleaseTopSlot(AActor* Monster)
{
	if (!Monster)
	{
		return;
	}

	const TWeakObjectPtr<AActor> Key = Monster;
	int32 RemovedIndex = INDEX_NONE;
	for (auto It = TopSlotAssignments.CreateIterator(); It; ++It)
	{
		if (It.Value() == Key)
		{
			RemovedIndex = It.Key();
			It.RemoveCurrent();
			break;
		}
	}

	if (RemovedIndex != INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShipJumpSpreadV1] ReleaseTopSlot Monster=%s SlotIndex=%d Usage=%d/%d"),
			*Monster->GetName(), RemovedIndex, TopSlotAssignments.Num(), MaxTopSlots);
	}
}

bool USpaceShipAttackSlotManager::HasTopSlot(const AActor* Monster) const
{
	if (!Monster)
	{
		return false;
	}
	const TWeakObjectPtr<AActor> Key = const_cast<AActor*>(Monster);
	for (const TPair<int32, TWeakObjectPtr<AActor>>& Pair : TopSlotAssignments)
	{
		if (Pair.Value == Key)
		{
			return true;
		}
	}
	return false;
}

bool USpaceShipAttackSlotManager::GetMonsterSlotInfo(const AActor* Monster, int32& OutSlotIndex, ESlotState& OutState) const
{
	OutSlotIndex = INDEX_NONE;
	OutState = ESlotState::Free;

	if (!Monster)
	{
		return false;
	}

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].OccupyingMonster.Get() != Monster)
		{
			continue;
		}

		OutSlotIndex = i;
		OutState = Slots[i].State;
		return true;
	}

	return false;
}

// ============================================================================
// GetWaitingPosition — 슬롯 없을 때 우주선 근처 대기 위치 반환
// ============================================================================
bool USpaceShipAttackSlotManager::GetWaitingPosition(AActor* Monster, FVector& OutLocation) const
{
	if (!Monster) return false;

	UWorld* World = GetWorld();
	if (!World) return false;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys) return false;

	AActor* Owner = GetOwner();
	if (!Owner) return false;

	const FVector Center = Owner->GetActorLocation();
	float EffectiveMinRadius = MinRadius;
	float EffectiveMaxRadius = MaxRadius;
	GetEffectiveSlotRadii(EffectiveMinRadius, EffectiveMaxRadius);
	const float SurfaceQueryDistance = SpaceShipSlotHelpers::GetShipSurfaceQueryDistance(Owner, EffectiveMaxRadius + 200.f);
	const float WaitMinOffset = EffectiveMaxRadius + 100.f;
	const float WaitMaxOffset = WaitMinOffset + 160.f;
	const float GroundTraceHalfHeight = 3000.f;
	float PreferredSectorAngle = 0.f;
	const bool bHasPreferredSector = GetPreferredSectorAngle(Monster, PreferredSectorAngle);

	for (int32 Try = 0; Try < 8; ++Try)
	{
		const float Angle = bHasPreferredSector
			? SpaceShipSlotHelpers::NormalizeAngleDegrees(PreferredSectorAngle + FMath::RandRange(-40.f, 40.f))
			: FMath::RandRange(0.f, 360.f);
		const FVector Direction2D(FMath::Cos(FMath::DegreesToRadians(Angle)), FMath::Sin(FMath::DegreesToRadians(Angle)), 0.f);
		FVector SurfacePoint = FVector::ZeroVector;
		if (!SpaceShipSlotHelpers::FindSurfacePointForDirection(Owner, Direction2D, SurfaceQueryDistance, SurfacePoint))
		{
			continue;
		}

		const float SurfaceOffset = FMath::RandRange(WaitMinOffset, WaitMaxOffset);
		const FVector CandidateXY = FVector(
			SurfacePoint.X + Direction2D.X * SurfaceOffset,
			SurfacePoint.Y + Direction2D.Y * SurfaceOffset,
			Center.Z);

		FVector Candidate = FVector::ZeroVector;
		if (!SpaceShipSlotHelpers::TraceGroundCandidate(World, Owner, CandidateXY, GroundTraceHalfHeight, Candidate))
		{
			continue;
		}

		FNavLocation NavLoc;
		const FVector Extent(NavExtent * 2.f, NavExtent * 2.f, NavExtent * 8.f);
		if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent))
		{
			if (FMath::Abs(NavLoc.Location.Z - Candidate.Z) > MaxNavProjectionHeightDelta)
			{
				continue;
			}

			if (FMath::Abs(NavLoc.Location.Z - Monster->GetActorLocation().Z) > MaxWaitingPositionHeightDelta)
			{
				continue;
			}

			if (IsNearOwnerCollision(NavLoc.Location, MeshOverlapRadius))
			{
				continue;
			}

			FCollisionQueryParams WaitTraceParams;

			// 우주선 밑인지 체크
			{
				const FVector UpStart = NavLoc.Location + FVector(0, 0, 10.f);
				const FVector UpEnd   = NavLoc.Location + FVector(0, 0, 1500.f);
				FHitResult UpHit;
				if (World->LineTraceSingleByChannel(UpHit, UpStart, UpEnd, ECC_Visibility, WaitTraceParams))
				{
					if (UpHit.GetActor() == Owner)
						continue;
				}
			}

			// 우주선 위인지 체크
			{
				const FVector DownStart = NavLoc.Location + FVector(0, 0, 10.f);
				const FVector DownEnd   = NavLoc.Location - FVector(0, 0, 500.f);
				FHitResult DownHit;
				if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_Visibility, WaitTraceParams))
				{
					if (DownHit.GetActor() == Owner)
						continue;
				}
			}

			OutLocation = NavLoc.Location;
			UE_LOG(LogTemp, Warning,
				TEXT("[enemybugreport][WaitingPosition] Monster=%s WaitLoc=%s Try=%d"),
				*GetNameSafe(Monster),
				*OutLocation.ToCompactString(),
				Try + 1);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[enemybugreport][WaitingPositionFail] Monster=%s Owner=%s MaxRadius=%.1f WaitMaxOffset=%.1f"),
		*GetNameSafe(Monster),
		*GetNameSafe(Owner),
		EffectiveMaxRadius,
		WaitMaxOffset);

	return false;
}

// ============================================================================
// TriggerBuildSlotsIfEmpty — 플레이어 접속 시 슬롯 재시도
// ============================================================================
void USpaceShipAttackSlotManager::TriggerBuildSlotsIfEmpty()
{
	if (Slots.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[enemybugreport][SlotBuildRetry] Reason=PlayerJoinedSlotsEmpty"));
		SlotRetryCount = 0; // 카운터 리셋하여 재시도 가능하게
		BuildSlots();
	}
}

// ============================================================================
// RebuildSlots — 런타임 재생성
// ============================================================================
void USpaceShipAttackSlotManager::RebuildSlots()
{
	// 현재 예약/점유 중인 슬롯의 몬스터들에게 슬롯 반납 처리
	for (FAttackSlot& Slot : Slots)
	{
		Slot.State = ESlotState::Free;
		Slot.OccupyingMonster = nullptr;
	}
	BuildSlots();
}

// ============================================================================
// TickComponent — 디버그 드로잉
// ============================================================================
void USpaceShipAttackSlotManager::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	CleanupPreferredSectorAngles();
	CleanupSectorReservations();

	// 죽은 몬스터가 점유한 슬롯 자동 반납
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FAttackSlot& Slot = Slots[i];
		if (Slot.State == ESlotState::Free)
		{
			continue;
		}

		AActor* OccupyingActor = Slot.OccupyingMonster.Get();
		const bool bShouldAutoRelease = SpaceShipSlotHelpers::ShouldReleaseReservationForActor(OccupyingActor);

		if (bShouldAutoRelease)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[enemybugreport][SlotAutoRelease] Slot=%d State=%s"),
				i, Slot.State == ESlotState::Reserved ? TEXT("Reserved") : TEXT("Occupied"));
			Slot.State = ESlotState::Free;
			Slot.OccupyingMonster = nullptr;
		}
	}

	if (!bDebugDraw) return;

	UWorld* World = GetWorld();
	if (!World) return;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FAttackSlot& Slot = Slots[i];

		FColor Color;
		switch (Slot.State)
		{
		case ESlotState::Free:     Color = FColor::Green;  break;
		case ESlotState::Reserved: Color = FColor::Yellow; break;
		case ESlotState::Occupied: Color = FColor::Red;    break;
		default:                   Color = FColor::White;  break;
		}

		DrawDebugSphere(World, Slot.WorldLocation, 35.f, 8, Color, false, -1.f, 0, 2.f);

		// 인덱스 + 상태 텍스트
		const FString StateStr = (Slot.State == ESlotState::Free)
			? TEXT("Free")
			: (Slot.State == ESlotState::Reserved ? TEXT("Rsv") : TEXT("Occ"));
		DrawDebugString(World, Slot.WorldLocation + FVector(0, 0, 50.f),
			FString::Printf(TEXT("[%d] %s"), i, *StateStr),
			nullptr, Color, -1.f);
	}

	if (bEnableSectorDistribution && SectorCount >= 2)
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			const FVector Center = Owner->GetActorLocation();
			const float DebugDistance = FMath::Max(SectorApproachDistanceMax, SectorApproachDistanceMin);
			const float SectorStep = 360.f / static_cast<float>(SectorCount);

			for (int32 SectorIndex = 0; SectorIndex < SectorCount; ++SectorIndex)
			{
				const float AngleDegrees = SectorStep * SectorIndex;
				const FVector Direction2D(
					FMath::Cos(FMath::DegreesToRadians(AngleDegrees)),
					FMath::Sin(FMath::DegreesToRadians(AngleDegrees)),
					0.f);
				const FVector OuterPoint = Center + Direction2D * DebugDistance;
				const int32 Occupancy = GetReservedSectorOccupancy(SectorIndex, nullptr);

				DrawDebugLine(World, Center, OuterPoint, FColor::Silver, false, -1.f, 0, 1.5f);
				DrawDebugString(
					World,
					OuterPoint + FVector(0.f, 0.f, 40.f),
					FString::Printf(TEXT("S%d (%d)"), SectorIndex, Occupancy),
					nullptr,
					FColor::Silver,
					-1.f);
			}
		}
	}
}

// ============================================================================
// ScheduleSlotRetry — BuildSlots 실패 시 재시도 스케줄링
//
// [World Partition NavMesh 스트리밍 대응]
// 패키징 빌드에서 서버 시작 직후에는 NavMesh 데이터가 아직 스트리밍되지 않아
// BuildSlots가 유효 슬롯 0개를 생성할 수 있다.
// 플레이어가 접속하고 World Partition 셀이 로드되면 NavMesh도 함께 로드되므로
// 일정 간격으로 재시도하면 성공한다.
// ============================================================================
void USpaceShipAttackSlotManager::ScheduleSlotRetry()
{
	++SlotRetryCount;

	if (SlotRetryCount >= MaxSlotRetryCount)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[enemybugreport][SlotBuildFail] Reason=RetryExhausted Try=%d"),
			MaxSlotRetryCount);
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	World->GetTimerManager().SetTimer(
		SlotRetryTimerHandle, this,
		&USpaceShipAttackSlotManager::BuildSlots,
		SlotRetryInterval, false);

	UE_LOG(LogTemp, Log,
		TEXT("[enemybugreport][SlotBuildRetryScheduled] Delay=%.1f Try=%d MaxTry=%d"),
		SlotRetryInterval, SlotRetryCount, MaxSlotRetryCount);
}
