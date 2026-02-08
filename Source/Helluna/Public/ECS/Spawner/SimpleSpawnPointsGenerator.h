/**
 * SimpleSpawnPointsGenerator.h
 *
 * EQS 없이 MassSpawner에서 사용할 수 있는 간단한 스폰 위치 생성기.
 * MassSpawner의 Spawn Data Generators 드롭다운에서 선택 가능.
 *
 * MassSpawner 위치를 중심으로 원형/그리드 패턴으로 스폰 포인트를 생성한다.
 * NavMesh에 의존하지 않으므로 NavMesh 없이도 즉시 테스트 가능.
 *
 * 나중에 NavMesh가 준비되면 EQS SpawnPoints Generator로 교체하면 된다.
 * (MassSpawner 프레임워크 구조는 동일)
 */

#pragma once

#include "CoreMinimal.h"
#include "MassEntitySpawnDataGeneratorBase.h"
#include "SimpleSpawnPointsGenerator.generated.h"

/**
 * USimpleSpawnPointsGenerator
 *
 * MassSpawner 위치 기준으로 원형 패턴의 스폰 포인트를 생성.
 * EQS나 NavMesh 없이 동작하여 빠른 테스트에 적합.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Simple Circle Generator (NavMesh 불필요)"))
class HELLUNA_API USimpleSpawnPointsGenerator : public UMassEntitySpawnDataGeneratorBase
{
	GENERATED_BODY()

public:
	virtual void Generate(
		UObject& QueryOwner,
		TConstArrayView<FMassSpawnedEntityType> EntityTypes,
		int32 Count,
		FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const override;

protected:
	/** 스폰 원의 반지름 (단위: cm). 기본값 2000 = 20m */
	UPROPERTY(EditAnywhere, Category = "Spawn Pattern",
		meta = (DisplayName = "Radius (반지름 cm)", ClampMin = "100.0", UIMin = "100.0"))
	float Radius = 2000.f;

	/** 스폰 위치의 Z축 오프셋 (지면 위로 띄우기, 단위: cm) */
	UPROPERTY(EditAnywhere, Category = "Spawn Pattern",
		meta = (DisplayName = "Z Offset (높이 오프셋 cm)"))
	float ZOffset = 100.f;
};
