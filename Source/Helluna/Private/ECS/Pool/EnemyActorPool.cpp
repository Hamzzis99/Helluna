/**
 * EnemyActorPool.cpp
 *
 * Actor Object Pooling 구현체.
 *
 * ■ [핵심 버그 수정] 단일 Pool → EnemyClass별 멀티 Pool
 *   증상: 근거리/원거리 두 스포너를 사용할 때, 딜레이가 빠른 스포너의 몬스터가 2마리 나옴.
 *   원인: UWorldSubsystem은 월드에 1개만 존재하는데, InitializePool이 처음 호출된
 *         EnemyClass로 Pool을 고정(bInitialized=true)하고 이후 호출은 무시함.
 *         → 원거리 Processor가 InitializePool(원거리Class)를 호출해도 무시됨
 *         → ActivateActor 시 Pool에는 근거리 Actor만 있어 근거리가 2마리 소환됨.
 *   수정: PerClassPools(TMap<EnemyClass, FActorPoolData>)로 클래스별 독립 Pool 유지.
 *         InitializePool은 클래스 키 기준으로 중복 여부를 판단하므로
 *         근거리/원거리가 각자 올바른 Pool에 등록되고 ActivateActor도 올바른 Pool에서 꺼냄.
 *
 * ■ 변경된 외부 인터페이스 (Processor 쪽 수정 필요)
 *   - IsPoolInitialized() → IsPoolInitialized(EnemyClass)
 *   - ActivateActor(Transform, HP, MaxHP) → ActivateActor(EnemyClass, Transform, HP, MaxHP)
 *   - GetActiveCount() → GetActiveCount(EnemyClass) 또는 GetTotalActiveCount()
 *   - GetInactiveCount() → GetInactiveCount(EnemyClass) 또는 GetTotalInactiveCount()
 *   - DeactivateActor(Actor): 변경 없음 (내부에서 Actor->GetClass()로 Pool 자동 탐색)
 *   - CleanupAndReplenish(): 변경 없음 (내부에서 모든 Pool 순회)
 */

#include "ECS/Pool/EnemyActorPool.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Components/StateTreeComponent.h"

#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogECSPool, Log, All);

/** Pool Actor 보관용 숨김 위치 (맵 아래 Z=-50000, 플레이어/물리/렌더링 범위 밖) */
const FVector UEnemyActorPool::PoolHiddenLocation = FVector(0.0, 0.0, -50000.0);

// ============================================================================
// ShouldCreateSubsystem
// ============================================================================
bool UEnemyActorPool::ShouldCreateSubsystem(UObject* Outer) const
{
	return true;
}

// ============================================================================
// Deinitialize — 월드 소멸 시 모든 EnemyClass의 Pool Actor 전부 파괴
// ============================================================================
void UEnemyActorPool::Deinitialize()
{
	// 모든 EnemyClass 별 Pool을 순회하여 Actor 파괴
	for (auto& Pair : PerClassPools)
	{
		FActorPoolData& PoolData = Pair.Value;

		// 비활성 Actor: Controller가 Possess 중이지만 StateTree는 Stop 상태.
		// Destroy 시 StopLogic이 재호출되어 Warning 방지를 위해 UnPossess 먼저 수행.
		for (AHellunaEnemyCharacter* Actor : PoolData.InactiveActors)
		{
			if (IsValid(Actor))
			{
				if (APawn* Pawn = Cast<APawn>(Actor))
				{
					if (AController* Controller = Pawn->GetController())
					{
						if (UStateTreeComponent* STComp = Controller->FindComponentByClass<UStateTreeComponent>())
						{
							STComp->SetComponentTickEnabled(false);
						}
						Controller->UnPossess();
					}
				}
				Actor->Destroy();
			}
		}

		// 활성 Actor는 그냥 파괴
		for (AHellunaEnemyCharacter* Actor : PoolData.ActiveActors)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}

		UE_LOG(LogECSPool, Log,
			TEXT("[Pool] Deinitialize — Class: %s | Inactive: %d, Active: %d 파괴 완료"),
			*Pair.Key->GetName(),
			PoolData.InactiveActors.Num(),
			PoolData.ActiveActors.Num());

		PoolData.InactiveActors.Empty();
		PoolData.ActiveActors.Empty();
	}

	PerClassPools.Empty();
}

