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
// [SpawnGroundSnapV1] 각 스폰점을 실제 지면에 스냅하기 위한 navmesh/트레이스용
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "GameFramework/Actor.h"

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

	// [SpawnGroundSnapV1] 지면 스냅용 — 고정 Z 원형 배치는 경사/단차/구멍/벽에서 일부 점이 공중·지하·
	//   navmesh 밖에 떨어져 엔티티가 스폰 직후 소멸(요청 수보다 적게 생존)함. 각 점을 실제 지면에 스냅한다.
	//   "여유롭게" — navmesh 투영 extent / 라인트레이스 범위를 넉넉히 잡아 거의 모든 점이 바닥을 잡도록.
	UWorld* World = QueryOwner.GetWorld();
	UNavigationSystemV1* NavSys = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	AActor* OwnerActor = Cast<AActor>(&QueryOwner);

	// 넉넉한 클리어런스 — ZOffset(에디터값) 위에 추가 여유. 바닥 박힘 방지 + 살짝 떨어지며 안착.
	const float GenerousClearance = FMath::Max(ZOffset, 0.f) + 150.f;
	const FVector NavExtent(800.f, 800.f, 12000.f); // 넉넉한 navmesh 검색 박스
	const float TraceUp = 6000.f;                   // 넉넉한 상향 시작
	const float TraceDown = 20000.f;                // 넉넉한 하향 탐색

	for (int32 i = 0; i < Count; ++i)
	{
		// 원 위에 균등 분포
		const float Angle = (2.f * UE_PI * i) / Count;
		FVector Point = CenterLocation;
		Point.X += Radius * FMath::Cos(Angle);
		Point.Y += Radius * FMath::Sin(Angle);

		// 지면 스냅: 1) navmesh 투영 우선  2) 실패 시 라인트레이스  3) 최종 폴백 = 중심 Z
		FVector Ground = Point;
		Ground.Z = CenterLocation.Z;
		bool bSnapped = false;

		if (NavSys)
		{
			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(Point, NavLoc, NavExtent))
			{
				Ground = NavLoc.Location;
				bSnapped = true;
			}
		}
		if (!bSnapped && World)
		{
			FHitResult Hit;
			FCollisionQueryParams P(SCENE_QUERY_STAT(SimpleSpawnGround), false);
			if (OwnerActor) { P.AddIgnoredActor(OwnerActor); }
			const FVector From = Point + FVector(0.f, 0.f, TraceUp);
			const FVector To   = Point - FVector(0.f, 0.f, TraceDown);
			if (World->LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic, P)
				|| World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, P))
			{
				Ground = Hit.ImpactPoint;
				bSnapped = true;
			}
		}

		Ground.Z += GenerousClearance;
		Locations.Add(Ground);
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
