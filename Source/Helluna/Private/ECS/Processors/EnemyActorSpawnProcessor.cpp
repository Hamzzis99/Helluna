/**
 * EnemyActorSpawnProcessor.cpp
 *
 * 하이브리드 ECS 핵심 Processor 구현체 (Phase 1 + Phase 2 최적화).
 *
 * ■ 실행 분리
 *   - if (!bIsClient): 서버/Standalone에서만 Actor 스폰/디스폰 수행
 *   - 시각화 루프: 서버/클라이언트 공통 (ExecutionFlags::All)
 *
 * ■ 디버깅 팁
 *   - LogECSEnemy 카테고리: 모든 스폰/디스폰/Soft Cap/상태 로그
 *   - 300프레임마다 자동 상태 로그 출력
 *   - 파일 하단 [디버깅 가이드] 참조
 */

#include "ECS/Processors/EnemyActorSpawnProcessor.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "ECS/Fragments/EnemyMassFragments.h"
#include "ECS/Pool/EnemyActorPool.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DebugHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogECSEnemy, Log, All);

// ============================================================================
// 생성자
// ============================================================================
UEnemyActorSpawnProcessor::UEnemyActorSpawnProcessor()
{
	// 서버/Standalone/클라이언트 모두 실행
	// - 스폰/디스폰: if(!bIsClient) 블록에서 서버만 처리
	// - 시각화: 공통 블록에서 모든 환경 처리
	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Standalone |
		EProcessorExecutionFlags::Client
	);

	bRequiresGameThreadExecution = true;
	RegisterQuery(EntityQuery);
}

// ============================================================================
// ConfigureQueries
// ============================================================================
void UEnemyActorSpawnProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);

	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEnemySpawnStateFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FEnemyDataFragment>(EMassFragmentAccess::ReadWrite);

	UE_LOG(LogECSEnemy, Log, TEXT("[EnemyActorSpawnProcessor] ConfigureQueries 완료"));
}

// ============================================================================
// 헬퍼: 최소 제곱 거리 계산
// ============================================================================
float UEnemyActorSpawnProcessor::CalcMinDistSq(
	const FVector& Location,
	const TArray<FVector>& PlayerLocations)
{
	float MinDistSq = MAX_FLT;
	for (const FVector& PlayerLoc : PlayerLocations)
	{
		const float DistSq = FVector::DistSquared(Location, PlayerLoc);
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
		}
	}
	return MinDistSq;
}

// ============================================================================
// 헬퍼: Actor → Entity 역변환
// ============================================================================
void UEnemyActorSpawnProcessor::DespawnActorToEntity(
	FEnemySpawnStateFragment& SpawnState,
	FEnemyDataFragment& Data,
	FTransformFragment& Transform,
	AActor* Actor,
	UEnemyActorPool* Pool)
{
	if (UHellunaHealthComponent* HC = Actor->FindComponentByClass<UHellunaHealthComponent>())
	{
		Data.CurrentHP = HC->GetHealth();
		Data.MaxHP = HC->GetMaxHealth();
	}

	Transform.GetMutableTransform() = Actor->GetActorTransform();
	Pool->DeactivateActor(Cast<AHellunaEnemyCharacter>(Actor));

	SpawnState.bHasSpawnedActor = false;
	SpawnState.SpawnedActor = nullptr;

	UE_LOG(LogECSEnemy, Verbose,
		TEXT("[Despawn] Actor->Entity 복귀 (Pool 반납)! HP: %.1f/%.1f, 위치: %s"),
		Data.CurrentHP, Data.MaxHP,
		*Transform.GetTransform().GetLocation().ToString());
}

// ============================================================================
// 헬퍼: 거리별 Tick 빈도 조절
// ============================================================================
void UEnemyActorSpawnProcessor::UpdateActorTickRate(
	AActor* Actor,
	float Distance,
	const FEnemyDataFragment& Data)
{
	float TickInterval;
	if (Distance < Data.NearDistance)
	{
		TickInterval = Data.NearTickInterval;
	}
	else if (Distance < Data.MidDistance)
	{
		TickInterval = Data.MidTickInterval;
	}
	else
	{
		TickInterval = Data.FarTickInterval;
	}

	Actor->SetActorTickInterval(TickInterval);

	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetActorTickInterval(TickInterval);
		}
	}

	if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Actor))
	{
		Enemy->UpdateAnimationLOD(Distance);
	}
}