// ============================================================================
// InitializePool — EnemyClass별 Pool 초기화
//
// [수정 포인트]
//   이전: bInitialized 단일 플래그 → 첫 호출 클래스로 고정, 이후 호출 무시
//   이후: PerClassPools[EnemyClass].bInitialized 로 클래스별 독립 판단
//         → 근거리/원거리 스포너가 각자 다른 클래스로 호출하면 둘 다 정상 초기화
// ============================================================================
void UEnemyActorPool::InitializePool(TSubclassOf<AHellunaEnemyCharacter> EnemyClass, int32 InPoolSize)
{
	if (!EnemyClass)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] InitializePool 실패! EnemyClass가 null."));
		return;
	}

	// 이미 이 클래스로 초기화된 Pool이 있으면 중복 호출 → 무시
	if (FActorPoolData* Existing = PerClassPools.Find(EnemyClass))
	{
		if (Existing->bInitialized)
		{
			UE_LOG(LogECSPool, Warning,
				TEXT("[Pool] 이미 초기화됨. 중복 호출 무시 — Class: %s"),
				*EnemyClass->GetName());
			return;
		}
	}

	// 이 클래스 전용 FActorPoolData를 TMap에 생성(없으면 기본값으로 추가)
	FActorPoolData& PoolData = PerClassPools.FindOrAdd(EnemyClass);
	PoolData.DesiredPoolSize = InPoolSize;
	PoolData.InactiveActors.Reserve(InPoolSize);

	int32 SuccessCount = 0;
	for (int32 i = 0; i < InPoolSize; ++i)
	{
		// 클래스를 명시적으로 전달하여 생성 — 이전엔 CachedEnemyClass 사용
		AHellunaEnemyCharacter* Actor = CreatePooledActor(EnemyClass);
		if (Actor)
		{
			PoolData.InactiveActors.Add(Actor);
			SuccessCount++;
		}
	}

	PoolData.bInitialized = true;

	UE_LOG(LogECSPool, Log,
		TEXT("[Pool] 초기화 완료! Class: %s | 요청: %d, 성공: %d, 실패: %d"),
		*EnemyClass->GetName(), InPoolSize, SuccessCount, InPoolSize - SuccessCount);
}

// ============================================================================
// IsPoolInitialized — 특정 EnemyClass의 Pool 초기화 여부
//
// [수정 포인트]
//   이전: bool IsPoolInitialized() const { return bInitialized; }  — 전역 단일 플래그
//   이후: EnemyClass 파라미터로 해당 클래스 Pool의 초기화 여부를 개별 조회
// ============================================================================
bool UEnemyActorPool::IsPoolInitialized(TSubclassOf<AHellunaEnemyCharacter> EnemyClass) const
{
	if (!EnemyClass)
	{
		return false;
	}

	const FActorPoolData* PoolData = PerClassPools.Find(EnemyClass);
	return PoolData && PoolData->bInitialized;
}

