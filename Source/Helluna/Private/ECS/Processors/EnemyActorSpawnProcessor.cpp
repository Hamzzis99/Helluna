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

// ============================================================================
// [TODO] EnemyEntityMovementProcessor - Entity 상태에서 기지 방향 직선 이동
// ============================================================================
//
// 현재 Entity는 스폰 위치에서 가만히 대기하다가 플레이어가 접근하면 Actor로 전환된다.
// 추후 기지 방어 시스템 구현 시, Entity 상태에서도 기지를 향해 이동해야 한다.
//
// [구현 계획]
// - 별도 UMassProcessor 서브클래스: UEnemyEntityMovementProcessor
// - 매 틱: Entity Transform 위치를 기지 방향으로 이동
//   EntityLocation += (BaseLocation - EntityLocation).GetSafeNormal() * MoveSpeed * DeltaTime
// - NavMesh 불필요 (먼 거리에서는 직선 이동으로 충분)
// - Actor 전환 후에는 StateTree의 MoveTo가 NavMesh 기반 정밀 이동 담당
//
// [필요한 Fragment 추가]
// - FEnemyDataFragment에 추가:
//   float EntityMoveSpeed = 300.f;  // Entity 상태 이동 속도 (cm/s)
//   FVector TargetBaseLocation;     // 기지 위치 (GameMode에서 설정)
//
// [실행 흐름]
// Entity (먼 거리) → 직선 이동 (이 Processor) → 50m 이내 → Actor 전환 → NavMesh 이동 (StateTree)
//
// [주의사항]
// - bHasSpawnedActor == true인 Entity는 스킵 (이미 Actor가 이동 중)
// - FTransformFragment를 ReadWrite로 사용해야 함 (위치 갱신)
// - ProcessingPhase는 PrePhysics (EnemyActorSpawnProcessor보다 먼저 실행)
// - 장애물 충돌 없음 (Entity는 물리 없음). 기지 근처에서 Actor 전환 후 NavMesh가 처리
// ============================================================================