// ============================================================================
// 헬퍼: Entity->Actor 스폰 (Pool에서 꺼내기)
// ============================================================================
bool UEnemyActorSpawnProcessor::TrySpawnActor(
	FEnemySpawnStateFragment& SpawnState,
	FEnemyDataFragment& Data,
	const FTransformFragment& Transform,
	UEnemyActorPool* Pool)
{
	if (!Data.EnemyClass)
	{
		UE_LOG(LogECSEnemy, Error, TEXT("[Spawn] EnemyClass가 null! Trait에서 설정하세요."));
		return false;
	}

	const FTransform SpawnTransform = Transform.GetTransform();

	AHellunaEnemyCharacter* SpawnedActor = Pool->ActivateActor(
		SpawnTransform, Data.CurrentHP, Data.MaxHP);

	if (!SpawnedActor)
	{
		UE_LOG(LogECSEnemy, Warning,
			TEXT("[Spawn] Pool 소진! Active: %d, Inactive: %d"),
			Pool->GetActiveCount(), Pool->GetInactiveCount());
		return false;
	}

	// Entity 상태의 마지막 이동 방향을 Actor 초기 회전에 적용
	if (!Data.LastMoveDirection.IsNearlyZero())
	{
		const FRotator LastRot = Data.LastMoveDirection.Rotation();
		SpawnedActor->SetActorRotation(FRotator(0.f, LastRot.Yaw, 0.f));
	}

	SpawnState.bHasSpawnedActor = true;
	SpawnState.SpawnedActor = SpawnedActor;

	UE_LOG(LogECSEnemy, Verbose,
		TEXT("[Spawn] Actor 활성화 성공 (Pool). 클래스: %s, 위치: %s, HP: %.1f"),
		*SpawnedActor->GetClass()->GetName(),
		*SpawnTransform.GetLocation().ToString(),
		Data.CurrentHP > 0.f ? Data.CurrentHP : Data.MaxHP);

	return true;
}

// ============================================================================
// 시각화 헬퍼: Root Actor 보장
// ============================================================================
void UEnemyActorSpawnProcessor::EnsureVisualizationRoot(UWorld* World)
{
	if (EntityVisualizationRoot)
		return;

	EntityVisualizationRoot = World->SpawnActor<AActor>();
	if (!EntityVisualizationRoot)
		return;

	EntityVisualizationRoot->SetActorLabel(TEXT("EntityVisualization_Root"));
	EntityVisualizationRoot->SetActorHiddenInGame(false);
	EntityVisualizationRoot->SetReplicates(false);

	// ISMC Attach 대상 SceneComponent 생성
	EntityVisualizationRootComp = NewObject<USceneComponent>(EntityVisualizationRoot);
	EntityVisualizationRootComp->SetMobility(EComponentMobility::Movable);
	EntityVisualizationRoot->SetRootComponent(EntityVisualizationRootComp);
	EntityVisualizationRootComp->RegisterComponent();
	EntityVisualizationRoot->AddInstanceComponent(EntityVisualizationRootComp);
	
	UE_LOG(LogECSEnemy, Log,
		TEXT("[Visualization] Root Actor 생성 (NetMode: %d)"),
		(int32)World->GetNetMode());
}

