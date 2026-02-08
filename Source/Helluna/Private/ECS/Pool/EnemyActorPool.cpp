/**
 * EnemyActorPool.cpp
 *
 * Actor Object Pooling 구현체.
 *
 * ■ 이 파일이 뭔가요? (팀원용)
 *   적 Actor "대여소"의 실제 동작 코드입니다.
 *   미리 만들어둔 Actor를 꺼내 쓰고(Activate) 돌려놓는(Deactivate) 로직,
 *   그리고 전투 사망으로 파괴된 Actor를 보충하는(Replenish) 로직이 들어 있습니다.
 *
 * ■ 시스템 내 위치
 *   - 의존: AHellunaEnemyCharacter, UHellunaHealthComponent,
 *           UStateTreeComponent, AController, UWorld
 *   - 피의존: UEnemyActorSpawnProcessor (TrySpawnActor/DespawnActorToEntity에서 호출)
 *
 * ■ 디버깅 팁
 *   - LogECSPool 카테고리로 모든 Pool 이벤트 로깅
 *   - 초기화: "[Pool] 초기화 완료!" / Activate: "[Pool] Activate!" / Deactivate: "[Pool] Deactivate!"
 *   - 보충: "[Pool] Replenish!" (파괴된 Actor 수 + 보충 수)
 *   - 에러: "[Pool] CreatePooledActor 실패!" → EnemyClass 또는 World 확인
 */

// File: Source/Helluna/Private/ECS/Pool/EnemyActorPool.cpp

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
// ShouldCreateSubsystem: 서브시스템 생성 여부
// ============================================================================
// ■ 쉬운 설명: 모든 월드에서 Pool 서브시스템을 자동 생성한다.
// ■ 반환값: 항상 true
// ============================================================================
bool UEnemyActorPool::ShouldCreateSubsystem(UObject* Outer) const
{
	return true;
}

// ============================================================================
// Deinitialize: 월드 소멸 시 정리
// ============================================================================
// ■ 쉬운 설명: 게임 종료 또는 맵 전환 시 Pool의 모든 Actor를 파괴한다.
// ■ 주의사항: UWorldSubsystem이 자동 호출. 수동 호출 금지.
// ============================================================================
void UEnemyActorPool::Deinitialize()
{
	// 비활성 Actor: Controller가 Possess 중이지만 StateTree는 Stop 상태.
	// Destroy 시 StopLogic이 다시 불려 Warning 발생 → UnPossess 먼저 수행.
	for (AHellunaEnemyCharacter* Actor : InactiveActors)
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
	for (AHellunaEnemyCharacter* Actor : ActiveActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}

	UE_LOG(LogECSPool, Log,
		TEXT("[Pool] Deinitialize - 모든 Pool Actor 파괴 완료 (Inactive: %d, Active: %d)"),
		InactiveActors.Num(), ActiveActors.Num());

	InactiveActors.Empty();
	ActiveActors.Empty();
	bInitialized = false;
}

// ============================================================================
// InitializePool: Pool 사전 생성
// ============================================================================
// ■ 쉬운 설명: 적 Actor를 미리 60개 만들어 "대기열"에 넣어둔다.
//   전부 숨김(Hidden) + 비활성(TickOff) 상태로, 필요할 때 꺼내 쓴다.
// ■ 매개변수:
//   - EnemyClass: 스폰할 적 블루프린트 (Trait의 EnemyClass)
//   - InPoolSize: 사전 생성 수 (Trait의 PoolSize, 기본 60)
// ■ 주의사항:
//   - 중복 호출 시 무시 (Warning 로그)
//   - EnemyClass가 null이면 Error 로그 후 실패
//   - 60개 동시 생성 = 첫 틱 ~120ms 소요 가능
// ■ 관련 함수: CreatePooledActor()
// ============================================================================
void UEnemyActorPool::InitializePool(
	TSubclassOf<AHellunaEnemyCharacter> EnemyClass,
	int32 InPoolSize)
{
	if (bInitialized)
	{
		UE_LOG(LogECSPool, Warning, TEXT("[Pool] 이미 초기화됨. 중복 호출 무시."));
		return;
	}

	if (!EnemyClass)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] EnemyClass가 null! Trait에서 EnemyClass를 설정하세요."));
		return;
	}

	CachedEnemyClass = EnemyClass;
	DesiredPoolSize = InPoolSize;

	InactiveActors.Reserve(InPoolSize);

	int32 SuccessCount = 0;
	for (int32 i = 0; i < InPoolSize; ++i)
	{
		AHellunaEnemyCharacter* Actor = CreatePooledActor();
		if (Actor)
		{
			InactiveActors.Add(Actor);
			SuccessCount++;
		}
	}

	bInitialized = true;

	UE_LOG(LogECSPool, Log,
		TEXT("[Pool] 초기화 완료! Class: %s, 요청: %d, 성공: %d, 실패: %d"),
		*EnemyClass->GetName(), InPoolSize, SuccessCount, InPoolSize - SuccessCount);
}

