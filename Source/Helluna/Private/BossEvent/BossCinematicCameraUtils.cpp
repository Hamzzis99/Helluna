// Capstone Project Helluna

#include "BossEvent/BossCinematicCameraUtils.h"

#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"

namespace BossCinematicCameraUtils
{
	FVector PushCameraOutOfActorOccluders(
		const UObject* WorldContext,
		const FVector& DesiredCamLoc,
		const FVector& TargetLoc,
		const TArray<AActor*>& IgnoreActors,
		float Margin)
	{
		if (!WorldContext)
		{
			return DesiredCamLoc;
		}
		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			return DesiredCamLoc;
		}

		const FVector Delta = DesiredCamLoc - TargetLoc;
		const float Dist = Delta.Size();
		if (Dist < KINDA_SMALL_NUMBER)
		{
			return DesiredCamLoc;
		}
		const FVector Dir = Delta / Dist;

		FCollisionQueryParams Params(SCENE_QUERY_STAT(BossCinematicOccluderTrace), false);
		Params.bTraceComplex = false;
		for (AActor* Ignore : IgnoreActors)
		{
			if (Ignore)
			{
				Params.AddIgnoredActor(Ignore);
			}
		}

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit, TargetLoc, DesiredCamLoc, ECC_Camera, Params);
		if (!bHit || !Hit.bBlockingHit)
		{
			return DesiredCamLoc;
		}

		// hit point 보다 보스 쪽(반대 방향) 으로 Margin 만큼 당김.
		FVector Pushed = Hit.ImpactPoint - Dir * Margin;

		// 보스 자체에 너무 붙지 않도록 최소 거리 보장 (Margin 자체).
		const float DistToTarget = (Pushed - TargetLoc).Size();
		if (DistToTarget < Margin)
		{
			Pushed = TargetLoc + Dir * Margin;
		}
		return Pushed;
	}

	FVector ClampCameraAboveGround(
		const UObject* WorldContext,
		const FVector& DesiredCamLoc,
		const FVector& BossHeadLoc,
		const TArray<AActor*>& IgnoreActors,
		float MarginZ,
		float MaxDownTrace)
	{
		if (!WorldContext)
		{
			return DesiredCamLoc;
		}
		UWorld* World = WorldContext->GetWorld();
		if (!World)
		{
			return DesiredCamLoc;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(BossCinematicGroundTrace), false);
		Params.bTraceComplex = false;
		for (AActor* Ignore : IgnoreActors)
		{
			if (Ignore)
			{
				Params.AddIgnoredActor(Ignore);
			}
		}

		const FVector TraceStart = BossHeadLoc;
		const FVector TraceEnd = BossHeadLoc - FVector(0.f, 0.f, MaxDownTrace);

		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(
			Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
		if (!bHit || !Hit.bBlockingHit)
		{
			return DesiredCamLoc; // 지형 못 찾음 → 보정 안 함.
		}

		const float MinZ = Hit.ImpactPoint.Z + MarginZ;
		if (DesiredCamLoc.Z >= MinZ)
		{
			return DesiredCamLoc; // 이미 충분히 위.
		}
		return FVector(DesiredCamLoc.X, DesiredCamLoc.Y, MinZ);
	}
}