// ============================================================================
// 시각화 헬퍼: Mesh별 ISMC 반환 (없으면 생성 후 Attach)
// ============================================================================
UInstancedStaticMeshComponent* UEnemyActorSpawnProcessor::GetOrCreateISMC(UStaticMesh* Mesh)
{
	if (!Mesh || !EntityVisualizationRoot)
		return nullptr;

	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = MeshToISMC.Find(Mesh))
	{
		return Found->Get();
	}

	UInstancedStaticMeshComponent* ISMC = NewObject<UInstancedStaticMeshComponent>(EntityVisualizationRoot);
	ISMC->SetStaticMesh(Mesh);
	
	//
	ISMC->SetMobility(EComponentMobility::Movable);
	ISMC->SetVisibility(true, true);
	ISMC->SetHiddenInGame(false);
	ISMC->bNeverDistanceCull = true;
	//
	ISMC->SetCastShadow(false);
	ISMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISMC->SetIsReplicated(false);

	ISMC->SetupAttachment(EntityVisualizationRootComp);

	// ✅ Register는 설정 다 하고 나서
	ISMC->RegisterComponent();
	EntityVisualizationRoot->AddInstanceComponent(ISMC);
	
	MeshToISMC.Add(Mesh, ISMC);
	MeshToInstanceEntities.FindOrAdd(Mesh);

	UE_LOG(LogECSEnemy, Log,
		TEXT("[Visualization] ISMC 생성 - Mesh: %s (NetMode: %d)"),
		*Mesh->GetName(),
		(int32)GetWorld()->GetNetMode());

	return ISMC;
}

