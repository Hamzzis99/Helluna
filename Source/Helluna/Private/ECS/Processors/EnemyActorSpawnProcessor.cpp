/**
 * EnemyActorSpawnProcessor.cpp
 *
 * Phase 1 최적화 적용:
 * - 역변환 (Actor->Entity): HP/위치 보존 후 Destroy
 * - 거리별 Tick 빈도 조절: Near/Mid/Far 구간
 * - Soft Cap: MaxConcurrentActors 초과 시 먼 Actor부터 Destroy
 * - HP 복원: Entity->Actor 재스폰 시 이전 HP 복원
 */

// File: Source/Helluna/Private/ECS/Processors/EnemyActorSpawnProcessor.cpp

#include "ECS/Processors/EnemyActorSpawnProcessor.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "ECS/Fragments/EnemyMassFragments.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"

DEFINE_LOG_CATEGORY_STATIC(LogECSEnemy, Log, All);

// ============================================================================
// 생성자
// ============================================================================
UEnemyActorSpawnProcessor::UEnemyActorSpawnProcessor()
{
	ExecutionFlags = (uint8)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
	bRequiresGameThreadExecution = true;
	RegisterQuery(EntityQuery);
}

// ============================================================================
// ConfigureQueries
// ============================================================================
void UEnemyActorSpawnProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.RegisterWithProcessor(*this);

	// FTransformFragment: ReadWrite (역변환 시 위치 갱신)
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);

	// FEnemySpawnStateFragment: ReadWrite (bHasSpawnedActor 갱신)
	EntityQuery.AddRequirement<FEnemySpawnStateFragment>(EMassFragmentAccess::ReadWrite);

	// FEnemyDataFragment: ReadWrite (역변환 시 HP 저장)
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
// 헬퍼: Actor->Entity 역변환
// HP와 위치를 Fragment에 보존한 후 Controller+Actor를 파괴한다.
// ============================================================================
void UEnemyActorSpawnProcessor::DespawnActorToEntity(
	FEnemySpawnStateFragment& SpawnState,
	FEnemyDataFragment& Data,
	FTransformFragment& Transform,
	AActor* Actor)
{
	// 1. HP 보존
	if (UHellunaHealthComponent* HC = Actor->FindComponentByClass<UHellunaHealthComponent>())
	{
		Data.CurrentHP = HC->GetHealth();
		Data.MaxHP = HC->GetMaxHealth();
	}

	// 2. 위치 보존 (Fragment에 현재 Actor 위치 기록)
	Transform.SetTransform(Actor->GetActorTransform());

	// 3. AI Controller 정리 (고아 Controller 방지)
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			Controller->UnPossess();
			Controller->Destroy();
		}
	}

	// 4. Actor 파괴
	Actor->Destroy();

	// 5. 상태 초기화
	SpawnState.bHasSpawnedActor = false;
	SpawnState.SpawnedActor = nullptr;

	UE_LOG(LogECSEnemy, Log,
		TEXT("[Despawn] Actor->Entity 복귀! HP: %.1f/%.1f, 위치: %s"),
		Data.CurrentHP, Data.MaxHP,
		*Transform.GetTransform().GetLocation().ToString());
}

// ============================================================================
// 헬퍼: 거리별 Tick 빈도 조절
// Actor와 AIController의 TickInterval을 동시에 변경한다.
// ============================================================================
void UEnemyActorSpawnProcessor::UpdateActorTickRate(
	AActor* Actor,
	float Distance,
	const FEnemyDataFragment& Data)
{
	float TickInterval;
	if (Distance < Data.NearDistance)
	{
		TickInterval = Data.NearTickInterval;   // 근거리: 매 틱
	}
	else if (Distance < Data.MidDistance)
	{
		TickInterval = Data.MidTickInterval;    // 중거리: ~12Hz
	}
	else
	{
		TickInterval = Data.FarTickInterval;    // 원거리: ~4Hz
	}

	Actor->SetActorTickInterval(TickInterval);

	// AIController의 Tick도 함께 조절
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetActorTickInterval(TickInterval);
		}
	}
}

