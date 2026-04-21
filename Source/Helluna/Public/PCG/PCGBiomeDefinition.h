#pragma once

#include "CoreMinimal.h"
#include "PCGBiomeDefinition.generated.h"

/**
 * 바이옴 유형
 */
UENUM(BlueprintType)
enum class EPCGBiomeType : uint8
{
	Desert		UMETA(DisplayName = "사막 (갈색 중앙)"),
	Marble		UMETA(DisplayName = "대리석 (흰색 지역1)"),
	MysticForest UMETA(DisplayName = "몽환 숲 (보라색 지역2)"),
	Any			UMETA(DisplayName = "모든 바이옴")
};

/**
 * 바이옴 영역 정의.
 * 월드에 배치된 액터 위치를 기준으로 원형 영역을 정의한다.
 * 여러 바이옴이 겹치면 가장 가까운 중심의 바이옴이 선택된다.
 */
USTRUCT(BlueprintType)
struct FPCGBiomeZone
{
	GENERATED_BODY()

	/** 바이옴 유형 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "바이옴"))
	EPCGBiomeType BiomeType = EPCGBiomeType::Desert;

	/** 바이옴 중심 위치 (월드 좌표 XY) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "중심 위치"))
	FVector2D Center = FVector2D::ZeroVector;

	/** 바이옴 반경(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "반경(cm)", ClampMin = "100.0"))
	float Radius = 50000.f;
};

/**
 * 주어진 월드 위치가 어느 바이옴에 속하는지 판정.
 * 가장 가까운 바이옴 중심을 기준으로 결정하며, 어디에도 속하지 않으면 Any를 반환.
 */
inline EPCGBiomeType DetermineBiome(const FVector& WorldLocation, const TArray<FPCGBiomeZone>& Zones)
{
	EPCGBiomeType Best = EPCGBiomeType::Any;
	float BestDistSq = TNumericLimits<float>::Max();

	const FVector2D Loc2D(WorldLocation.X, WorldLocation.Y);

	for (const FPCGBiomeZone& Zone : Zones)
	{
		const float DistSq = FVector2D::DistSquared(Loc2D, Zone.Center);
		if (DistSq <= Zone.Radius * Zone.Radius && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Zone.BiomeType;
		}
	}

	return Best;
}
