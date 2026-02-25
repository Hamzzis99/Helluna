/**
 * EnvQueryGenerator_HellunaAttackPos.cpp
 */

#include "AI/EQS/EnvQueryGenerator_HellunaAttackPos.h"

#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

UEnvQueryGenerator_HellunaAttackPos::UEnvQueryGenerator_HellunaAttackPos()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEnvQueryGenerator_HellunaAttackPos::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	TArray<FVector> ContextLocations;
	QueryInstance.PrepareContext(TargetContext, ContextLocations);
	if (ContextLocations.IsEmpty()) return;

	UWorld* World = GEngine->GetWorldFromContextObject(
		QueryInstance.Owner.Get(), EGetWorldErrorMode::LogAndReturnNull);
	if (!World) return;

	const FVector QuerierLocation = QueryInstance.Owner.IsValid()
		? Cast<AActor>(QueryInstance.Owner.Get())->GetActorLocation()
		: FVector::ZeroVector;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	const float SafeStep = FMath::Max(AngleStep, 1.f);

	struct FCandidatePoint { FVector Location; float Score; };
	TArray<FCandidatePoint> Candidates;

	// 반경 레이어: Min, Mid, Max
	TArray<float> Radii;
	Radii.Add(MinRadius);
	if (!FMath::IsNearlyEqual(MinRadius, MaxRadius))
	{
		Radii.Add((MinRadius + MaxRadius) * 0.5f);
		Radii.Add(MaxRadius);
	}

	for (const FVector& Center : ContextLocations)
	{
		for (const float Radius : Radii)
		{
			float Angle = 0.f;
			while (Angle < 360.f)
			{
				const float Rad = FMath::DegreesToRadians(Angle);
				FVector CandidatePos(
					Center.X + FMath::Cos(Rad) * Radius,
					Center.Y + FMath::Sin(Rad) * Radius,
					Center.Z
				);

				// Filter 1: NavMesh
				if (NavSys)
				{
					FNavLocation NavLoc;
					const FVector Extent(NavMeshExtent, NavMeshExtent, NavMeshExtent);
					if (!NavSys->ProjectPointToNavigation(CandidatePos, NavLoc, Extent))
					{
						Angle += SafeStep;
						continue;
					}
					CandidatePos = NavLoc.Location;
				}

				// Filter 2: Pawn 겹침
				if (PawnOverlapRadius > 0.f)
				{
					TArray<FOverlapResult> Overlaps;
					FCollisionQueryParams Params;
					if (AActor* Owner = Cast<AActor>(QueryInstance.Owner.Get()))
						Params.AddIgnoredActor(Owner);

					if (World->OverlapMultiByChannel(
						Overlaps, CandidatePos, FQuat::Identity,
						ECC_Pawn, FCollisionShape::MakeSphere(PawnOverlapRadius), Params)
						&& Overlaps.Num() > 0)
					{
						Angle += SafeStep;
						continue;
					}
				}

				Candidates.Add({ CandidatePos, (float)FVector::Dist(CandidatePos, QuerierLocation) });
				Angle += SafeStep;
			}
		}
	}

	// 후보가 없으면 겹침 체크 없이 재시도
	if (Candidates.IsEmpty())
	{
		for (const FVector& Center : ContextLocations)
		{
			for (const float Radius : Radii)
			{
				float Angle = 0.f;
				while (Angle < 360.f)
				{
					const float Rad = FMath::DegreesToRadians(Angle);
					FVector CandidatePos(
						Center.X + FMath::Cos(Rad) * Radius,
						Center.Y + FMath::Sin(Rad) * Radius,
						Center.Z
					);
					if (NavSys)
					{
						FNavLocation NavLoc;
						if (NavSys->ProjectPointToNavigation(CandidatePos, NavLoc,
							FVector(NavMeshExtent, NavMeshExtent, NavMeshExtent)))
							CandidatePos = NavLoc.Location;
					}
					Candidates.Add({ CandidatePos, (float)FVector::Dist(CandidatePos, QuerierLocation) });
					Angle += SafeStep;
				}
			}
		}
	}

	Candidates.Sort([this](const FCandidatePoint& A, const FCandidatePoint& B)
	{
		return bPreferNearest ? A.Score < B.Score : A.Score > B.Score;
	});

	for (const FCandidatePoint& Candidate : Candidates)
		QueryInstance.AddItemData<UEnvQueryItemType_Point>(Candidate.Location);
}

FText UEnvQueryGenerator_HellunaAttackPos::GetDescriptionTitle() const
{
	return FText::FromString(TEXT("Helluna: Attack Position Around Target"));
}

FText UEnvQueryGenerator_HellunaAttackPos::GetDescriptionDetails() const
{
	return FText::FromString(FString::Printf(
		TEXT("MinRadius=%.0f, MaxRadius=%.0f, AngleStep=%.0f, OverlapR=%.0f"),
		MinRadius, MaxRadius, AngleStep, PawnOverlapRadius
	));
}