// ============================================================================
// Execute: 매 틱 메인 로직
// ============================================================================
void UEnemyActorSpawnProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World)
		return;

	const bool bIsClient = (World->GetNetMode() == NM_Client);
	const uint64 CurrentFrame = GFrameCounter;

	// =========================================================
	// ✅ 서버/Standalone 전용: Actor 스폰/디스폰 로직
	//  - 여기서 "return" 해버리면 아래 시각화가 안 도니까,
	//    서버 로직은 "필요하면 skip" 하고, 함수는 끝까지 내려가게 만든다.
	// =========================================================
	if (!bIsClient)
	{
		UEnemyActorPool* Pool = World->GetSubsystem<UEnemyActorPool>();
		if (!Pool)
		{
			// 서버에서 풀 없으면 서버 로직만 스킵
			Debug::Print(TEXT("[EnemyProc] Pool missing - skip server actor logic"), FColor::Red);
		}
		else
		{
			// ------------------------------------------------------------------
			// Step 0.5: Pool 초기화 (첫 틱만)
			//  - 여기서도 return 하지 말고, 서버 로직만 "이번 프레임 스킵"한다.
			// ------------------------------------------------------------------
			if (!Pool->IsPoolInitialized())
			{
				EntityQuery.ForEachEntityChunk(EntityManager, Context,
					[&](FMassExecutionContext& ChunkCtx)
					{
						if (Pool->IsPoolInitialized()) return;

						auto DataList = ChunkCtx.GetFragmentView<FEnemyDataFragment>();
						if (DataList.Num() > 0)
						{
							const FEnemyDataFragment& Data = DataList[0];
							if (Data.EnemyClass)
							{
								Pool->InitializePool(Data.EnemyClass, Data.PoolSize);
								Debug::Print(TEXT("[EnemyProc] Pool initialized"), FColor::Green);
							}
						}
					});

				// ✅ Pool 초기화 프레임에는 Actor 스폰/디스폰만 스킵
				// (아래 시각화는 계속 돌아야 함)
			}
			else
			{
				// ------------------------------------------------------------------
				// Step 1: 플레이어 위치 수집
				// ------------------------------------------------------------------
				TArray<FVector> PlayerLocations;
				PlayerLocations.Reserve(4);

				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					if (APlayerController* PC = It->Get())
					{
						if (APawn* Pawn = PC->GetPawn())
						{
							PlayerLocations.Add(Pawn->GetActorLocation());
						}
					}
				}

				// 플레이어가 없으면 서버 로직 스킵 (시각화는 아래에서 계속)
				if (!PlayerLocations.IsEmpty())
				{
					// ------------------------------------------------------------------
					// Step 1.5: Pool 유지보수 (60프레임마다)
					// ------------------------------------------------------------------
					static uint64 LastReplenishFrame = 0;
					if (CurrentFrame - LastReplenishFrame >= 60)
					{
						LastReplenishFrame = CurrentFrame;
						Pool->CleanupAndReplenish();
					}

					// ------------------------------------------------------------------
					// Step 2: 엔티티 순회 준비
					// ------------------------------------------------------------------
					int32 ActiveActorCount = 0;
					int32 MaxConcurrentActorsValue = 50;

					struct FSoftCapEntry
					{
						TWeakObjectPtr<AActor> Actor;
						float DistSq;
						FEnemySpawnStateFragment* SpawnStatePtr;
						FEnemyDataFragment* DataPtr;
						FTransformFragment* TransformPtr;
					};

					TArray<FSoftCapEntry> SoftCapCandidates;

					// ------------------------------------------------------------------
					// Step 3: ForEachEntityChunk - 스폰/디스폰/틱 처리
					// ------------------------------------------------------------------
					EntityQuery.ForEachEntityChunk(EntityManager, Context,
						[&](FMassExecutionContext& ChunkCtx)
						{
							auto TransformList = ChunkCtx.GetMutableFragmentView<FTransformFragment>();
							auto SpawnStateList = ChunkCtx.GetMutableFragmentView<FEnemySpawnStateFragment>();
							auto DataList = ChunkCtx.GetMutableFragmentView<FEnemyDataFragment>();
							const int32 NumEntities = ChunkCtx.GetNumEntities();

							for (int32 i = 0; i < NumEntities; ++i)
							{
								FEnemySpawnStateFragment& SpawnState = SpawnStateList[i];
								FEnemyDataFragment& Data = DataList[i];
								FTransformFragment& Transform = TransformList[i];

								MaxConcurrentActorsValue = Data.MaxConcurrentActors;

								if (SpawnState.bHasSpawnedActor)
								{
									AActor* Actor = SpawnState.SpawnedActor.Get();

									if (!IsValid(Actor))
									{
										SpawnState.bHasSpawnedActor = false;
										SpawnState.SpawnedActor = nullptr;
										SpawnState.bDead = true;
										Data.CurrentHP = -1.f;
										continue;
									}

									const float MinDistSq = CalcMinDistSq(Actor->GetActorLocation(), PlayerLocations);
									const float DespawnSq = Data.DespawnThreshold * Data.DespawnThreshold;

									if (MinDistSq > DespawnSq)
									{
										DespawnActorToEntity(SpawnState, Data, Transform, Actor, Pool);
										continue;
									}

									const float ActualDist = FMath::Sqrt(MinDistSq);
									UpdateActorTickRate(Actor, ActualDist, Data);

									ActiveActorCount++;
									SoftCapCandidates.Add({
										SpawnState.SpawnedActor, MinDistSq,
										&SpawnState, &Data, &Transform
									});
								}
								else
								{
									if (SpawnState.bDead)
									{
										continue;
									}

									const FVector EntityLocation = Transform.GetTransform().GetLocation();
									const float MinDistSq = CalcMinDistSq(EntityLocation, PlayerLocations);
									const float SpawnSq = Data.SpawnThreshold * Data.SpawnThreshold;

									// 플레이어 거리 OR 우주선 거리 중 하나라도 SpawnThreshold 이내면 스폰
									const float GoalDistSq = Data.bGoalLocationCached
										? FVector::DistSquared(EntityLocation, Data.GoalLocation)
										: MAX_FLT;

									const bool bNearPlayer = MinDistSq < SpawnSq;
									const bool bNearGoal   = GoalDistSq < SpawnSq;

									if ((bNearPlayer || bNearGoal) && ActiveActorCount < MaxConcurrentActorsValue)
									{
										if (TrySpawnActor(SpawnState, Data, Transform, Pool))
										{
											ActiveActorCount++;
										}
									}
								}
							}
						}
					);

					// ------------------------------------------------------------------
					// Step 4: Soft Cap 관리 (30프레임마다)
					// ------------------------------------------------------------------
					static uint64 LastSoftCapFrame = 0;
					if (CurrentFrame - LastSoftCapFrame >= 30 && ActiveActorCount > MaxConcurrentActorsValue)
					{
						LastSoftCapFrame = CurrentFrame;

						SoftCapCandidates.Sort([](const FSoftCapEntry& A, const FSoftCapEntry& B)
						{
							return A.DistSq > B.DistSq;
						});

						const int32 ToRemove = ActiveActorCount - MaxConcurrentActorsValue;
						int32 Removed = 0;

						for (int32 k = 0; k < ToRemove && k < SoftCapCandidates.Num(); ++k)
						{
							AActor* Actor = SoftCapCandidates[k].Actor.Get();
							if (!IsValid(Actor))
								continue;

							DespawnActorToEntity(
								*SoftCapCandidates[k].SpawnStatePtr,
								*SoftCapCandidates[k].DataPtr,
								*SoftCapCandidates[k].TransformPtr,
								Actor, Pool);
							Removed++;
						}

						Debug::Print(
							FString::Printf(TEXT("[SoftCap] %d -> %d (Removed=%d)"),
								ActiveActorCount, ActiveActorCount - Removed, Removed),
							FColor::Yellow);
					}

					// 디버그(300프레임마다)
					static uint64 LastDebugFrame = 0;
					if (CurrentFrame - LastDebugFrame >= 300)
					{
						LastDebugFrame = CurrentFrame;
						Debug::Print(
							FString::Printf(TEXT("[ServerStatus] Active=%d/%d | Players=%d | Pool(A=%d I=%d)"),
								ActiveActorCount, MaxConcurrentActorsValue, PlayerLocations.Num(),
								Pool->GetActiveCount(), Pool->GetInactiveCount()),
							FColor::Cyan);
					}
				}
			}
		}
	}

	// =========================================================
	// ✅ 서버/클라이언트 공통: Entity 시각화 갱신
	// =========================================================
	EntityQuery.ForEachEntityChunk(EntityManager, Context,
		[&](FMassExecutionContext& ChunkContext)
		{
			const int32 NumEntities = ChunkContext.GetNumEntities();

			const TConstArrayView<FTransformFragment> TransformList =
				ChunkContext.GetFragmentView<FTransformFragment>();
			const TArrayView<FEnemySpawnStateFragment> SpawnStateList =
				ChunkContext.GetMutableFragmentView<FEnemySpawnStateFragment>();
			const TArrayView<FEnemyDataFragment> DataList =
				ChunkContext.GetMutableFragmentView<FEnemyDataFragment>();

			for (int32 i = 0; i < NumEntities; ++i)
			{
				const FMassEntityHandle Entity = ChunkContext.GetEntity(i);
				const FTransformFragment& Transform = TransformList[i];
				const FEnemySpawnStateFragment& SpawnState = SpawnStateList[i];
				const FEnemyDataFragment& Data = DataList[i];

				UpdateEntityVisualization(Entity, Transform, Data, SpawnState);
			}
		}
	);
}