// ============================================================================
// ActivateActor — 지정 EnemyClass의 Pool에서 비활성 Actor를 꺼내 활성화
//
// [수정 포인트]
//   이전: InactiveActors (단일 배열) 에서 무조건 꺼냄
//         → Pool이 근거리로만 채워진 경우 원거리 요청도 근거리 Actor를 반환하는 버그
//   이후: PerClassPools[EnemyClass].InactiveActors 에서 해당 클래스만 꺼냄
//         → 근거리 요청 → 근거리 Pool, 원거리 요청 → 원거리 Pool 정확히 분리
// ============================================================================
AHellunaEnemyCharacter* UEnemyActorPool::ActivateActor(
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass,
	const FTransform& SpawnTransform,
	float CurrentHP,
	float MaxHP)
{
	if (!EnemyClass)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] ActivateActor 실패! EnemyClass가 null."));
		return nullptr;
	}

	// 해당 클래스의 Pool이 없거나 비어있으면 nullptr 반환
	FActorPoolData* PoolData = PerClassPools.Find(EnemyClass);
	if (!PoolData || PoolData->InactiveActors.IsEmpty())
	{
		UE_LOG(LogECSPool, Warning,
			TEXT("[Pool] 비활성 Actor 없음! Pool 소진 — Class: %s | Active: %d, DesiredSize: %d"),
			*EnemyClass->GetName(),
			PoolData ? PoolData->ActiveActors.Num() : 0,
			PoolData ? PoolData->DesiredPoolSize : 0);
		return nullptr;
	}

	// 해당 클래스의 Pool에서 꺼내기
	AHellunaEnemyCharacter* Actor = PoolData->InactiveActors.Pop();

	// 요청한 EnemyClass와 실제 Actor 클래스가 다르면 잘못된 Pool에서 꺼낸 것 — 즉시 경고
	if (Actor->GetClass() != EnemyClass)
	{
		UE_LOG(LogECSPool, Error,
			TEXT("[Pool] ActivateActor 클래스 불일치! 요청:%s | 실제:%s — Pool 초기화 설정 확인 필요"),
			*EnemyClass->GetName(), *Actor->GetClass()->GetName());
		// Pool에 되돌리고 nullptr 반환
		PoolData->InactiveActors.Add(Actor);
		return nullptr;
	}

	PoolData->ActiveActors.Add(Actor);

	// 1. 위치 설정
	Actor->SetActorTransform(SpawnTransform);

	// 2. 보이기 + 활성화
	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(true);
	Actor->SetReplicates(true);

	// CharacterMovement 재활성화 (DeactivateActor에서 꺼둔 것)
	if (UCharacterMovementComponent* MoveComp = Actor->GetCharacterMovement())
	{
		if (MoveComp->IsRegistered())
			MoveComp->SetComponentTickEnabled(true);
	}

	// 3. AI Controller + StateTree 시작
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		AController* Controller = Pawn->GetController();

		// 첫 활성화: Controller 없음 → Deferred Spawn으로 수동 생성
		if (!Controller)
		{
			UWorld* World = Actor->GetWorld();
			TSubclassOf<AController> AIControllerClass = Actor->AIControllerClass;

			if (World && AIControllerClass)
			{
				AController* NewController = World->SpawnActorDeferred<AController>(
					AIControllerClass,
					Actor->GetActorTransform(),
					nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

				if (NewController)
				{
					// BeginPlay에서 StateTree 자동 시작 방지 (Possess 전 Pawn 컨텍스트 없음 → 에러)
					if (UStateTreeComponent* STComp = NewController->FindComponentByClass<UStateTreeComponent>())
					{
						STComp->SetStartLogicAutomatically(false);
					}

					NewController->FinishSpawning(Actor->GetActorTransform());
					NewController->Possess(Pawn);
					Controller = NewController;
				}
			}
		}

		if (Controller)
		{
			Controller->SetActorTickEnabled(true);

			if (UStateTreeComponent* STComp = Controller->FindComponentByClass<UStateTreeComponent>())
			{
				STComp->SetStartLogicAutomatically(true);
				STComp->SetComponentTickEnabled(true);
				STComp->RestartLogic();
			}
		}
	}

	// 4. HP 복원 (CurrentHP > 0이면 이전 HP, -1이면 풀 HP)
	if (CurrentHP > 0.f)
	{
		if (UHellunaHealthComponent* HC = Actor->FindComponentByClass<UHellunaHealthComponent>())
		{
			HC->SetHealth(CurrentHP);
		}
	}

	// 5. Animation 재개
	if (USkeletalMeshComponent* SkelMesh = Actor->GetMesh())
	{
		if (SkelMesh->IsRegistered())
		{
			SkelMesh->SetComponentTickEnabled(true);
			SkelMesh->bPauseAnims = false;
			SkelMesh->bNoSkeletonUpdate = false;
		}
	}

	UE_LOG(LogECSPool, Verbose,
		TEXT("[Pool] Activate! Class: %s | Actor: %s | 위치: %s | HP: %.1f | Active: %d, Inactive: %d"),
		*EnemyClass->GetName(),
		*Actor->GetName(),
		*SpawnTransform.GetLocation().ToString(),
		CurrentHP > 0.f ? CurrentHP : MaxHP,
		PoolData->ActiveActors.Num(),
		PoolData->InactiveActors.Num());

	return Actor;
}

