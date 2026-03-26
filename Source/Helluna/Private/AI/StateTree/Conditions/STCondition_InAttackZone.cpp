/**
 * STCondition_InAttackZone.cpp
 *
 * 몬스터 전방 Box 공격존 vs 타겟 콜리전 오버랩 판정.
 *
 * ─── 동작 흐름 ─────────────────────────────────────────────────
 *  1. 폰 위치 + 전방 오프셋 → 박스 중심 계산
 *  2. 폰 회전 → 박스 회전 (전방 방향에 맞춰 박스가 회전)
 *  3. OverlapMultiByObjectType 으로 월드 오버랩 쿼리
 *  4. 결과 중 타겟 Actor가 있으면 공격존 안으로 판정
 *
 * @author 김민우
 */

#include "AI/StateTree/Conditions/STCondition_InAttackZone.h"
#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"

// ============================================================================
// TestCondition
// ============================================================================
bool FSTCondition_InAttackZone::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	const FHellunaAITargetData& TargetData = InstanceData.TargetData;

	if (!TargetData.HasValidTarget())
		return !bCheckInside;

	AActor* TargetActor = TargetData.TargetActor.Get();
	if (!TargetActor)
		return !bCheckInside;

	AAIController* AIC = InstanceData.AIController;
	if (!AIC)
		return !bCheckInside;

	const APawn* Pawn = AIC->GetPawn();
	if (!Pawn)
		return !bCheckInside;

	UWorld* World = Pawn->GetWorld();
	if (!World)
		return !bCheckInside;

	// ── 박스 위치/회전 계산 ─────────────────────────────────────
	const FVector PawnLoc  = Pawn->GetActorLocation();
	const FQuat   PawnRot  = Pawn->GetActorQuat();
	const FVector Forward  = PawnRot.GetForwardVector();
	const FVector BoxCenter = PawnLoc + Forward * ForwardOffset;

	// ── 오버랩 쿼리 ────────────────────────────────────────────
	const FCollisionShape BoxShape = FCollisionShape::MakeBox(AttackZoneHalfExtent);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Pawn);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		BoxCenter,
		PawnRot,
		FCollisionObjectQueryParams::AllObjects,
		BoxShape,
		QueryParams);

	// ── 타겟 Actor의 Block All 콜리전과 겹치는지 확인 ─────────
	// UI 콜리전 박스(WorldDynamic 등)는 무시하고
	// 우주선 본체 메시(Block All)에 닿았을 때만 공격 판정
	bool bOverlapping = false;
	for (const FOverlapResult& Result : Overlaps)
	{
		if (Result.GetActor() != TargetActor)
			continue;

		const UPrimitiveComponent* Comp = Result.GetComponent();
		if (!Comp)
			continue;

		// Block All: 주요 채널 모두 Block인 컴포넌트만 허용
		const bool bBlocksPawn   = (Comp->GetCollisionResponseToChannel(ECC_Pawn)         == ECR_Block);
		const bool bBlocksStatic = (Comp->GetCollisionResponseToChannel(ECC_WorldStatic)  == ECR_Block);
		const bool bBlocksDynamic= (Comp->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);

		if (bBlocksPawn && bBlocksStatic && bBlocksDynamic)
		{
			bOverlapping = true;
			break;
		}
	}

#if ENABLE_DRAW_DEBUG
	{
		const FColor ZoneColor = bOverlapping ? FColor::Green : FColor::Red;

		// 박스 시각화 (폰 회전에 맞춰 회전된 박스)
		DrawDebugBox(
			World,
			BoxCenter,
			AttackZoneHalfExtent,
			PawnRot,
			ZoneColor,
			false,   // bPersistent
			0.f,     // LifeTime (1프레임)
			0,
			2.f      // Thickness
		);

		// 전방 방향 화살표
		DrawDebugDirectionalArrow(
			World,
			PawnLoc,
			PawnLoc + Forward * (ForwardOffset + AttackZoneHalfExtent.X),
			30.f,
			FColor::Yellow,
			false,
			0.f,
			0,
			1.5f
		);
	}
#endif

	return bCheckInside ? bOverlapping : !bOverlapping;
}