// ============================================================================
// ActivateActor: Pool에서 Actor 꺼내기 (Entity → Actor 전환)
// ============================================================================
// ■ 쉬운 설명: "대기열"에서 적 하나를 꺼내 월드에 배치한다.
//   위치 설정 → 보이기 → AI 시작 → HP 복원 → 네트워크 동기화.
//   SpawnActor(~2ms) 대신 이 함수(~0.1ms)를 사용하여 성능 향상.
// ■ 매개변수:
//   - SpawnTransform: 배치할 위치/회전 (Fragment의 Transform)
//   - CurrentHP: 복원할 HP (-1이면 풀 HP = 첫 스폰)
//   - MaxHP: 최대 HP (로그용)
// ■ 반환값: 활성화된 Actor. Pool이 비어있으면 nullptr.
// ■ 주의사항:
//   - Pool이 비면 "[Pool] 비활성 Actor 없음!" 경고 → PoolSize 늘리기
//   - Pool Actor는 이미 AutoPossessAI 완료 상태 → 0.2초 타이머 불필요
//   - RestartLogic()은 Controller의 StateTree를 즉시 재시작
// ■ 관련 함수: DeactivateActor(), TrySpawnActor() (Processor에서 호출)
// ============================================================================
AHellunaEnemyCharacter* UEnemyActorPool::ActivateActor(
	const FTransform& SpawnTransform,
	float CurrentHP,
	float MaxHP)
{
	if (InactiveActors.IsEmpty())
	{
		UE_LOG(LogECSPool, Warning,
			TEXT("[Pool] 비활성 Actor 없음! Pool 소진. Active: %d, PoolSize: %d"),
			ActiveActors.Num(), DesiredPoolSize);
		return nullptr;
	}

	AHellunaEnemyCharacter* Actor = InactiveActors.Pop();
	ActiveActors.Add(Actor);

	// 1. 위치 설정 (Pool의 숨김 위치 → 실제 전투 위치)
	Actor->SetActorTransform(SpawnTransform);

	// 2. 보이기 + 활성화
	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(true);
	Actor->SetReplicates(true);

	// CharacterMovement 재활성화 (DeactivateActor/CreatePooledActor에서 꺼둔 것)
	if (UCharacterMovementComponent* MoveComp = Actor->GetCharacterMovement())
	{
		MoveComp->SetComponentTickEnabled(true);
	}

	// 3. AI Controller + StateTree 시작
	// CreatePooledActor에서 AutoPossessAI를 끈 상태로 생성했으므로
	// 첫 활성화 시 Controller가 없다. SpawnDefaultController()로 수동 생성.
	// 두 번째 이후 활성화: DeactivateActor가 Controller를 살려뒀으므로 RestartLogic만 호출.
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		AController* Controller = Pawn->GetController();

		// 첫 활성화: Controller 없음 → Deferred Spawn으로 수동 생성
		// SpawnDefaultController()는 내부에서 BeginPlay → StateTree::StartTree가
		// Possess보다 먼저 실행되어 에러 발생 (Actor당 3줄 × 50마리 = 150줄).
		// 수동으로: Deferred Spawn → StateTree 끄기 → FinishSpawning → Possess → StateTree 시작
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
					// ★ 핵심: BeginPlay에서 StateTree 자동 시작을 막는다.
					// UStateTreeComponent::BeginPlay()는 bStartLogicAutomatically가 true이면
					// StartLogic() → StartTree()를 호출한다. Possess 전이라 Pawn 컨텍스트 없음 → 에러.
					// SetStartLogicAutomatically(false)로 자동 시작을 건너뛴다.
					if (UStateTreeComponent* STComp = NewController->FindComponentByClass<UStateTreeComponent>())
					{
						STComp->SetStartLogicAutomatically(false);
					}

					// FinishSpawning → BeginPlay 호출되지만 자동 시작 꺼져있으므로 에러 0
					NewController->FinishSpawning(Actor->GetActorTransform());

					// Possess: Controller가 Pawn을 소유 → Pawn 컨텍스트 설정됨
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
				// 첫 활성화 시 꺼둔 자동 시작 복원 (재활성화 시에도 안전)
				STComp->SetStartLogicAutomatically(true);
				STComp->SetComponentTickEnabled(true);
				STComp->RestartLogic();
			}
		}
	}

	// 4. HP 복원 (CurrentHP > 0이면 역변환된 적의 이전 HP, -1이면 풀 HP = 첫 스폰)
	if (CurrentHP > 0.f)
	{
		if (UHellunaHealthComponent* HC = Actor->FindComponentByClass<UHellunaHealthComponent>())
		{
			HC->SetHealth(CurrentHP);
		}
	}

	// === Animation URO 리셋 ===
	// Pool에서 꺼낸 Actor의 SkeletalMesh를 즉시 풀 업데이트 상태로 복원.
	// DeactivateActor/CreatePooledActor에서 꺼뒀던 애니메이션을 재시작한다.
	if (USkeletalMeshComponent* SkelMesh = Actor->GetMesh())
	{
		SkelMesh->SetComponentTickEnabled(true);
		SkelMesh->bPauseAnims = false;       // 애니메이션 재개
		SkelMesh->bNoSkeletonUpdate = false;  // 스켈레톤 업데이트 재개
	}

	// TODO: GameMode->RegisterAliveMonster(Actor) 호출 필요
	// BeginPlay에서 이미 등록되었지만, Pool 재활성화 시 재등록이 필요할 수 있음.
	// 웨이브 시스템 연동 시 함께 구현 예정.

	UE_LOG(LogECSPool, Verbose,
		TEXT("[Pool] Activate! Actor: %s, 위치: %s, HP: %.1f, Active: %d, Inactive: %d"),
		*Actor->GetName(),
		*SpawnTransform.GetLocation().ToString(),
		CurrentHP > 0.f ? CurrentHP : MaxHP,
		ActiveActors.Num(), InactiveActors.Num());

	return Actor;
}