// ============================================================================
// 시각화: Entity 상태에 따라 ISMC 인스턴스 추가/갱신/제거
// ============================================================================
void UEnemyActorSpawnProcessor::UpdateEntityVisualization(
	const FMassEntityHandle Entity,
	const FTransformFragment& Transform,
	const FEnemyDataFragment& Data,
	const FEnemySpawnStateFragment& SpawnState)
{
	const bool bShouldVisualize =
		!SpawnState.bHasSpawnedActor &&
		!SpawnState.bDead &&
		Data.bShowEntityVisualization &&
		Data.EntityVisualizationMesh != nullptr;

	FEntityInstanceRef* ExistingRef = EntityToInstanceRef.Find(Entity);

	if (!bShouldVisualize)
	{
		if (ExistingRef)
		{
			CleanupEntityVisualization(Entity);
		}
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
		return;

	EnsureVisualizationRoot(World);
	if (!EntityVisualizationRootComp)
		return;

	UStaticMesh* Mesh = Data.EntityVisualizationMesh;
	UInstancedStaticMeshComponent* ISMC = GetOrCreateISMC(Mesh);
	if (!ISMC)
		return;

	FTransform InstanceTransform = Transform.GetTransform();
	InstanceTransform.AddToTranslation(FVector(0.f, 0.f, Data.EntityMeshZOffset));
	InstanceTransform.SetScale3D(Data.EntityMeshScale);

	// 이미 존재하면 업데이트
	if (ExistingRef && ExistingRef->Mesh == Mesh && ExistingRef->Index != INDEX_NONE)
	{
		if (ExistingRef->Index < ISMC->GetInstanceCount())
		{
			ISMC->UpdateInstanceTransform(ExistingRef->Index, InstanceTransform, true, true, true);
			return;
		}
		// 인덱스가 깨졌으면 재생성
		CleanupEntityVisualization(Entity);
	}

	// 새 인스턴스 생성
	const int32 NewIndex = ISMC->AddInstance(InstanceTransform, true);

	FEntityInstanceRef NewRef;
	NewRef.Mesh = Mesh;
	NewRef.Index = NewIndex;
	EntityToInstanceRef.Add(Entity, NewRef);

	TArray<FMassEntityHandle>& InstanceEntities = MeshToInstanceEntities.FindOrAdd(Mesh);

	// 방어: NewIndex 위치까지 채우기
	while (InstanceEntities.Num() < NewIndex)
		InstanceEntities.Add(FMassEntityHandle());

	InstanceEntities.Add(Entity);
}

// ============================================================================
// 시각화: 인스턴스 제거 (RemoveInstance 스왑 방식에 맞춰 인덱스 매핑 갱신)
// ============================================================================
void UEnemyActorSpawnProcessor::CleanupEntityVisualization(const FMassEntityHandle Entity)
{
	FEntityInstanceRef* Ref = EntityToInstanceRef.Find(Entity);
	if (!Ref || !Ref->Mesh || Ref->Index == INDEX_NONE)
		return;

	UInstancedStaticMeshComponent* ISMC = nullptr;
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = MeshToISMC.Find(Ref->Mesh))
	{
		ISMC = Found->Get();
	}
	if (!ISMC)
	{
		EntityToInstanceRef.Remove(Entity);
		return;
	}

	TArray<FMassEntityHandle>* InstanceEntitiesPtr = MeshToInstanceEntities.Find(Ref->Mesh);
	if (!InstanceEntitiesPtr)
	{
		EntityToInstanceRef.Remove(Entity);
		return;
	}

	TArray<FMassEntityHandle>& InstanceEntities = *InstanceEntitiesPtr;
	const int32 RemoveIndex = Ref->Index;
	const int32 LastIndex = InstanceEntities.Num() - 1;

	if (RemoveIndex < 0 || RemoveIndex > LastIndex)
	{
		EntityToInstanceRef.Remove(Entity);
		return;
	}

	// RemoveInstance는 내부적으로 마지막↔제거 위치 스왑 → 매핑도 동일하게 처리
	if (RemoveIndex != LastIndex)
	{
		const FMassEntityHandle SwappedEntity = InstanceEntities[LastIndex];
		InstanceEntities[RemoveIndex] = SwappedEntity;

		if (FEntityInstanceRef* SwappedRef = EntityToInstanceRef.Find(SwappedEntity))
		{
			SwappedRef->Index = RemoveIndex;
		}
	}

	InstanceEntities.Pop();
	ISMC->RemoveInstance(RemoveIndex);

	EntityToInstanceRef.Remove(Entity);
}

