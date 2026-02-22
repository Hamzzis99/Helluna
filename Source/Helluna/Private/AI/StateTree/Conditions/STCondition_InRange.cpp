/**
 * STCondition_InRange.cpp
 *
 * @author 김민우
 */

// STCondition_InRange.cpp

// STCondition_InRange.cpp

#// STCondition_InRange.cpp

#include "AI/StateTree/Conditions/STCondition_InRange.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"

static bool IsBlockLikeCombatPrim(const UPrimitiveComponent* Prim)
{
	if (!Prim)
		return false;

	if (Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		return false;

	// Overlap 전용 UI 박스는 보통 Block이 하나도 없음 → 자동 제외
	const bool bBlocksSomething =
		(Prim->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) ||
		(Prim->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block) ||
		(Prim->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);

	return bBlocksSomething;
}

static bool TryComputeMinSurfaceDistance(
	const FVector& FromLoc,
	const TArray<UPrimitiveComponent*>& Candidates,
	float& OutMinDist
)
{
	float MinDist = MAX_FLT;
	bool bFound = false;

	for (UPrimitiveComponent* Prim : Candidates)
	{
		if (!Prim)
			continue;

		if (Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			continue;

		FVector Closest = FVector::ZeroVector;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);

		// 실패/지원 안 함
		if (D < 0.f)
			continue;

		// 내부
		if (D == 0.f)
		{
			OutMinDist = 0.f;
			return true;
		}

		if (D < MinDist)
		{
			MinDist = D;
			bFound = true;
		}
	}

	if (bFound)
	{
		OutMinDist = MinDist;
		return true;
	}

	return false;
}

static float GetSurfaceDistance(const FVector& FromLoc, const AActor* ToActor)
{
	if (!ToActor)
		return MAX_FLT;

	TArray<UPrimitiveComponent*> Prims;
	ToActor->GetComponents<UPrimitiveComponent>(Prims);

	// 1) 태그 후보 먼저
	TArray<UPrimitiveComponent*> Tagged;
	Tagged.Reserve(Prims.Num());

	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim && Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
		{
			Tagged.Add(Prim);
		}
	}

	float Dist = MAX_FLT;

	// ✅ 태그가 달려있으면 우선 태그 후보로 시도
	if (Tagged.Num() > 0)
	{
		if (TryComputeMinSurfaceDistance(FromLoc, Tagged, Dist))
			return Dist;

		// 태그 후보가 전부 실패(-1)면 아래 블록 후보로 자동 폴백
	}

	// 2) 태그가 없거나, 태그 후보가 실패하면: Block 반응 있는 후보들로 재시도
	TArray<UPrimitiveComponent*> BlockCandidates;
	BlockCandidates.Reserve(Prims.Num());

	for (UPrimitiveComponent* Prim : Prims)
	{
		if (Prim && IsBlockLikeCombatPrim(Prim))
		{
			BlockCandidates.Add(Prim);
		}
	}

	if (TryComputeMinSurfaceDistance(FromLoc, BlockCandidates, Dist))
		return Dist;

	// 3) 아무것도 못 찾으면 중심거리 폴백
	return (float)FVector::Dist(FromLoc, ToActor->GetActorLocation());
}

bool FSTCondition_InRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	if (!TargetData.IsValid())
		return !bCheckInside;

	AActor* TargetActor = TargetData.TargetActor.Get();
	const bool bIsSpaceShip = (TargetData.TargetType == EHellunaTargetType::SpaceShip);

	float EffectiveDist = TargetData.DistanceToTarget;
	const float EffectiveRange = bIsSpaceShip ? SpaceRange : Range;

	if (bIsSpaceShip && TargetActor)
	{
		if (const AAIController* AIC = Cast<AAIController>(Context.GetOwner()))
		{
			if (const APawn* Pawn = AIC->GetPawn())
			{
				EffectiveDist = GetSurfaceDistance(Pawn->GetActorLocation(), TargetActor);
			}
		}
	}

	const bool bIsInside = (EffectiveDist <= EffectiveRange);
	return bCheckInside ? bIsInside : !bIsInside;
}