// ============================================================================
// DeactivateActor: Actor를 Pool에 반납 (Actor → Entity 복귀)
// ============================================================================
// ■ 쉬운 설명: 사용 끝난 적 Actor를 "대기열"에 돌려놓는다.
//   AI 정지 → 숨기기 → 비활성화 → 네트워크 차단 → 숨김 위치 이동.
//   Destroy(GC 스파이크) 대신 이 함수를 사용하여 성능 향상.
// ■ 매개변수:
//   - Actor: 비활성화할 Actor (nullptr/무효이면 무시)
// ■ 주의사항:
//   - HP/위치 저장은 DespawnActorToEntity가 이 함수 호출 전에 수행
//   - Controller는 Possess 유지 (재활성화 시 즉시 사용 가능)
//   - Destroy와 달리 Actor 인스턴스는 살아있음 (IsValid = true)
// ■ 관련 함수: ActivateActor(), DespawnActorToEntity() (Processor에서 호출)
// ============================================================================
void UEnemyActorPool::DeactivateActor(AHellunaEnemyCharacter* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	// 1. AI 정지 (StateTree StopLogic + Controller Tick Off)
	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AController* Controller = Pawn->GetController())
		{
			if (UStateTreeComponent* STComp = Controller->FindComponentByClass<UStateTreeComponent>())
			{
				STComp->StopLogic(TEXT("Pool deactivation"));
				STComp->SetComponentTickEnabled(false);
			}
			Controller->SetActorTickEnabled(false);
		}
	}

	// 2. CharacterMovement 정지 (SetActorTickEnabled만으로는 부족 — 별도 Tick 보유)
	if (UCharacterMovementComponent* MoveComp = Actor->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->SetComponentTickEnabled(false);
	}

	// 2-1. Animation 완전 정지 (CPU 절약)
	// Pool 반납 시 SkeletalMesh 업데이트를 완전 정지.
	// Hidden 상태에서도 기본적으로 본 변환이 계속되므로 명시적으로 꺼야 한다.
	if (USkeletalMeshComponent* SkelMesh = Actor->GetMesh())
	{
		SkelMesh->bPauseAnims = true;         // 애니메이션 평가 정지
		SkelMesh->bNoSkeletonUpdate = true;   // 스켈레톤 본 업데이트 정지
		SkelMesh->SetComponentTickEnabled(false);  // 컴포넌트 Tick 정지
	}

	// 3. 숨기기 + 비활성화 + 네트워크 차단
	Actor->SetActorTickEnabled(false);
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetReplicates(false);

	// 4. 숨김 위치로 이동 (물리/렌더링/Perception 범위 밖)
	Actor->SetActorLocation(PoolHiddenLocation);

	// 5. Pool 목록 갱신 (Active → Inactive)
	ActiveActors.Remove(Actor);
	InactiveActors.Add(Actor);

	UE_LOG(LogECSPool, Verbose,
		TEXT("[Pool] Deactivate! Actor: %s, Active: %d, Inactive: %d"),
		*Actor->GetName(), ActiveActors.Num(), InactiveActors.Num());
}