// ============================================================================
// 헬퍼: Entity->Actor 스폰
// HP 복원 포함. AutoPossessAI가 자동으로 AI/GAS 초기화 처리.
// ============================================================================
bool UEnemyActorSpawnProcessor::TrySpawnActor(
	FEnemySpawnStateFragment& SpawnState,
	FEnemyDataFragment& Data,
	const FTransformFragment& Transform,
	UWorld* World)
{
	if (!Data.EnemyClass)
	{
		UE_LOG(LogECSEnemy, Error, TEXT("[Spawn] EnemyClass가 null! Trait에서 설정하세요."));
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FTransform SpawnTransform = Transform.GetTransform();

	AHellunaEnemyCharacter* SpawnedActor = Cast<AHellunaEnemyCharacter>(
		World->SpawnActor(Data.EnemyClass, &SpawnTransform, SpawnParams));

	if (!SpawnedActor)
	{
		UE_LOG(LogECSEnemy, Error,
			TEXT("[Spawn] Actor 스폰 실패! 클래스: %s, 위치: %s"),
			*Data.EnemyClass->GetName(),
			*SpawnTransform.GetLocation().ToString());
		return false;
	}

	// HP 복원 (이전에 역변환으로 저장된 HP가 있으면)
	if (Data.CurrentHP > 0.f)
	{
		if (UHellunaHealthComponent* HC = SpawnedActor->FindComponentByClass<UHellunaHealthComponent>())
		{
			HC->SetHealth(Data.CurrentHP);
		}
	}

	// 전환 완료 표시
	SpawnState.bHasSpawnedActor = true;
	SpawnState.SpawnedActor = SpawnedActor;

	UE_LOG(LogECSEnemy, Log,
		TEXT("===== Actor 스폰 성공! 클래스: %s, 위치: %s, HP: %.1f, bReplicates: %s ====="),
		*SpawnedActor->GetClass()->GetName(),
		*SpawnTransform.GetLocation().ToString(),
		Data.CurrentHP > 0.f ? Data.CurrentHP : Data.MaxHP,
		SpawnedActor->GetIsReplicated() ? TEXT("true") : TEXT("false"));

	return true;
}

// ============================================================================
// Execute: 매 틱 메인 로직
// ============================================================================
void UEnemyActorSpawnProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	// ------------------------------------------------------------------
	// Step 0: World 검증
	// ------------------------------------------------------------------
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		return;
	}

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

	if (PlayerLocations.IsEmpty())
	{
		return;
	}

	// ------------------------------------------------------------------
	// Step 2: 엔티티 순회 준비
	// ------------------------------------------------------------------

	// 청크 간 공유 카운터 (람다에서 참조 캡처)
	int32 ActiveActorCount = 0;
	int32 MaxConcurrentActorsValue = 50;

	// Soft Cap용: 스폰된 Actor 정보 수집
	struct FSoftCapEntry
	{
		TWeakObjectPtr<AActor> Actor;
		float DistSq;
	};
	TArray<FSoftCapEntry> SoftCapCandidates;

	// ------------------------------------------------------------------
	// Step 3: ForEachEntityChunk - 스폰/디스폰/틱 처리
	// ------------------------------------------------------------------
	EntityQuery.ForEachEntityChunk(Context,
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

				// MaxConcurrentActors는 모든 엔티티가 동일 (같은 EntityConfig)
				MaxConcurrentActorsValue = Data.MaxConcurrentActors;

				// ================================================
				// A) 이미 Actor로 전환된 엔티티 처리
				// ================================================
				if (SpawnState.bHasSpawnedActor)
				{
					AActor* Actor = SpawnState.SpawnedActor.Get();

					// A-1) Actor가 이미 파괴됨 (전투 사망 등) -> 상태 정리
					if (!IsValid(Actor))
					{
						SpawnState.bHasSpawnedActor = false;
						SpawnState.SpawnedActor = nullptr;
						Data.CurrentHP = -1.f;
						continue;
					}

					const float MinDistSq = CalcMinDistSq(Actor->GetActorLocation(), PlayerLocations);
					const float DespawnSq = Data.DespawnThreshold * Data.DespawnThreshold;

					// A-2) 거리 > DespawnThreshold -> 역변환 (HP/위치 보존 후 Destroy)
					if (MinDistSq > DespawnSq)
					{
						DespawnActorToEntity(SpawnState, Data, Transform, Actor);
						continue;
					}

					// A-3) 범위 내 -> Tick 빈도 조절
					const float ActualDist = FMath::Sqrt(MinDistSq);
					UpdateActorTickRate(Actor, ActualDist, Data);

					// 활성 Actor 카운트 증가
					ActiveActorCount++;

					// Soft Cap 후보 수집
					SoftCapCandidates.Add({SpawnState.SpawnedActor, MinDistSq});
				}
				// ================================================
				// B) 아직 Actor로 전환되지 않은 엔티티 처리
				// ================================================
				else
				{
					const FVector EntityLocation = Transform.GetTransform().GetLocation();
					const float MinDistSq = CalcMinDistSq(EntityLocation, PlayerLocations);
					const float SpawnSq = Data.SpawnThreshold * Data.SpawnThreshold;

					// B-1) 거리 < SpawnThreshold && Actor 수 미초과 -> 스폰
					if (MinDistSq < SpawnSq && ActiveActorCount < MaxConcurrentActorsValue)
					{
						if (TrySpawnActor(SpawnState, Data, Transform, World))
						{
							ActiveActorCount++;
						}
					}
				}
			}
		}
	);

	// ------------------------------------------------------------------
	// Step 4: Soft Cap (30프레임마다)
	// 활성 Actor가 MaxConcurrentActors를 초과하면 가장 먼 Actor부터 Destroy.
	// Fragment 상태는 다음 틱에서 A-1(파괴된 Actor 정리)로 자동 정리된다.
	// ------------------------------------------------------------------
	static uint64 LastSoftCapFrame = 0;
	const uint64 CurrentFrame = GFrameCounter;

	if (CurrentFrame - LastSoftCapFrame >= 30 && ActiveActorCount > MaxConcurrentActorsValue)
	{
		LastSoftCapFrame = CurrentFrame;

		// 거리 내림차순 정렬 (가장 먼 Actor가 앞에)
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
			{
				continue;
			}

			// Controller 정리 후 Actor 파괴
			if (APawn* Pawn = Cast<APawn>(Actor))
			{
				if (AController* Controller = Pawn->GetController())
				{
					Controller->UnPossess();
					Controller->Destroy();
				}
			}
			Actor->Destroy();
			Removed++;
		}

		UE_LOG(LogECSEnemy, Warning,
			TEXT("[SoftCap] Actor %d마리 -> %d마리로 축소 (%d마리 제거)"),
			ActiveActorCount, ActiveActorCount - Removed, Removed);
	}

	// 디버그 로그 (300프레임마다)
	static uint64 LastDebugFrame = 0;
	if (CurrentFrame - LastDebugFrame >= 300)
	{
		LastDebugFrame = CurrentFrame;
		UE_LOG(LogECSEnemy, Log,
			TEXT("[Status] 활성 Actor: %d/%d, 플레이어: %d명"),
			ActiveActorCount, MaxConcurrentActorsValue, PlayerLocations.Num());
	}
}
