/**
 * EnemyActorSpawnProcessor.cpp
 *
 * 하이브리드 ECS 시스템의 핵심 Processor 구현.
 * 서버에서 Mass Entity와 플레이어 간 거리를 계산하여
 * 가까운 엔티티를 AHellunaEnemyCharacter로 전환한다.
 *
 * SpawnActor 호출만으로 기존 시스템이 모두 자동 초기화된다:
 * - AutoPossessAI = PlacedInWorldOrSpawned
 *   -> 엔진이 AIController 자동 스폰 + Possess
 * - PossessedBy() -> InitEnemyStartUpData()
 *   -> GAS/어빌리티 자동 초기화
 * - bReplicates = true (ACharacter 기본)
 *   -> 클라이언트에 자동 복제
 *
 * [UE 5.7.2 API 적용 사항]
 * - ConfigureQueries(const TSharedRef<FMassEntityManager>&) 사용
 * - ForEachEntityChunk(Context, ...) 사용 (EntityManager 버전은 deprecated)
 * - Context.GetWorld()로 월드 참조 획득
 * - RegisterQuery(EntityQuery) 필수 호출
 * - 거리 비교는 DistSquared로 수행 (Sqrt 호출 최소화)
 */

// File: Source/Helluna/Private/ECS/Processors/EnemyActorSpawnProcessor.cpp

#include "ECS/Processors/EnemyActorSpawnProcessor.h"

// Mass Entity 관련
#include "MassCommonFragments.h"        // FTransformFragment
#include "MassExecutionContext.h"        // FMassExecutionContext

// 커스텀 Fragment
#include "ECS/Fragments/EnemyMassFragments.h"

// 기존 Helluna 클래스 (수정하지 않음, include만)
#include "Character/HellunaEnemyCharacter.h"

// UE 유틸리티
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// 커스텀 로그 카테고리: 모든 ECS 하이브리드 시스템 로그에 사용
DEFINE_LOG_CATEGORY_STATIC(LogECSEnemy, Log, All);

// ============================================================================
// 생성자
// ============================================================================
UEnemyActorSpawnProcessor::UEnemyActorSpawnProcessor()
{
	// ------------------------------------------------------------------
	// 실행 플래그 설정
	// Server: 데디케이티드 서버에서 실행
	// Standalone: 에디터 PIE/Standalone 테스트에서 실행
	// Client는 제외: Mass Entity 시뮬레이션은 서버에서만 수행하고
	//               클라이언트는 Actor 리플리케이션으로 받으므로 불필요
	// ------------------------------------------------------------------
	ExecutionFlags = (uint8)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);

	// ------------------------------------------------------------------
	// SpawnActor는 반드시 게임 스레드에서만 호출 가능.
	// Mass Processor는 기본적으로 멀티스레드에서 실행될 수 있으므로
	// 이 플래그를 true로 설정하여 게임 스레드 실행을 보장한다.
	// ------------------------------------------------------------------
	bRequiresGameThreadExecution = true;

	// ProcessingPhase는 기본값 EMassProcessingPhase::PrePhysics 사용

	// ------------------------------------------------------------------
	// ⚠️ RegisterQuery는 반드시 생성자에서 호출해야 한다!
	// CallInitialize 흐름:
	//   1) OwnedQueries 순회 → Query->Initialize(EntityManager) → bInitialized = true
	//   2) ConfigureQueries() 호출 → AddRequirement 사용 가능
	// 생성자에서 RegisterQuery를 안 하면 Step 1에서 OwnedQueries가 비어있어
	// 쿼리가 Initialize되지 않고, AddRequirement에서 assert 발생!
	// ------------------------------------------------------------------
	RegisterQuery(EntityQuery);
}

// ============================================================================
// ConfigureQueries: Fragment 요구사항 등록
// UE 5.7.2 시그니처: const TSharedRef<FMassEntityManager>& 파라미터 필수
// ============================================================================
void UEnemyActorSpawnProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	// ------------------------------------------------------------------
	// EntityQuery에 필요한 Fragment를 등록한다.
	// 이 쿼리를 만족하는 엔티티만 ForEachEntityChunk에서 순회된다.
	// ※ RegisterQuery는 생성자에서 이미 호출됨 (CallInitialize 초기화 순서 때문)
	// ------------------------------------------------------------------

	// UE 5.7.2: ForEachEntityChunk 사용 시 반드시 RegisterWithProcessor 필요
	// RegisterQuery(생성자)는 OwnedQueries에 추가만 하고,
	// RegisterWithProcessor는 bRegistered 플래그를 설정하여 실행 시 assert 방지
	EntityQuery.RegisterWithProcessor(*this);

	// FTransformFragment: 엔티티의 월드 위치/회전 (ReadOnly - 위치만 읽음)
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);

	// FEnemySpawnStateFragment: 전환 상태 추적 (ReadWrite - bHasSpawnedActor를 갱신해야 함)
	EntityQuery.AddRequirement<FEnemySpawnStateFragment>(EMassFragmentAccess::ReadWrite);

	// FEnemyDataFragment: 스폰 설정 데이터 (ReadOnly - EnemyClass, SpawnThreshold 읽기)
	EntityQuery.AddRequirement<FEnemyDataFragment>(EMassFragmentAccess::ReadOnly);

	UE_LOG(LogECSEnemy, Log, TEXT("[EnemyActorSpawnProcessor] ConfigureQueries 완료 - 쿼리 등록됨"));
}

