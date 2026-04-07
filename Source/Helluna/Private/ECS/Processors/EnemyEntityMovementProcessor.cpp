/**
 * EnemyEntityMovementProcessor.cpp
 *
 * 매 틱 두 가지 처리:
 *   1. 이동: Entity를 GoalLocation 방향으로 이동
 *   2. 분리: Entity끼리 겹치면 밀어냄
 *
 * ■ 구조
 *   ForEachEntityChunk 1회 호출로 모든 데이터를 flat 배열에 복사 후
 *   분리 계산 → 결과를 다시 ForEachEntityChunk로 적용
 */

#include "ECS/Processors/EnemyEntityMovementProcessor.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "ECS/Fragments/EnemyMassFragments.h"
#include "AI/SpaceShipAttackSlotManager.h"
#include "DebugHelper.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"

// ============================================================================
// 생성자
// ============================================================================
UEnemyEntityMovementProcessor::UEnemyEntityMovementProcessor()
{
	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Standalone
	);

	bRequiresGameThreadExecution = true;
	RegisterQuery(EntityQuery);
	RegisterQuery(GoalCacheQuery);
}
// ============================================================================
// ConfigureQueries
// ============================================================================
void UEnemyEntityMovementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// 이동 처리 쿼리
	EntityQuery.RegisterWithProcessor(*this);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEnemyDataFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEnemySpawnStateFragment>(EMassFragmentAccess::ReadOnly);

	// GoalLocation 캐싱 쿼리 (별도 분리)
	GoalCacheQuery.RegisterWithProcessor(*this);
	GoalCacheQuery.AddRequirement<FEnemyDataFragment>(EMassFragmentAccess::ReadWrite);
	GoalCacheQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
}
// ============================================================================
// Execute
// ============================================================================
void UEnemyEntityMovementProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	const float DeltaTime = Context.GetDeltaTimeSeconds();

	// ----------------------------------------------------------------
	// Step 0: GoalLocation 캐싱 (StateTree에 의존하지 않고 직접 처리)
	// 60프레임마다 갱신한다. Actor 단계는 슬롯 시스템이 분산을 담당하므로
	// Entity 단계도 같은 슬롯/링 정보를 사용해 우주선 주변으로 퍼지게 만든다.
	//
	// [버그 수정] 우주선 중심점(GetActorLocation)을 GoalLocation으로 쓰면
	// Entity가 콜리전 없이 순수 수학 이동을 하므로 우주선 메쉬를 관통해
	// 중심까지 파고드는 현상이 발생.
	//
	// 해결:
	//   1) 우주선 슬롯이 있으면 엔티티별 해시 기반으로 슬롯/근처 지점을 선택
	//   2) 슬롯이 없으면 ShipCombatCollision 컴포넌트 주변으로 분산
	//   3) 그것도 없으면 우주선 둘레 링으로 분산
	// ----------------------------------------------------------------
	if (GFrameCounter % 60 == 0)
	{
		UWorld* World = Context.GetWorld();
		if (World)
		{
			// 우주선 Actor와 ShipCombatCollision 박스들을 미리 수집 (모든 Entity 공통)
			// #2 최적화: TActorIterator 반복 스캔 대신 캐시 사용
			TArray<FVector> ApproachPoints;
			TArray<FVector> SlotPoints;
			TArray<FVector> SlotNormals;

			if (!CachedSpaceShip.IsValid())
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					if (It->ActorHasTag(TEXT("SpaceShip")))
					{
						CachedSpaceShip = *It;
						break;
					}
				}
			}
			AActor* GoalActor = CachedSpaceShip.Get();

			if (GoalActor)
			{
				// ShipCombatCollision 태그 컴포넌트 수집
				TArray<UPrimitiveComponent*> Prims;
				GoalActor->GetComponents<UPrimitiveComponent>(Prims);
				for (UPrimitiveComponent* Prim : Prims)
				{
					if (Prim && Prim->ComponentHasTag(TEXT("ShipCombatCollision")))
						ApproachPoints.Add(Prim->GetComponentLocation());
				}

				if (USpaceShipAttackSlotManager* SlotManager =
					GoalActor->FindComponentByClass<USpaceShipAttackSlotManager>())
				{
					for (const FAttackSlot& Slot : SlotManager->GetSlots())
					{
						if (Slot.IsValid())
						{
							const FVector SlotAnchor = Slot.SurfaceLocation.IsZero() ? Slot.WorldLocation : Slot.SurfaceLocation;
							const FVector SlotNormal =
								Slot.SurfaceNormal.IsNearlyZero()
									? (SlotAnchor - GoalActor->GetActorLocation()).GetSafeNormal2D()
									: Slot.SurfaceNormal.GetSafeNormal2D();
							SlotPoints.Add(SlotAnchor);
							SlotNormals.Add(SlotNormal);
						}
					}
				}

				FVector BoundsOrigin = FVector::ZeroVector;
				FVector BoundsExtent = FVector::ZeroVector;
				GoalActor->GetActorBounds(true, BoundsOrigin, BoundsExtent);
				const float RingRadius = FMath::Max(BoundsExtent.Size2D() + 220.f, 260.f);
				for (int32 RingIndex = 0; RingIndex < 8; ++RingIndex)
				{
					const float AngleRad = FMath::DegreesToRadians(360.f * static_cast<float>(RingIndex) / 8.f);
					const FVector RingDir(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.f);
					ApproachPoints.Add(GoalActor->GetActorLocation() + RingDir * RingRadius);
				}
			}

			if (GoalActor)
			{
				const FVector FallbackLoc = GoalActor->GetActorLocation();
				auto MakeSpreadOffset = [](uint32 Seed, const FVector& BasisDir, float MinRadius, float MaxRadius)
				{
					FVector Forward = FVector(BasisDir.X, BasisDir.Y, 0.f).GetSafeNormal();
					if (Forward.IsNearlyZero())
					{
						const float FallbackAngle = FMath::DegreesToRadians(static_cast<float>(Seed % 360));
						Forward = FVector(FMath::Cos(FallbackAngle), FMath::Sin(FallbackAngle), 0.f);
					}

					const FVector Right(-Forward.Y, Forward.X, 0.f);
					const float AngleDeg = static_cast<float>((Seed >> 8) % 360);
					const float RadiusAlpha = static_cast<float>((Seed >> 16) & 0xFF) / 255.f;
					const float Radius = FMath::Lerp(MinRadius, MaxRadius, RadiusAlpha);
					const float AngleRad = FMath::DegreesToRadians(AngleDeg);
					return (Forward * FMath::Cos(AngleRad) + Right * FMath::Sin(AngleRad)) * Radius;
				};

				GoalCacheQuery.ForEachEntityChunk(Context,
					[&](FMassExecutionContext& ChunkCtx)
					{
						const TArrayView<FEnemyDataFragment> DataList =
							ChunkCtx.GetMutableFragmentView<FEnemyDataFragment>();
						const TConstArrayView<FTransformFragment> TransformList =
							ChunkCtx.GetFragmentView<FTransformFragment>();

						for (int32 i = 0; i < ChunkCtx.GetNumEntities(); ++i)
						{
							FEnemyDataFragment& Data = DataList[i];
							const FMassEntityHandle Entity = ChunkCtx.GetEntity(i);
							const uint32 EntitySeed = GetTypeHash(Entity);
							const FVector EntityLoc = TransformList[i].GetTransform().GetLocation();

							if (SlotPoints.Num() > 0)
							{
								const int32 SlotIndex = static_cast<int32>(EntitySeed % SlotPoints.Num());
								const FVector SlotLoc = SlotPoints[SlotIndex];
								const FVector BasisDir =
									SlotNormals.IsValidIndex(SlotIndex) && !SlotNormals[SlotIndex].IsNearlyZero()
										? SlotNormals[SlotIndex]
										: (SlotLoc - FallbackLoc).GetSafeNormal2D();
								const FVector Tangent(-BasisDir.Y, BasisDir.X, 0.f);
								const float ForwardAlpha = static_cast<float>((EntitySeed >> 16) & 0xFF) / 255.f;
								const float SideAlpha = static_cast<float>((EntitySeed >> 24) & 0xFF) / 255.f;
								const float ForwardOffset = FMath::Lerp(70.f, 150.f, ForwardAlpha);
								const float SideOffset = FMath::Lerp(-120.f, 120.f, SideAlpha);
								Data.GoalLocation = SlotLoc + BasisDir * ForwardOffset + Tangent * SideOffset;
							}
							else if (ApproachPoints.Num() > 0)
							{
								const int32 PointIndex = static_cast<int32>(EntitySeed % ApproachPoints.Num());
								const FVector AnchorPoint = ApproachPoints[PointIndex];
								const FVector BasisDir = (AnchorPoint - FallbackLoc).GetSafeNormal2D();
								Data.GoalLocation = AnchorPoint + MakeSpreadOffset(EntitySeed, BasisDir, 90.f, 240.f);
							}
							else
							{
								const FVector ShipDir = (EntityLoc - FallbackLoc).GetSafeNormal2D();
								Data.GoalLocation = FallbackLoc + MakeSpreadOffset(EntitySeed, ShipDir, 220.f, 520.f);
							}

							Data.GoalLocation.Z = FallbackLoc.Z;
							Data.bGoalLocationCached = true;
						}
					}
				);
			}
		}
	}

	// ----------------------------------------------------------------
	// Step 1: 이동 대상 Entity 데이터를 flat 배열로 수집
	// ----------------------------------------------------------------
	struct FEntityData
	{
		FVector  CurrentLoc;
		FVector  GoalLoc;
		float    MoveSpeed;
		float    SeparationRadius;
		FTransformFragment* TransformPtr = nullptr;
		FEnemyDataFragment* DataPtr      = nullptr;  // LastMoveDirection 저장용
	};

	TArray<FEntityData> Entities;
	Entities.Reserve(256);

	// ForEachEntityChunk 1회 - 데이터 수집 + 포인터 캐싱
	EntityQuery.ForEachEntityChunk(Context,
		[&](FMassExecutionContext& ChunkCtx)
		{
			const TArrayView<FTransformFragment> TransformList =
				ChunkCtx.GetMutableFragmentView<FTransformFragment>();
			const TArrayView<FEnemyDataFragment> DataList =
				ChunkCtx.GetMutableFragmentView<FEnemyDataFragment>();
			const TConstArrayView<FEnemySpawnStateFragment> SpawnStateList =
				ChunkCtx.GetFragmentView<FEnemySpawnStateFragment>();

			const int32 NumEntities = ChunkCtx.GetNumEntities();
			for (int32 i = 0; i < NumEntities; ++i)
			{
				const FEnemySpawnStateFragment& SpawnState = SpawnStateList[i];
				const FEnemyDataFragment& Data = DataList[i];

				if (SpawnState.bHasSpawnedActor || SpawnState.bDead)
					continue;

				if (!Data.bGoalLocationCached)
					continue;

				FEntityData& ED = Entities.AddDefaulted_GetRef();
				ED.CurrentLoc        = TransformList[i].GetTransform().GetLocation();
				ED.MoveSpeed         = Data.EntityMoveSpeed;
				ED.SeparationRadius  = Data.EntitySeparationRadius;
				ED.TransformPtr      = &TransformList[i];
				ED.DataPtr           = &DataList[i];

				ED.GoalLoc = Data.GoalLocation;
				if (Data.bMove2DOnly)
					ED.GoalLoc.Z = ED.CurrentLoc.Z;
			}
		}
	);

	const int32 TotalEntities = Entities.Num();



	if (TotalEntities == 0)
		return;

	// 성능 측정 시작
	const double SepStartTime = FPlatformTime::Seconds();

	// ----------------------------------------------------------------
	// Step 2: 이동 + 분리 계산 → NewLoc 배열 생성
	// #1 최적화: O(N²) → 공간 해시 그리드 O(N*K)
	// ----------------------------------------------------------------
	TArray<FVector> NewLocations;
	NewLocations.SetNum(TotalEntities);

	// 공간 해시 그리드 구축 (셀 크기 = MaxSeparationRadius * 2 이상)
	constexpr float CellSize = 200.f;
	constexpr float InvCellSize = 1.f / CellSize;

	auto CellKey = [](const FVector& V, float InvCS) -> int64 {
		const int32 CX = FMath::FloorToInt(V.X * InvCS);
		const int32 CY = FMath::FloorToInt(V.Y * InvCS);
		return (static_cast<int64>(CX) << 32) | static_cast<int64>(static_cast<uint32>(CY));
	};

	TMap<int64, TArray<int32, TInlineAllocator<8>>> Grid;
	Grid.Reserve(TotalEntities);
	for (int32 i = 0; i < TotalEntities; ++i)
	{
		Grid.FindOrAdd(CellKey(Entities[i].CurrentLoc, InvCellSize)).Add(i);
	}

	for (int32 A = 0; A < TotalEntities; ++A)
	{
		const FEntityData& EA = Entities[A];

		// 이동 벡터
		FVector MoveDir = FVector::ZeroVector;
		if (FVector::DistSquared(EA.CurrentLoc, EA.GoalLoc) > 50.f * 50.f)
			MoveDir = (EA.GoalLoc - EA.CurrentLoc).GetSafeNormal();

		// 분리 벡터 — 인접 9셀만 탐색
		FVector SeparationVec = FVector::ZeroVector;
		const int32 CX = FMath::FloorToInt(EA.CurrentLoc.X * InvCellSize);
		const int32 CY = FMath::FloorToInt(EA.CurrentLoc.Y * InvCellSize);

		for (int32 dx = -1; dx <= 1; ++dx)
		{
			for (int32 dy = -1; dy <= 1; ++dy)
			{
				const int64 Key = (static_cast<int64>(CX + dx) << 32)
					| static_cast<int64>(static_cast<uint32>(CY + dy));
				if (const auto* Cell = Grid.Find(Key))
				{
					for (int32 B : *Cell)
					{
						if (A == B) continue;

						const FEntityData& EB = Entities[B];
						const float MinDist = EA.SeparationRadius + EB.SeparationRadius;

						FVector Diff = EA.CurrentLoc - EB.CurrentLoc;
						Diff.Z = 0.f;  // XY 평면에서만 분리

						const float DistSq = Diff.SizeSquared();
						if (DistSq < MinDist * MinDist && DistSq > KINDA_SMALL_NUMBER)
						{
							const float Dist    = FMath::Sqrt(DistSq);
							const float Overlap = (MinDist - Dist) / MinDist;
							SeparationVec += (Diff / Dist) * Overlap;
						}
					}
				}
			}
		}

		// 최종 위치
		const float SepSpeed = EA.MoveSpeed * 1.5f;
		NewLocations[A] = EA.CurrentLoc
			+ MoveDir * EA.MoveSpeed * DeltaTime
			+ SeparationVec.GetSafeNormal() * SepSpeed * DeltaTime;
	}

	// 성능 측정 로그 (300프레임마다)
	{
		const double SepEndTime = FPlatformTime::Seconds();
		const double SepMs = (SepEndTime - SepStartTime) * 1000.0;
		static double AccumMs = 0.0;
		static int32 AccumCount = 0;
		static double PeakMs = 0.0;
		AccumMs += SepMs;
		AccumCount++;
		if (SepMs > PeakMs) PeakMs = SepMs;

		if (GFrameCounter % 300 == 0 && AccumCount > 0)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[fast][Movement] Entities=%d | Separation Avg=%.3fms Peak=%.3fms (GridCells=%d) | 구형O(N²)비교: %d회→%d셀탐색"),
				TotalEntities, AccumMs / AccumCount, PeakMs,
				Grid.Num(),
				TotalEntities * TotalEntities,
				TotalEntities * 9);
			AccumMs = 0.0;
			AccumCount = 0;
			PeakMs = 0.0;
		}
	}

	// ----------------------------------------------------------------
	// Step 3: 계산된 위치 + 회전을 Transform에 적용
	// ----------------------------------------------------------------
	for (int32 i = 0; i < TotalEntities; ++i)
	{
		FEntityData& ED = Entities[i];
		if (!ED.TransformPtr || !ED.DataPtr)
			continue;

		FTransform& T = ED.TransformPtr->GetMutableTransform();
		T.SetLocation(NewLocations[i]);

		// 실제 이동 벡터 계산 (이동 + 분리 합산 방향)
		const FVector MoveDelta = NewLocations[i] - ED.CurrentLoc;
		const float MoveDeltaSize = MoveDelta.Size2D();  // XY 크기

		// 의미 있는 이동이 있을 때만 회전 업데이트 (너무 작으면 떨림 방지)
		if (MoveDeltaSize > 0.1f)
		{
			const FVector FlatDelta = FVector(MoveDelta.X, MoveDelta.Y, 0.f).GetSafeNormal();

			// 이동 방향으로 회전 (Yaw만 변경, Roll/Pitch 유지)
			const FRotator NewRot = FlatDelta.Rotation();
			T.SetRotation(FQuat(FRotator(0.f, NewRot.Yaw, 0.f)));

			// Actor 전환 시 사용할 마지막 이동 방향 저장
			ED.DataPtr->LastMoveDirection = FlatDelta;
		}
	}
}
