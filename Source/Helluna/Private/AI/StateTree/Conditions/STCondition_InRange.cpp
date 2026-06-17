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
#include "AI/HellunaAIAttackZone.h" // [SurfaceDistanceV1] 공용 표면거리 헬퍼

// ============================================================================
// 헬퍼: Actor 표면까지 최단 거리
//   [SurfaceDistanceV1] 공용 HellunaAI::GetTargetSurfaceDistance 로 위임.
//   기존 로컬 구현은 우주선 메시(복합 콜리전)에서 GetClosestPointOnCollision 이 -1 을
//   반환해 전부 원점 거리로 폴백되는 버그가 있었다 → 공용 헬퍼가 OBB 폴백으로 해결.
// ============================================================================
static float GetSurfaceDistance(const FVector& FromLoc, const AActor* ToActor)
{
	return HellunaAI::GetTargetSurfaceDistance(FromLoc, ToActor);
}

// ============================================================================
// TestCondition
// ============================================================================
bool FSTCondition_InRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	if (!TargetData.HasValidTarget())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[InRange] TargetData 유효하지 않음 → %s 반환"),
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

	// [DBG] 결과 로그 — Verbose로 전환 (Warning 스팸이 FPS를 크게 저하시킴)
	UE_LOG(LogTemp, Verbose,
		TEXT("[InRange] %s | Target=%s | bIsSpaceShip=%d | Dist=%.1f | Range=%.1f | bCheckInside=%d | bIsInside=%d"),
		bResult ? TEXT("PASS") : TEXT("FAIL"),
		*GetNameSafe(TargetActor),
		(int)bIsSpaceShip,
		EffectiveDist,
		EffectiveRange,
		(int)bCheckInside,
		(int)bIsInside);

	return bResult;
}