// ============================================================================
// Execute: 매 틱 메인 로직
// ============================================================================
void UEnemyActorSpawnProcessor::Execute(
	FMassEntityManager& EntityManager,
	FMassExecutionContext& Context)
{
	// ------------------------------------------------------------------
	// Step 0: World 유효성 검사
	// UMassProcessor는 UObject 상속이라 GetWorld()가 불안정하다.
	// 반드시 Context.GetWorld()를 사용해야 한다.
	// ------------------------------------------------------------------
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		UE_LOG(LogECSEnemy, Error, TEXT("[Execute] World가 null입니다!"));
		return;
	}

	// 디버그: Execute 진입 확인
	static uint64 LastLogFrame = 0;
	uint64 CurrentFrame = GFrameCounter;
	if (CurrentFrame != LastLogFrame && (CurrentFrame % 300 == 0)) // ~5초마다 1회
	{
		UE_LOG(LogECSEnemy, Warning, TEXT("[Execute] ▶ 진입! World: %s, IsServer: %s"),
			*World->GetName(),
			World->IsNetMode(NM_DedicatedServer) || World->IsNetMode(NM_ListenServer) ? TEXT("YES") : TEXT("NO"));
		LastLogFrame = CurrentFrame;
	}

	// ------------------------------------------------------------------
	// Step 1: 서버의 모든 플레이어 위치 수집
	// 서버의 PlayerController를 순회하여 Pawn의 위치를 캐싱한다.
	// 매 틱 한 번만 수집하고, 엔티티 순회 시 재사용한다.
	// ------------------------------------------------------------------
	TArray<FVector> PlayerLocations;
	PlayerLocations.Reserve(4); // 일반적인 플레이어 수를 고려한 예약

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

	// 플레이어가 없으면 거리 비교 불가 -> 조기 종료
	if (PlayerLocations.IsEmpty())
	{
		UE_LOG(LogECSEnemy, Warning, TEXT("[Execute] 플레이어가 없어서 조기 종료!"));
		return;
	}

	UE_LOG(LogECSEnemy, Warning, TEXT("[Execute] 플레이어 %d명 위치 수집 완료"), PlayerLocations.Num());

	// ------------------------------------------------------------------
	// Step 2-3: 엔티티 청크 순회 + 거리 비교 + Actor 스폰
	// UE 5.7.2 API: ForEachEntityChunk(Context, lambda)
	// (EntityManager를 넘기는 버전은 UE 5.6에서 deprecated)
	// ------------------------------------------------------------------
	EntityQuery.ForEachEntityChunk(Context,
		[this, World, &PlayerLocations](FMassExecutionContext& Context)
		{
			// Fragment 배열 뷰 획득
			const auto TransformList = Context.GetFragmentView<FTransformFragment>();
			auto SpawnStateList = Context.GetMutableFragmentView<FEnemySpawnStateFragment>();
			const auto DataList = Context.GetFragmentView<FEnemyDataFragment>();
			const int32 NumEntities = Context.GetNumEntities();

			UE_LOG(LogECSEnemy, Warning, TEXT("[Execute] 청크 순회 - 엔티티 %d개 발견"), NumEntities);

			// 각 엔티티 순회
			for (int32 i = 0; i < NumEntities; ++i)
			{
				FEnemySpawnStateFragment& SpawnState = SpawnStateList[i];

				// ----------------------------------------------------------
				// 이미 Actor로 전환된 엔티티는 스킵
				// ----------------------------------------------------------
				if (SpawnState.bHasSpawnedActor)
				{
					continue;
				}

				const FTransformFragment& Transform = TransformList[i];
				const FEnemyDataFragment& Data = DataList[i];
				const FVector EntityLocation = Transform.GetTransform().GetLocation();

				// ----------------------------------------------------------
				// 모든 플레이어와의 최소 제곱 거리 계산
				// DistSquared를 사용하여 Sqrt 호출을 피한다 (성능 최적화)
				// ----------------------------------------------------------
				float MinDistSq = MAX_FLT;
				for (const FVector& PlayerLoc : PlayerLocations)
				{
					const float DistSq = FVector::DistSquared(EntityLocation, PlayerLoc);
					if (DistSq < MinDistSq)
					{
						MinDistSq = DistSq;
					}
				}

				// SpawnThreshold도 제곱하여 비교 (Sqrt 없이 판단)
				const float ThresholdSq = Data.SpawnThreshold * Data.SpawnThreshold;

				// 디버그: 거리 출력
				UE_LOG(LogECSEnemy, Warning, TEXT("  엔티티[%d] 위치: X=%.1f Y=%.1f Z=%.1f, 최소거리: %.1f cm, 임계값: %.1f cm"),
					i, EntityLocation.X, EntityLocation.Y, EntityLocation.Z,
					FMath::Sqrt(MinDistSq), Data.SpawnThreshold);

				// ----------------------------------------------------------
				// Step 3: 거리 < SpawnThreshold이면 Actor 스폰
				// ----------------------------------------------------------
				if (MinDistSq >= ThresholdSq)
				{
					// 아직 멀어서 스폰하지 않음
					continue;
				}

				// EnemyClass Null 체크 (에디터 설정 누락 방지)
				if (!Data.EnemyClass)
				{
					UE_LOG(LogECSEnemy, Error,
						TEXT("[Execute] EnemyClass가 null! 엔티티 %d번 스폰 불가. Trait에서 클래스를 설정하세요."), i);
					continue;
				}

				// ----------------------------------------------------------
				// SpawnActor로 AHellunaEnemyCharacter 생성
				// SpawnActor 호출만으로 기존 시스템이 모두 자동 초기화된다:
				// 1. AutoPossessAI = PlacedInWorldOrSpawned
				//    -> 엔진이 AHellunaAIController 자동 스폰 + Possess
				// 2. PossessedBy() -> InitEnemyStartUpData()
				//    -> GAS/어빌리티 자동 초기화
				// 3. bReplicates = true (ACharacter 기본)
				//    -> 클라이언트에 자동 복제
				// 별도의 AI 컨트롤러 스폰 코드나 GAS 초기화 코드 불필요!
				// ----------------------------------------------------------
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

				// 비템플릿 UWorld::SpawnActor(UClass*, const FTransform*, FActorSpawnParameters)를 사용.
				// UE 5.7.2에서 SpawnActor<T> 템플릿 오버로드 해석 문제를 회피하기 위해
				// 비템플릿 버전 호출 후 Cast<>로 타입 변환한다.
				const FTransform SpawnTransform = Transform.GetTransform();

				AHellunaEnemyCharacter* SpawnedActor = Cast<AHellunaEnemyCharacter>(
					World->SpawnActor(Data.EnemyClass, &SpawnTransform, SpawnParams));

				if (SpawnedActor)
				{
					// 전환 완료 표시
					SpawnState.bHasSpawnedActor = true;
					SpawnState.SpawnedActor = SpawnedActor;

					// 스폰 성공 로그 (실제 거리는 여기서만 Sqrt 호출)
					UE_LOG(LogECSEnemy, Log,
						TEXT("===== Actor 스폰 성공! ====="));
					UE_LOG(LogECSEnemy, Log,
						TEXT("  클래스: %s"),
						*SpawnedActor->GetClass()->GetName());
					UE_LOG(LogECSEnemy, Log,
						TEXT("  위치: X=%.1f Y=%.1f Z=%.1f"),
						EntityLocation.X, EntityLocation.Y, EntityLocation.Z);
					UE_LOG(LogECSEnemy, Log,
						TEXT("  최소 거리: %.1f cm (임계값: %.1f cm)"),
						FMath::Sqrt(MinDistSq), Data.SpawnThreshold);
					UE_LOG(LogECSEnemy, Log,
						TEXT("  bReplicates: %s"),
						SpawnedActor->GetIsReplicated() ? TEXT("true") : TEXT("false"));
				}
				else
				{
					UE_LOG(LogECSEnemy, Error,
						TEXT("Actor 스폰 실패! 클래스: %s, 위치: X=%.1f Y=%.1f Z=%.1f"),
						*Data.EnemyClass->GetName(),
						EntityLocation.X, EntityLocation.Y, EntityLocation.Z);
				}
			}
		}
	);
}