#include "ECS/Processors/EnemyActorSpawnProcessor.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "ECS/Fragments/EnemyMassFragments.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "MassAgentComponent.h"

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
// 헬퍼: Actor → Entity 역변환
// ============================================================================
// "죽인 게 아니라 잠시 꺼두는 것"
//
// Actor를 파괴하되, HP와 위치를 Fragment에 보존한다.
// 나중에 TrySpawnActor에서 이 데이터로 Actor를 재생성하면
// 플레이어 입장에서는 "적이 계속 거기 있었던 것"처럼 보인다.
//
// Controller도 함께 파괴하는 이유:
// AutoPossessAI로 생성된 AIController는 Actor와 별개 UObject이므로
// Actor만 Destroy하면 Controller가 고아 객체로 남아 메모리 누수 발생.
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
	Transform.GetMutableTransform() = Actor->GetActorTransform();

	// 3. MassAgentComponent 안전 제거 (클라이언트 리플리케이션 크래시 방지)
	// BP에 UMassAgentComponent가 붙어있으면 PuppetPendingReplication 상태에서
	// Destroy 시 클라이언트에서 assert 터짐. Destroy 전에 먼저 제거한다.
	if (UMassAgentComponent* MassAgent = Actor->FindComponentByClass<UMassAgentComponent>())
	{
		MassAgent->DestroyComponent();
	}

	// 4. AI Controller 정리 (고아 Controller 방지)
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			Controller->UnPossess();
			Controller->Destroy();
		}
	}

	// 5. Actor 파괴
	Actor->Destroy();

	// 6. 상태 초기화
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

	// HP 복원: 역변환(Soft Cap 등)으로 Entity가 된 적이 다시 Actor로 돌아올 때
	// 저장된 HP를 복원한다. CurrentHP == -1이면 처음 스폰이므로 건너뛴다.
	// (처음 스폰 시 HP는 HellunaHealthComponent의 기본값 사용)
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
	// ============================================================================
	// [핵심 개념] 하이브리드 ECS의 Actor ↔ Entity 전환 흐름
	// ============================================================================
	//
	// 이 게임은 기지 방어 게임이다. 밤에 적이 기지로 몰려온다.
	// 적은 처음에 Mass Entity(가벼운 데이터)로 생성되고,
	// 플레이어 근처에 왔을 때만 Actor(무거운 전체 기능)로 전환된다.
	//
	// [왜 역변환이 필요한가?]
	// Actor는 비싸다 (AI, 물리, 애니메이션, 네트워크). 100마리 전부 Actor면 30FPS.
	// 그래서 동시 Actor 수를 50마리(MaxConcurrentActors)로 제한하고,
	// 초과분은 Entity로 되돌린다 (역변환 = Despawn).
	//
	// [Soft Cap이 만드는 자연스러운 전투 흐름]
	// 100마리가 기지로 몰려올 때:
	//   - 가까운 50마리 → Actor (전투 가능)
	//   - 뒤쪽 50마리 → Entity (대기, 비용 거의 0)
	//   - 앞의 적이 죽으면 → Actor 슬롯이 빈다
	//   - 뒤의 Entity가 자연스럽게 Actor로 전환됨
	//   = 킬링플로어처럼 "다음 투입"이 코드 없이 자동 발생!
	//
	// [HP 복원이 필요한 이유]
	// Soft Cap에 의해 Actor→Entity 역변환된 적은 "죽은 게 아니다".
	// 예: 50마리 꽉 찬 상태에서 51번째 적이 Soft Cap으로 역변환됨
	//   → 앞의 적이 죽어서 슬롯이 빔
	//   → 51번째 적이 다시 Actor로 전환됨
	//   → 이때 HP가 풀로 차있으면 이상하다! (때리던 적이었을 수도 있으니까)
	//   → 그래서 역변환 시 HP를 Fragment에 저장하고, 재스폰 시 복원한다.
	//
	// [죽은 적 vs 역변환된 적]
	// - 죽은 적: bDead = true → 영구적으로 다시 스폰 안 함
	// - 역변환된 적: bDead = false, CurrentHP > 0 → 슬롯 나면 재스폰 (HP 유지)
	// ============================================================================

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

					// A-1) Actor가 외부에서 파괴됨 (전투 사망, 환경 데미지 등)
					// ※ 역변환(DespawnActorToEntity)은 bHasSpawnedActor=false로 설정하므로
					//    여기 도달하는 경우 = 우리 코드가 아닌 외부에서 Actor가 파괴된 것.
					//    즉 전투 중 사망 → bDead = true → 이 Entity는 영구 퇴장.
					//    다시는 Actor로 전환되지 않으며, 웨이브 전멸 카운트에 반영된다.
					if (!IsValid(Actor))
					{
						SpawnState.bHasSpawnedActor = false;
						SpawnState.SpawnedActor = nullptr;
						SpawnState.bDead = true;  // 죽은 적은 다시 스폰하지 않음!
						Data.CurrentHP = -1.f;
						continue;
					}

					const float MinDistSq = CalcMinDistSq(Actor->GetActorLocation(), PlayerLocations);
					const float DespawnSq = Data.DespawnThreshold * Data.DespawnThreshold;

					// A-2) 거리 > DespawnThreshold → 역변환 (Actor→Entity 복귀)
					// 적이 죽은 게 아니라, 플레이어에게서 멀어졌거나 Soft Cap 대상이 된 것.
					// HP와 위치를 Fragment에 저장한 뒤 Actor를 파괴한다.
					// 나중에 플레이어가 다시 접근하거나 Soft Cap 슬롯이 나면
					// 저장된 HP로 Actor가 재생성된다 (풀피로 부활하는 게 아님!).
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
					// 죽은 적은 다시 스폰하지 않음
					if (SpawnState.bDead)
					{
						continue;
					}

					const FVector EntityLocation = Transform.GetTransform().GetLocation();
					const float MinDistSq = CalcMinDistSq(EntityLocation, PlayerLocations);
					const float SpawnSq = Data.SpawnThreshold * Data.SpawnThreshold;

					// B-1) 거리 < SpawnThreshold && Actor 슬롯 여유 있음 → Actor 스폰
					// 이 Entity는 두 가지 경우일 수 있다:
					//   a) 처음 스폰: CurrentHP == -1 → 풀 HP로 생성
					//   b) 역변환 후 재스폰: CurrentHP > 0 → 이전 HP로 복원
					// Soft Cap이 이 흐름을 자동 관리:
					//   앞의 적 사망 → ActiveActorCount 감소 → 여기서 다음 적이 스폰됨
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
	// Step 4: Soft Cap 관리 (30프레임마다)
	// ------------------------------------------------------------------
	// 활성 Actor가 MaxConcurrentActors(기본 50)를 초과하면 가장 먼 Actor부터 역변환.
	//
	// [왜 초과가 발생하는가?]
	// Step 3에서 스폰 시 ActiveActorCount < Max 체크를 하지만,
	// 같은 틱에 여러 Entity가 동시에 SpawnThreshold에 진입할 수 있다.
	// 또한 플레이어가 여러 명이면 각자 주변에 Actor가 생기므로 합산 초과 가능.
	//
	// [30프레임마다인 이유]
	// 매 틱 정렬+Destroy는 비싸다. 0.5초 주기면 체감 차이 없이 성능 절약.
	//
	// [역변환 순서: 가장 먼 Actor부터]
	// 플레이어에게서 가장 먼 Actor는 전투 참여 가능성이 낮으므로
	// 역변환해도 플레이어가 눈치채지 못한다.
	// Fragment 상태(bHasSpawnedActor)는 다음 틱 A-1에서 자동 정리.
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

			// MassAgentComponent 안전 제거 (클라이언트 크래시 방지)
			if (UMassAgentComponent* MassAgent = Actor->FindComponentByClass<UMassAgentComponent>())
			{
				MassAgent->DestroyComponent();
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
