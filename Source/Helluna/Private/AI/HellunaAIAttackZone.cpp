// Capstone Project Helluna

#include "AI/HellunaAIAttackZone.h"

#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Object/ResourceUsingObject/HellunaTurretBase.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

// [AttackZoneDebugDrawV1] 콘솔 변수로 AttackZone 오버랩 박스 시각화 토글.
//   에디터 콘솔 입력: `Helluna.DrawAttackZone 1` (on) / `0` (off)
//   - 녹색: 판정 성공 (오버랩 + Pawn/WorldStatic/WorldDynamic 모두 Block)
//   - 노란색: 판정 실패지만 Target 오버랩은 잡힘 (채널 조건 미달)
//   - 빨간색: 타겟 오버랩 자체가 없음
static TAutoConsoleVariable<int32> CVarDrawAttackZone(
	TEXT("Helluna.DrawAttackZone"),
	0,
	TEXT("AttackZone 박스 디버그 드로잉 토글.\n")
	TEXT(" 0 = off (기본)\n")
	TEXT(" 1 = 한 틱짜리 박스 (매 판정마다 새로 그림)\n")
	TEXT(" 2 = 2초 잔상 (느린 관찰용)"),
	ECVF_Cheat);

bool HellunaAI::IsTargetInAttackZone(
	const APawn* Pawn,
	const AActor* Target,
	const FVector& HalfExtent,
	float ForwardOffset)
{
	if (!Pawn || !Target) return false;

	UWorld* World = Pawn->GetWorld();
	if (!World) return false;

	const FVector PawnLoc = Pawn->GetActorLocation();
	const FQuat PawnRot = Pawn->GetActorQuat();
	const FVector Forward = PawnRot.GetForwardVector();

	// [NarrowZoneV7] 우주선/타워 모두 X 0.85, ForwardOffset 0.90 으로 통일.
	FVector EffectiveHalfExtent = HalfExtent;
	float EffectiveForwardOffset = ForwardOffset;
	if (Target &&
		(Target->IsA<AHellunaTurretBase>() || Target->IsA<AResourceUsingObject_SpaceShip>()))
	{
		EffectiveHalfExtent.X *= 0.85f;
		EffectiveForwardOffset *= 0.90f;
	}

	const FVector BoxCenter = PawnLoc + Forward * EffectiveForwardOffset;
	const FCollisionShape BoxShape = FCollisionShape::MakeBox(EffectiveHalfExtent);

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

	bool bTargetOverlapped = false;
	bool bHit = false;
	for (const FOverlapResult& Result : Overlaps)
	{
		if (Result.GetActor() != Target) continue;

		bTargetOverlapped = true;

		const UPrimitiveComponent* Component = Result.GetComponent();
		if (!Component) continue;

		const bool bBlocksPawn = (Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
		const bool bBlocksStatic = (Component->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
		const bool bBlocksDynamic = (Component->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
		if (bBlocksPawn && bBlocksStatic && bBlocksDynamic)
		{
			bHit = true;
			break;
		}
	}

	// [AttackZoneDebugDrawV1] CVar on일 때만 박스 그리기.
	const int32 DrawMode = CVarDrawAttackZone.GetValueOnAnyThread();
	if (DrawMode > 0)
	{
		const FColor BoxColor = bHit
			? FColor::Green
			: (bTargetOverlapped ? FColor::Yellow : FColor::Red);
		const float LifeTime = (DrawMode >= 2) ? 2.0f : 0.f;
		DrawDebugBox(
			World,
			BoxCenter,
			EffectiveHalfExtent,
			PawnRot,
			BoxColor,
			/*bPersistent*/ false,
			LifeTime,
			/*DepthPriority*/ 0,
			/*Thickness*/ 1.5f);
	}

	return bHit;
}

// ============================================================================
// [SurfaceDistanceV1] 표면 최단 거리
// ============================================================================
namespace
{
	// 컴포넌트의 OBB(로컬 바운드를 월드로 변환한 박스) 표면까지 최단 거리.
	//   GetClosestPointOnCollision 이 복합 콜리전에서 -1 을 반환할 때의 폴백.
	//   FromLoc 을 컴포넌트 로컬 공간으로 옮겨 로컬 AABB 에 clamp → 월드로 되돌려 거리 측정.
	//   회전/스케일이 반영된 OBB 최단 거리이며 레이캐스트가 필요 없다.
	float ClosestDistanceToComponentOBB(const FVector& FromLoc, const UPrimitiveComponent* Prim)
	{
		if (!Prim) return TNumericLimits<float>::Max();

		const FTransform CompXf = Prim->GetComponentTransform();
		// 로컬 공간 바운드 (Identity 변환으로 CalcBounds → 로컬 AABB)
		const FBoxSphereBounds LocalBounds = Prim->CalcBounds(FTransform::Identity);
		const FVector BoxMin = LocalBounds.Origin - LocalBounds.BoxExtent;
		const FVector BoxMax = LocalBounds.Origin + LocalBounds.BoxExtent;

		const FVector LocalP = CompXf.InverseTransformPosition(FromLoc);
		const FVector LocalClosest(
			FMath::Clamp(LocalP.X, BoxMin.X, BoxMax.X),
			FMath::Clamp(LocalP.Y, BoxMin.Y, BoxMax.Y),
			FMath::Clamp(LocalP.Z, BoxMin.Z, BoxMax.Z));

		const FVector WorldClosest = CompXf.TransformPosition(LocalClosest);
		return FVector::Dist(FromLoc, WorldClosest);
	}

	bool IsBlockLikeCombatPrim(const UPrimitiveComponent* Prim)
	{
		if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision) return false;
		return (Prim->GetCollisionResponseToChannel(ECC_Pawn)         == ECR_Block)
			|| (Prim->GetCollisionResponseToChannel(ECC_WorldStatic)  == ECR_Block)
			|| (Prim->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
	}

	// 후보 컴포넌트들 중 표면 최단 거리. GetClosestPointOnCollision 우선, 미지원이면 OBB 폴백.
	//   TArrayView 로 받아 TArray/TInlineComponentArray 등 allocator 무관하게 호출 가능.
	bool TryMinSurfaceDistance(const FVector& FromLoc,
		TArrayView<UPrimitiveComponent* const> Candidates, float& OutMin)
	{
		float MinDist = TNumericLimits<float>::Max();
		bool bFound = false;
		for (UPrimitiveComponent* Prim : Candidates)
		{
			if (!Prim || Prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				continue;

			FVector Closest;
			const float D = Prim->GetClosestPointOnCollision(FromLoc, Closest);

			float Effective;
			if (D > 0.f)        Effective = D;                                  // 단순 셰이프: 정확
			else if (D == 0.f)  { OutMin = 0.f; return true; }                  // 내부
			else                Effective = ClosestDistanceToComponentOBB(FromLoc, Prim); // 복합: OBB 폴백

			if (Effective < MinDist) { MinDist = Effective; bFound = true; }
		}
		if (bFound) { OutMin = MinDist; return true; }
		return false;
	}
}

float HellunaAI::GetTargetSurfaceDistance(const FVector& FromLoc, const AActor* Target)
{
	if (!Target) return TNumericLimits<float>::Max();

	// 힙 할당 없이 컴포넌트 조회 (50+ 몹이 매 틱 호출하므로 per-call alloc 방지)
	TInlineComponentArray<UPrimitiveComponent*> Prims(Target);

	float Dist = TNumericLimits<float>::Max();

	// 1순위: "ShipCombatCollision" 태그 컴포넌트
	TInlineComponentArray<UPrimitiveComponent*> Tagged;
	for (UPrimitiveComponent* Prim : Prims)
		if (Prim && Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
			Tagged.Add(Prim);
	if (Tagged.Num() > 0 && TryMinSurfaceDistance(FromLoc, Tagged, Dist))
		return Dist;

	// 2순위: Block 반응 컴포넌트
	TInlineComponentArray<UPrimitiveComponent*> BlockPrims;
	for (UPrimitiveComponent* Prim : Prims)
		if (IsBlockLikeCombatPrim(Prim))
			BlockPrims.Add(Prim);
	if (TryMinSurfaceDistance(FromLoc, BlockPrims, Dist))
		return Dist;

	// 3순위: 루트 프리미티브 OBB
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
		if (Root->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			return ClosestDistanceToComponentOBB(FromLoc, Root);

	// 4순위: 중심점 거리 폴백
	return FVector::Dist(FromLoc, Target->GetActorLocation());
}
