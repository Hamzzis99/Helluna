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
#include "DebugHelper.h"

#include "EngineUtils.h"
#include "Engine/World.h"

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
	// 60프레임마다 갱신 (우주선이 움직일 수도 있으므로)
	// ----------------------------------------------------------------
	if (GFrameCounter % 60 == 0)
	{
		UWorld* World = Context.GetWorld();
		if (World)
		{
			GoalCacheQuery.ForEachEntityChunk(Context,
				[&](FMassExecutionContext& ChunkCtx)
				{
					const TArrayView<FEnemyDataFragment> DataList =
						ChunkCtx.GetMutableFragmentView<FEnemyDataFragment>();

					for (int32 i = 0; i < ChunkCtx.GetNumEntities(); ++i)
					{
						FEnemyDataFragment& Data = DataList[i];
						if (Data.bGoalLocationCached)
							continue;

						for (TActorIterator<AActor> It(World); It; ++It)
						{
							if (It->ActorHasTag(Data.GoalActorTag))
							{
								Data.GoalLocation = It->GetActorLocation();
								Data.bGoalLocationCached = true;

								break;
							}
						}
					}
				}	
			);
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

	// ----------------------------------------------------------------
	// Step 2: 이동 + 분리 계산 → NewLoc 배열 생성
	// ----------------------------------------------------------------
	TArray<FVector> NewLocations;
	NewLocations.SetNum(TotalEntities);

	for (int32 A = 0; A < TotalEntities; ++A)
	{
		const FEntityData& EA = Entities[A];

		// 이동 벡터
		FVector MoveDir = FVector::ZeroVector;
		if (FVector::DistSquared(EA.CurrentLoc, EA.GoalLoc) > 50.f * 50.f)
			MoveDir = (EA.GoalLoc - EA.CurrentLoc).GetSafeNormal();

		// 분리 벡터
		FVector SeparationVec = FVector::ZeroVector;
		for (int32 B = 0; B < TotalEntities; ++B)
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
				const float Overlap = (MinDist - Dist) / MinDist;  // 겹친 비율 0~1
				SeparationVec += (Diff / Dist) * Overlap;
			}
		}

		// 최종 위치
		const float SepSpeed = EA.MoveSpeed * 1.5f;
		NewLocations[A] = EA.CurrentLoc
			+ MoveDir * EA.MoveSpeed * DeltaTime
			+ SeparationVec.GetSafeNormal() * SepSpeed * DeltaTime;
				

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
	