// ============================================================================
// DeactivateActor — Actor를 해당 클래스의 Pool에 반납
//
// [수정 포인트]
//   이전: ActiveActors/InactiveActors 단일 배열 직접 접근
//   이후: Actor->GetClass()를 키로 PerClassPools에서 올바른 Pool 탐색
//         → 근거리 Actor는 근거리 Pool로, 원거리 Actor는 원거리 Pool로 자동 반납
// ============================================================================
void UEnemyActorPool::DeactivateActor(AHellunaEnemyCharacter* Actor)
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed())
	{
		return;
	}

	// ActiveActors에 없으면 이미 처리된 Actor — 중복 처리 방지
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass = Actor->GetClass();
	FActorPoolData* PoolData = PerClassPools.Find(EnemyClass);
	if (!PoolData || !PoolData->ActiveActors.Contains(Actor))
	{
		return;
	}

	// 1. AI 정지
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			if (UStateTreeComponent* STComp = Controller->FindComponentByClass<UStateTreeComponent>())
			{
				if (STComp->IsRegistered())
				{
					STComp->StopLogic(TEXT("Pool deactivation"));
					STComp->SetComponentTickEnabled(false);
				}
			}
			Controller->SetActorTickEnabled(false);
		}
	}

	// 2. CharacterMovement 정지
	if (UCharacterMovementComponent* MoveComp = Actor->GetCharacterMovement())
	{
		if (MoveComp->IsRegistered())
		{
			MoveComp->StopMovementImmediately();
			MoveComp->SetComponentTickEnabled(false);
		}
	}

	// 3. Animation 완전 정지 (Hidden 상태에서도 본 변환이 계속되므로 명시적으로 꺼야 함)
	if (USkeletalMeshComponent* SkelMesh = Actor->GetMesh())
	{
		if (SkelMesh->IsRegistered())
		{
			SkelMesh->bPauseAnims = true;
			SkelMesh->bNoSkeletonUpdate = true;
			SkelMesh->SetComponentTickEnabled(false);
		}
	}

	// 4. 숨기기 + 비활성화
	Actor->SetActorTickEnabled(false);
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetReplicates(false);

	// 5. 숨김 위치로 이동
	Actor->SetActorLocation(PoolHiddenLocation);

	// 6. Pool 반납 (앞에서 이미 PoolData를 구했으므로 재조회 없이 직접 사용)
	PoolData->ActiveActors.Remove(Actor);
	PoolData->InactiveActors.Add(Actor);

	UE_LOG(LogECSPool, Verbose,
		TEXT("[Pool] Deactivate! Class: %s | Actor: %s | Active: %d, Inactive: %d"),
		*EnemyClass->GetName(),
		*Actor->GetName(),
		PoolData->ActiveActors.Num(),
		PoolData->InactiveActors.Num());
}

// ============================================================================
// CleanupAndReplenish — 파괴된 Actor 정리 + Pool 보충
//
// [수정 포인트]
//   이전: InactiveActors/ActiveActors 단일 배열 순회
//   이후: PerClassPools의 모든 항목을 순회하여 클래스별로 독립 보충
//         → 근거리 Pool 파괴분은 근거리 클래스로, 원거리 Pool 파괴분은 원거리 클래스로 보충
// ============================================================================
void UEnemyActorPool::CleanupAndReplenish()
{
	// 모든 EnemyClass의 Pool을 순회
	for (auto& Pair : PerClassPools)
	{
		TSubclassOf<AHellunaEnemyCharacter> EnemyClass = Pair.Key;
		FActorPoolData& PoolData = Pair.Value;

		// 파괴된 Actor 제거 (IsValid가 false인 항목)
		const int32 RemovedInactive = PoolData.InactiveActors.RemoveAll(
			[](const TObjectPtr<AHellunaEnemyCharacter>& Actor) { return !IsValid(Actor); });
		const int32 RemovedActive = PoolData.ActiveActors.RemoveAll(
			[](const TObjectPtr<AHellunaEnemyCharacter>& Actor) { return !IsValid(Actor); });

		// 파괴된 Actor가 없으면 이 클래스는 스킵
		if (RemovedInactive + RemovedActive == 0)
		{
			continue;
		}

		// 부족분 보충 (DesiredPoolSize 유지)
		const int32 TotalActors = PoolData.InactiveActors.Num() + PoolData.ActiveActors.Num();
		const int32 ToCreate = PoolData.DesiredPoolSize - TotalActors;

		int32 Created = 0;
		for (int32 i = 0; i < ToCreate; ++i)
		{
			// 해당 클래스를 명시적으로 전달하여 보충
			AHellunaEnemyCharacter* NewActor = CreatePooledActor(EnemyClass);
			if (NewActor)
			{
				PoolData.InactiveActors.Add(NewActor);
				Created++;
			}
		}

		UE_LOG(LogECSPool, Log,
			TEXT("[Pool] Replenish! Class: %s | 제거: %d (Inactive:%d Active:%d) | 보충: %d/%d | Total: %d/%d"),
			*EnemyClass->GetName(),
			RemovedInactive + RemovedActive, RemovedInactive, RemovedActive,
			Created, ToCreate,
			PoolData.InactiveActors.Num() + PoolData.ActiveActors.Num(),
			PoolData.DesiredPoolSize);
	}
}