// ============================================================================
// CleanupAndReplenish: 파괴된 Actor 정리 + Pool 보충
// ============================================================================
// ■ 쉬운 설명: 전투 중 죽은 적의 "빈 자리"를 새 Actor로 채운다.
//   전투 사망 = Actor가 외부에서 Destroy됨 → Pool에 빈 슬롯 발생
//   이 함수가 빈 슬롯을 감지하고 새 Actor를 생성하여 Pool 크기를 유지한다.
// ■ 주의사항:
//   - Processor에서 60프레임마다 호출 (매 틱 호출 시 성능 낭비)
//   - 파괴된 Actor가 없으면 early return (비용 0)
//   - 새로 생성된 Actor도 0.3초 타이머로 AI 정지됨 (CreatePooledActor)
// ■ 관련 함수: CreatePooledActor()
// ============================================================================
void UEnemyActorPool::CleanupAndReplenish()
{
	// 파괴된 Actor 제거 (IsValid가 false인 항목)
	const int32 RemovedInactive = InactiveActors.RemoveAll(
		[](const TObjectPtr<AHellunaEnemyCharacter>& Actor) { return !IsValid(Actor); });
	const int32 RemovedActive = ActiveActors.RemoveAll(
		[](const TObjectPtr<AHellunaEnemyCharacter>& Actor) { return !IsValid(Actor); });

	if (RemovedInactive + RemovedActive == 0)
	{
		return;  // 파괴된 Actor 없음 → 정리/보충 불필요
	}

	// 부족분 보충 (DesiredPoolSize 유지)
	const int32 TotalActors = InactiveActors.Num() + ActiveActors.Num();
	const int32 ToCreate = DesiredPoolSize - TotalActors;

	int32 Created = 0;
	for (int32 i = 0; i < ToCreate; ++i)
	{
		AHellunaEnemyCharacter* NewActor = CreatePooledActor();
		if (NewActor)
		{
			InactiveActors.Add(NewActor);
			Created++;
		}
	}

	UE_LOG(LogECSPool, Log,
		TEXT("[Pool] Replenish! 제거: %d (Inactive: %d, Active: %d), 보충: %d/%d, Total: %d/%d"),
		RemovedInactive + RemovedActive, RemovedInactive, RemovedActive,
		Created, ToCreate,
		InactiveActors.Num() + ActiveActors.Num(), DesiredPoolSize);
}