// ============================================================================
// [디버깅 가이드]
// ============================================================================
//
// ■ 증상: 멀티에서 클라이언트에 메시 안 보임
//   1. Output Log에서 "[Visualization] Root Actor 생성 (NetMode: 1)" 확인
//      → 없으면 ExecutionFlags::Client 미적용 또는 Entity가 클라에 없음
//   2. "[Visualization] ISMC 생성" 로그 확인
//      → 없으면 bShowEntityVisualization=false 또는 EntityVisualizationMesh=null
//
// ■ 증상: 적이 가까이 와도 Actor로 전환되지 않음
//   1. PlayerLocations가 비어있지 않은지 확인
//   2. SpawnThreshold 값 확인 (기본 5000cm = 50m)
//   3. MaxConcurrentActors 초과 여부 확인
//   4. Pool에 InactiveActor가 있는지 확인
//   5. bDead가 true인지 확인
//
// ■ 증상: 적이 멀어져도 Entity로 돌아가지 않음
//   1. DespawnThreshold 값 확인 (기본 6000cm = 60m)
//   2. DespawnThreshold > SpawnThreshold인지 확인
//
// ■ 로그 확인 명령어
//   콘솔: Log LogECSEnemy Log        → Processor 이벤트
//   콘솔: Log LogECSEnemy Verbose    → 상세 이벤트
//   콘솔: stat unit                  → Game Thread 시간 확인
//   콘솔: stat game                  → 전체 게임 성능
// ============================================================================