// ============================================================================
// CreatePooledActor — 단일 Pool Actor 사전 생성
//
// [수정 포인트]
//   이전: CreatePooledActor() — CachedEnemyClass 사용 (단일 클래스 고정)
//   이후: CreatePooledActor(EnemyClass) — 호출 시 클래스를 명시적으로 전달
//         → 근거리/원거리 각각의 클래스로 올바른 Actor 생성 가능
// ============================================================================
AHellunaEnemyCharacter* UEnemyActorPool::CreatePooledActor(TSubclassOf<AHellunaEnemyCharacter> EnemyClass)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] CreatePooledActor 실패! World가 null."));
		return nullptr;
	}
	if (!EnemyClass)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] CreatePooledActor 실패! EnemyClass가 null."));
		return nullptr;
	}

	const FTransform HiddenTransform(FRotator::ZeroRotator, PoolHiddenLocation);

	// SpawnActorDeferred: BeginPlay 전에 AutoPossessAI를 끌 수 있어 StateTree 에러 방지
	AHellunaEnemyCharacter* Actor = World->SpawnActorDeferred<AHellunaEnemyCharacter>(
		EnemyClass, HiddenTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Actor)
	{
		UE_LOG(LogECSPool, Error,
			TEXT("[Pool] CreatePooledActor 실패! SpawnActorDeferred 반환 null — Class: %s"),
			*EnemyClass->GetName());
		return nullptr;
	}

	// AutoPossessAI 비활성화 — StateTree가 Possess 전에 시작되지 않도록
	Actor->AutoPossessAI = EAutoPossessAI::Disabled;
	Actor->FinishSpawning(HiddenTransform);

	// 즉시 숨기기 + 비활성화
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);
	Actor->SetReplicates(false);

	if (UCharacterMovementComponent* MoveComp = Actor->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->SetComponentTickEnabled(false);
	}

	if (USkeletalMeshComponent* SkelMesh = Actor->GetMesh())
	{
		SkelMesh->bPauseAnims = true;
		SkelMesh->bNoSkeletonUpdate = true;
		SkelMesh->SetComponentTickEnabled(false);
	}

	return Actor;
}

// ============================================================================
// 상태 조회 — 클래스별 / 전체 합산
// ============================================================================

int32 UEnemyActorPool::GetActiveCount(TSubclassOf<AHellunaEnemyCharacter> EnemyClass) const
{
	const FActorPoolData* PoolData = PerClassPools.Find(EnemyClass);
	return PoolData ? PoolData->ActiveActors.Num() : 0;
}

int32 UEnemyActorPool::GetInactiveCount(TSubclassOf<AHellunaEnemyCharacter> EnemyClass) const
{
	const FActorPoolData* PoolData = PerClassPools.Find(EnemyClass);
	return PoolData ? PoolData->InactiveActors.Num() : 0;
}

int32 UEnemyActorPool::GetTotalActiveCount() const
{
	int32 Total = 0;
	for (const auto& Pair : PerClassPools)
	{
		Total += Pair.Value.ActiveActors.Num();
	}
	return Total;
}

int32 UEnemyActorPool::GetTotalInactiveCount() const
{
	int32 Total = 0;
	for (const auto& Pair : PerClassPools)
	{
		Total += Pair.Value.InactiveActors.Num();
	}
	return Total;
}