// ============================================================================
// CreatePooledActor: 단일 Pool Actor 사전 생성
// ============================================================================
// ■ 쉬운 설명: 적 Actor 1마리를 만들어 바로 숨기고 재우는 함수.
//   SpawnActorDeferred → AutoPossessAI 끄기 → FinishSpawning → 숨기기.
//   Controller는 생성하지 않는다 (ActivateActor에서 첫 호출 시 생성).
// ■ 반환값: 생성된 Actor. 실패 시 nullptr.
// ■ 주의사항:
//   - 숨김 위치(Z=-50000)에 생성하여 플레이어에게 안 보임
//   - AutoPossessAI = Disabled → Controller 없음 → StateTree 에러 0
//   - ActivateActor 첫 호출 시 SpawnDefaultController()로 Controller 생성
//     → Pawn이 이미 올바른 위치+보이는 상태 → StateTree 정상 시작
//   - 두 번째 이후: DeactivateActor가 Controller 유지 → RestartLogic만 호출
// ■ 관련 함수: InitializePool(), CleanupAndReplenish()
// ============================================================================
AHellunaEnemyCharacter* UEnemyActorPool::CreatePooledActor()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] CreatePooledActor 실패! World가 null."));
		return nullptr;
	}
	if (!CachedEnemyClass)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] CreatePooledActor 실패! CachedEnemyClass가 null."));
		return nullptr;
	}

	const FTransform HiddenTransform(FRotator::ZeroRotator, PoolHiddenLocation);

	// SpawnActorDeferred 사용: BeginPlay 전에 AutoPossessAI를 끌 수 있다.
	// 왜: AutoPossessAI가 켜져 있으면 SpawnActor 즉시 Possess → StateTree::StartTree
	//     → 아직 Pawn 컨텍스트가 없어서 에러 60×3줄 스팸.
	// 해결: Deferred 스폰 → AutoPossess 끄기 → FinishSpawning → 에러 0
	//       Possess는 ActivateActor에서 SpawnDefaultController() 호출 시 수행.
	AHellunaEnemyCharacter* Actor = World->SpawnActorDeferred<AHellunaEnemyCharacter>(
		CachedEnemyClass, HiddenTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Actor)
	{
		UE_LOG(LogECSPool, Error, TEXT("[Pool] CreatePooledActor 실패! SpawnActorDeferred 반환 null. Class: %s"),
			*CachedEnemyClass->GetName());
		return nullptr;
	}

	// BeginPlay 전에 AutoPossessAI 비활성화 (StateTree 에러 방지)
	Actor->AutoPossessAI = EAutoPossessAI::Disabled;

	// FinishSpawning → BeginPlay 호출되지만 AutoPossess 꺼져있으므로
	// Controller 생성 안 됨 → StateTree 시작 안 됨 → 에러 0
	Actor->FinishSpawning(HiddenTransform);

	// 즉시 숨기기 + 비활성화
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);
	Actor->SetReplicates(false);

	// CharacterMovement도 꺼야 stuck 로그 방지
	// SetActorTickEnabled만으로는 CharacterMovementComponent의 자체 Tick이 꺼지지 않음
	if (UCharacterMovementComponent* MoveComp = Actor->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->SetComponentTickEnabled(false);
	}

	// Pool 생성 시 Animation 정지
	// 사전 생성된 Actor는 Hidden이지만 애니메이션이 기본으로 돌아간다.
	// 60개 Actor의 애니메이션이 불필요하게 돌면 초기 CPU 낭비 (~3ms).
	if (USkeletalMeshComponent* SkelMesh = Actor->GetMesh())
	{
		SkelMesh->bPauseAnims = true;
		SkelMesh->bNoSkeletonUpdate = true;
		SkelMesh->SetComponentTickEnabled(false);
	}

	return Actor;
}
