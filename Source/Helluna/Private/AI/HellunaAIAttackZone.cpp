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
