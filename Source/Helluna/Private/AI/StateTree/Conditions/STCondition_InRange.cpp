/**
 * STCondition_InRange.cpp
 *
 * 우주선처럼 크기가 큰 Actor는 중심점 거리가 아닌 표면까지 최단 거리로 판정한다.
 *
 * ─── 표면 거리 계산 우선순위 ─────────────────────────────────
 *  1. "ShipCombatCollision" 태그가 붙은 PrimitiveComponent
 *  2. Block 반응이 있는 PrimitiveComponent
 *  3. Actor 중심점 거리 (폴백)
 *
 * @author 김민우
 */

#include "AI/StateTree/Conditions/STCondition_InRange.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"

// ============================================================================
// 헬퍼: 전투 판정에 쓸만한 Block 콜리전이 있는지 확인
// ============================================================================
static bool IsBlockLikeCombatPrim(const UPrimitiveComponent* Prim)
{
	if (!Prim) return false;
	if (Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision) return false;

	// Overlap 전용(UI 박스 등)은 Block이 하나도 없으므로 자동 제외
	return (Prim->GetCollisionResponseToChannel(ECC_Pawn)        == ECR_Block)
		|| (Prim->GetCollisionResponseToChannel(ECC_WorldStatic)  == ECR_Block)
		|| (Prim->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
}

// ============================================================================
// 헬퍼: 후보 컴포넌트 목록 중 FromLoc 기준 최단 표면 거리 계산
// ============================================================================
static bool TryComputeMinSurfaceDistance(
	const FVector& FromLoc,
	const TArray<UPrimitiveComponent*>& Candidates,
	float& OutMinDist)
{
	float MinDist = MAX_FLT;
	bool bFound = false;

	for (UPrimitiveComponent* Prim : Candidates)
	{
		if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			continue;

		FVector Closest;
		const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);

		if (D < 0.f) continue;  // 지원 안 하는 컴포넌트
		if (D == 0.f) { OutMinDist = 0.f; return true; }  // 내부

		if (D < MinDist) { MinDist = D; bFound = true; }
	}

	if (bFound) { OutMinDist = MinDist; return true; }
	return false;
}

// ============================================================================
// 헬퍼: Actor 표면까지 최단 거리 반환
//   1순위: "ShipCombatCollision" 태그 컴포넌트
//   2순위: Block 반응 컴포넌트
//   3순위: 중심점 거리 (폴백)
// ============================================================================
static float GetSurfaceDistance(const FVector& FromLoc, const AActor* ToActor)
{
	if (!ToActor) return MAX_FLT;

	TArray<UPrimitiveComponent*> Prims;
	ToActor->GetComponents<UPrimitiveComponent>(Prims);

	// 1순위: 태그 컴포넌트
	TArray<UPrimitiveComponent*> Tagged;
	for (UPrimitiveComponent* Prim : Prims)
		if (Prim && Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
			Tagged.Add(Prim);

	float Dist = MAX_FLT;
	if (Tagged.Num() > 0 && TryComputeMinSurfaceDistance(FromLoc, Tagged, Dist))
		return Dist;

	// 2순위: Block 컴포넌트
	TArray<UPrimitiveComponent*> BlockCandidates;
	for (UPrimitiveComponent* Prim : Prims)
		if (IsBlockLikeCombatPrim(Prim))
			BlockCandidates.Add(Prim);

	if (TryComputeMinSurfaceDistance(FromLoc, BlockCandidates, Dist))
		return Dist;

	// 3순위: 중심점 거리 폴백
	return (float)FVector::Dist(FromLoc, ToActor->GetActorLocation());
}

// ============================================================================
// TestCondition
// ============================================================================
bool FSTCondition_InRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	// [DBG] 타겟 유효성 체크
	if (!TargetData.HasValidTarget())
	{
		UE_LOG(LogTemp, Warning, TEXT("[InRange] ❌ TargetData 유효하지 않음 (TargetActor=nullptr) → %s 반환"),
			!bCheckInside ? TEXT("true") : TEXT("false"));
		return !bCheckInside;
	}

	AActor* TargetActor = TargetData.TargetActor.Get();
	const bool bIsSpaceShip = (TargetData.TargetType == EHellunaTargetType::SpaceShip);
	const float EffectiveRange = bIsSpaceShip ? SpaceRange : Range;

	float EffectiveDist = TargetData.DistanceToTarget;

	// 우주선은 표면 거리로 판정
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
	const bool bResult = bCheckInside ? bIsInside : !bIsInside;

	// [DBG] 결과 로그 (매 틱 대신 결과가 false일 때만 출력해 스팸 방지)
	if (!bResult)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InRange] ⚠️ 조건 실패 | Target=%s | bIsSpaceShip=%d | Dist=%.1f | Range=%.1f | bCheckInside=%d | bIsInside=%d"),
			*GetNameSafe(TargetActor),
			(int)bIsSpaceShip,
			EffectiveDist,
			EffectiveRange,
			(int)bCheckInside,
			(int)bIsInside);
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[InRange] ✅ 조건 통과 | Target=%s | Dist=%.1f | Range=%.1f | bCheckInside=%d"),
			*GetNameSafe(TargetActor),
			EffectiveDist,
			EffectiveRange,
			(int)bCheckInside);
	}

	return bResult;
}
