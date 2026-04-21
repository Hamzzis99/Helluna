#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PCG/PCGBiomeDefinition.h"

/**
 * PCG Scatter 컴포넌트들이 공유하는 유틸리티.
 * - Foliage(ISM) 무시 라인트레이스 파라미터 생성
 * - 지면 트레이스
 * - 가중치 기반 랜덤 선택
 */
namespace PCGScatterUtils
{
	/** Foliage(ISM) 액터를 무시하는 트레이스 파라미터 생성 */
	inline FCollisionQueryParams BuildFoliageIgnoreParams(UWorld* World, FName Tag = NAME_None)
	{
		FCollisionQueryParams Params(Tag, true);

		if (!World) return Params;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UInstancedStaticMeshComponent*> ISMs;
			(*It)->GetComponents<UInstancedStaticMeshComponent>(ISMs);
			if (ISMs.Num() > 0)
			{
				Params.AddIgnoredActor(*It);
			}
		}

		return Params;
	}

	/** 지면 트레이스 — Foliage 무시 */
	inline bool TraceToGround(UWorld* World, const FCollisionQueryParams& Params,
		const FVector& InXY, float ZHigh, float ZLow,
		FVector& OutLocation, FVector* OutNormal = nullptr)
	{
		if (!World) return false;

		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit,
			FVector(InXY.X, InXY.Y, ZHigh),
			FVector(InXY.X, InXY.Y, ZLow),
			ECC_Visibility, Params))
		{
			OutLocation = Hit.ImpactPoint;
			if (OutNormal) *OutNormal = Hit.ImpactNormal;
			return true;
		}
		return false;
	}

	/** 가중치 배열에서 랜덤 인덱스 선택 */
	template<typename TEntry, typename TWeightGetter>
	int32 SelectWeightedIndex(FRandomStream& Rng, const TArray<TEntry>& Entries, TWeightGetter GetWeight)
	{
		float TotalWeight = 0.f;
		for (const TEntry& E : Entries)
			TotalWeight += GetWeight(E);

		if (TotalWeight <= 0.f) return 0;

		float Roll = Rng.FRandRange(0.f, TotalWeight);
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			Roll -= GetWeight(Entries[i]);
			if (Roll <= 0.f) return i;
		}
		return Entries.Num() - 1;
	}

	/** 바이옴 필터링된 가중치 인덱스 선택 */
	template<typename TEntry, typename TWeightGetter, typename TBiomeGetter>
	int32 SelectWeightedIndexFiltered(FRandomStream& Rng, const TArray<TEntry>& Entries,
		TWeightGetter GetWeight, TBiomeGetter GetBiome, EPCGBiomeType TargetBiome)
	{
		TArray<int32> Valid;
		float TotalWeight = 0.f;

		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			EPCGBiomeType B = GetBiome(Entries[i]);
			if (B == EPCGBiomeType::Any || B == TargetBiome)
			{
				Valid.Add(i);
				TotalWeight += GetWeight(Entries[i]);
			}
		}

		if (Valid.Num() == 0) return -1;

		float Roll = Rng.FRandRange(0.f, TotalWeight);
		for (int32 VI : Valid)
		{
			Roll -= GetWeight(Entries[VI]);
			if (Roll <= 0.f) return VI;
		}
		return Valid.Last();
	}

	/** 원형 영역 내 균등 분포 랜덤 포인트 (sqrt 보정) */
	inline FVector RandomPointInCircle(FRandomStream& Rng, const FVector& Origin, float Radius)
	{
		const float Angle = Rng.FRandRange(0.f, 360.f);
		const float Dist = FMath::Sqrt(Rng.FRandRange(0.f, 1.f)) * Radius;
		return Origin + FVector(
			FMath::Cos(FMath::DegreesToRadians(Angle)) * Dist,
			FMath::Sin(FMath::DegreesToRadians(Angle)) * Dist,
			0.f);
	}

	/** 최소 간격 체크 */
	inline bool CheckMinSpacing(const FVector& Loc, const TArray<FVector>& Existing, float MinSpacingSq)
	{
		for (const FVector& E : Existing)
		{
			if (FVector::DistSquared(Loc, E) < MinSpacingSq)
				return false;
		}
		return true;
	}
}
