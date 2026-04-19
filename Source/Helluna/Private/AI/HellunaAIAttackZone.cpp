// Capstone Project Helluna

#include "AI/HellunaAIAttackZone.h"

#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

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
	const FVector BoxCenter = PawnLoc + Forward * ForwardOffset;

	const FCollisionShape BoxShape = FCollisionShape::MakeBox(HalfExtent);

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

	for (const FOverlapResult& Result : Overlaps)
	{
		if (Result.GetActor() != Target) continue;

		const UPrimitiveComponent* Component = Result.GetComponent();
		if (!Component) continue;

		const bool bBlocksPawn = (Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
		const bool bBlocksStatic = (Component->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block);
		const bool bBlocksDynamic = (Component->GetCollisionResponseToChannel(ECC_WorldDynamic) == ECR_Block);
		if (bBlocksPawn && bBlocksStatic && bBlocksDynamic)
		{
			return true;
		}
	}

	return false;
}
