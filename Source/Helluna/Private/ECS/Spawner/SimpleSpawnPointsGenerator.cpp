/**
 * SimpleSpawnPointsGenerator.cpp
 *
 * MassSpawner 위치 기준 원형 패턴 스폰 포인트 생성기.
 * 엔진의 UMassEntityEQSSpawnPointsGenerator 구현 패턴을 그대로 따른다:
 *   1. BuildResultsFromEntityTypes()로 엔티티 타입별 결과 배열 생성
 *   2. UMassSpawnLocationProcessor를 SpawnDataProcessor로 지정
 *   3. FMassTransformsSpawnData에 Transform 배열 채우기
 *   4. FinishedGeneratingSpawnPointsDelegate로 결과 전달
 */

#include "ECS/Spawner/SimpleSpawnPointsGenerator.h"
#include "MassSpawnLocationProcessor.h"   // UMassSpawnLocationProcessor (위치 적용 Processor)
#include "MassSpawnerTypes.h"             // FMassTransformsSpawnData

DEFINE_LOG_CATEGORY_STATIC(LogSimpleSpawnGen, Log, All);

void USimpleSpawnPointsGenerator::Generate(
	UObject& QueryOwner,
	TConstArrayView<FMassSpawnedEntityType> EntityTypes,
	int32 Count,
	FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	if (Count <= 0)
	{
		FinishedGeneratingSpawnPointsDelegate.Execute(TArray<FMassEntitySpawnDataGeneratorResult>());
		return;
	}

	// ------------------------------------------------------------------
	// Step 1: MassSpawner(=QueryOwner)의 위치를 중심점으로 사용
	// ------------------------------------------------------------------
	FVector CenterLocation = FVector::ZeroVector;
	if (AActor* OwnerActor = Cast<AActor>(&QueryOwner))
	{
		CenterLocation = OwnerActor->GetActorLocation();
	}

	UE_LOG(LogSimpleSpawnGen, Log,
		TEXT("[SimpleSpawnGen] 스폰 포인트 생성 시작 - 중심: X=%.1f Y=%.1f Z=%.1f, 반지름: %.1f, 개수: %d"),
		CenterLocation.X, CenterLocation.Y, CenterLocation.Z, Radius, Count);

	// ------------------------------------------------------------------
	// Step 2: 원형 패턴으로 스폰 위치 생성
	// ------------------------------------------------------------------
	TArray<FVector> Locations;
	Locations.Reserve(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		// 원 위에 균등 분포
		const float Angle = (2.f * UE_PI * i) / Count;
		FVector Point = CenterLocation;
		Point.X += Radius * FMath::Cos(Angle);
		Point.Y += Radius * FMath::Sin(Angle);
		Point.Z += ZOffset;

		Locations.Add(Point);
	}

	// ------------------------------------------------------------------
	// Step 3: 엔진 패턴 그대로 — BuildResultsFromEntityTypes → Transform 채우기
	// (UMassEntityEQSSpawnPointsGenerator와 동일한 방식)
	// ------------------------------------------------------------------
	TArray<FMassEntitySpawnDataGeneratorResult> Results;
	BuildResultsFromEntityTypes(Count, EntityTypes, Results);

	const int32 LocationCount = Locations.Num();
	int32 LocationIndex = 0;

	for (FMassEntitySpawnDataGeneratorResult& Result : Results)
	{
		// 엔진 내장 Processor — FMassTransformsSpawnData를 읽어서 엔티티 위치에 적용
		Result.SpawnDataProcessor = UMassSpawnLocationProcessor::StaticClass();
		Result.SpawnData.InitializeAs<FMassTransformsSpawnData>();
		FMassTransformsSpawnData& Transforms = Result.SpawnData.GetMutable<FMassTransformsSpawnData>();

		Transforms.Transforms.Reserve(Result.NumEntities);
		for (int32 i = 0; i < Result.NumEntities; ++i)
		{
			FTransform& Transform = Transforms.Transforms.AddDefaulted_GetRef();
			Transform.SetLocation(Locations[LocationIndex % LocationCount]);
			LocationIndex++;
		}
	}

	UE_LOG(LogSimpleSpawnGen, Log,
		TEXT("[SimpleSpawnGen] 스폰 포인트 생성 완료 - 총 %d개 위치 할당"), LocationIndex);

	// ------------------------------------------------------------------
	// Step 4: MassSpawner에 결과 전달 → 실제 Mass Entity 생성됨
	// ------------------------------------------------------------------
	FinishedGeneratingSpawnPointsDelegate.Execute(Results);
}
