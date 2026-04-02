#include "ECS/Spawner/HellunaEnemyMassSpawner.h"
#include "MassSimulationSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogHellunaSpawner);

AHellunaEnemyMassSpawner::AHellunaEnemyMassSpawner()
{
	bAutoSpawnOnBeginPlay = false;
}

void AHellunaEnemyMassSpawner::BeginPlay()
{
	Super::BeginPlay();
	DefaultSpawnCount = Count;
	UE_LOG(LogHellunaSpawner, Log, TEXT("[HellunaEnemyMassSpawner] BeginPlay — 자동스폰 OFF"));
}

// ============================================================
// RequestSpawn — 밤 시작 시 GameMode에서 호출
// ============================================================
void AHellunaEnemyMassSpawner::RequestSpawn(int32 InSpawnCount, float AdditionalStartDelay)
{
	if (GetWorld()->GetNetMode() == NM_Client) return;

	if ((DelayTimerHandle.IsValid() && GetWorldTimerManager().IsTimerActive(DelayTimerHandle))
		|| (SpawnBatchTimerHandle.IsValid() && GetWorldTimerManager().IsTimerActive(SpawnBatchTimerHandle)))
	{
		UE_LOG(LogHellunaSpawner, Warning,
			TEXT("[RequestSpawn] 이전 스폰 시퀀스가 진행 중인 상태에서 재호출 — 기존 예약을 초기화합니다. Spawner: %s"), *GetName());
	}

	// 소환 수 확정: 0 이하면 BP 기본값 사용
	RequestedSpawnCount = (InSpawnCount > 0) ? InSpawnCount : DefaultSpawnCount;
	PendingSpawnCount = RequestedSpawnCount;
	PendingInitialDelay = FMath::Max(0.f, SpawnDelay + AdditionalStartDelay);

	// DefaultSpawnCount가 0이면 DoSpawning에서 Count가 덮어써지지 않아 이전 Count 잔재가 쓰일 수 있다.
	if (RequestedSpawnCount <= 0)
	{
		UE_LOG(LogHellunaSpawner, Warning,
			TEXT("[RequestSpawn] RequestedSpawnCount=%d — BP의 Count 기본값을 확인하세요. Spawner: %s"),
			RequestedSpawnCount, *GetName());
	}

	GetWorldTimerManager().ClearTimer(DelayTimerHandle);
	GetWorldTimerManager().ClearTimer(SpawnBatchTimerHandle);

	const UMassSimulationSubsystem* Sim = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld());
	if (Sim && Sim->IsSimulationStarted())
	{
		UE_LOG(LogHellunaSpawner, Log,
			TEXT("[RequestSpawn] 시뮬레이션 준비됨 — %.1f초 후 총 %d마리 순차 소환 예약 (Batch=%d, Interval=%.2f, ExtraDelay=%.2f). Spawner: %s"),
			PendingInitialDelay, RequestedSpawnCount, SpawnBatchSize, SpawnBatchInterval, AdditionalStartDelay, *GetName());

		ScheduleNextSpawnBatch(PendingInitialDelay, true);
	}
	else
	{
		UE_LOG(LogHellunaSpawner, Warning,
			TEXT("[RequestSpawn] 시뮬레이션 미준비 — 콜백 대기. Spawner: %s"), *GetName());

		if (HellunaSimStartedHandle.IsValid())
			UMassSimulationSubsystem::GetOnSimulationStarted().Remove(HellunaSimStartedHandle);

		HellunaSimStartedHandle = UMassSimulationSubsystem::GetOnSimulationStarted().AddUObject(
			this, &AHellunaEnemyMassSpawner::OnSimulationReady);
	}
}

// ============================================================
// CancelPendingSpawn — 낮 전환 시 GameMode 에서 호출
// ============================================================
void AHellunaEnemyMassSpawner::CancelPendingSpawn()
{
	const bool bTimerWasActive = DelayTimerHandle.IsValid() && GetWorldTimerManager().IsTimerActive(DelayTimerHandle);
	const bool bBatchTimerWasActive = SpawnBatchTimerHandle.IsValid() && GetWorldTimerManager().IsTimerActive(SpawnBatchTimerHandle);
	const bool bCallbackWasBound = HellunaSimStartedHandle.IsValid();

	GetWorldTimerManager().ClearTimer(DelayTimerHandle);
	GetWorldTimerManager().ClearTimer(SpawnBatchTimerHandle);
	PendingSpawnCount = 0;

	if (HellunaSimStartedHandle.IsValid())
	{
		UMassSimulationSubsystem::GetOnSimulationStarted().Remove(HellunaSimStartedHandle);
		HellunaSimStartedHandle.Reset();
	}

	UE_LOG(LogHellunaSpawner, Log,
		TEXT("[CancelPendingSpawn] Spawner: %s | 시작 타이머 취소: %s | 배치 타이머 취소: %s | 콜백 해제: %s"),
		*GetName(),
		bTimerWasActive    ? TEXT("YES ⚠️") : TEXT("no"),
		bBatchTimerWasActive ? TEXT("YES ⚠️") : TEXT("no"),
		bCallbackWasBound  ? TEXT("YES ⚠️") : TEXT("no"));
}

// ============================================================
// OnSimulationReady — 시뮬레이션 준비 완료 콜백
// ============================================================
void AHellunaEnemyMassSpawner::OnSimulationReady(UWorld* InWorld)
{
	if (GetWorld() != InWorld) return;

	UMassSimulationSubsystem::GetOnSimulationStarted().Remove(HellunaSimStartedHandle);
	HellunaSimStartedHandle.Reset();

	ScheduleNextSpawnBatch(PendingInitialDelay, true);
}

// ============================================================
// ExecuteDelayedSpawn — 실제 스폰 실행
//
// RequestSpawn에서 설정한 SpawnDelay 후 타이머로 호출됨.
//
// [소환 수 적용 방식]
// AMassSpawner::DoSpawning()은 내부적으로 Count * SpawningCountScale 공식으로 소환 수를 결정.
// Count는 protected 멤버이지만 파생 클래스(HellunaEnemyMassSpawner)에서 직접 접근 가능.
//
// 초기 시도: FMassSpawnedEntityType의 Count 필드 직접 수정
//   → 실패: FMassSpawnedEntityType에는 Count가 없고 Proportion(비율)만 존재
// 2차 시도: ScaleSpawningCount(RequestedSpawnCount / BaseCount)로 배율 조정
//   → 동작하지만 BP 기본 Count값에 의존하여 불안정
// 최종: AMassSpawner::Count를 RequestedSpawnCount로 직접 덮어쓰고 Scale=1 초기화
//   → BP 기본값과 무관하게 항상 정확한 수 보장
// ============================================================
void AHellunaEnemyMassSpawner::ExecuteDelayedSpawn()
{
	AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(
		UGameplayStatics::GetGameMode(GetWorld()));

	if (GM)
	{
		AHellunaDefenseGameState* GS = GM->GetGameState<AHellunaDefenseGameState>();
		if (GS && GS->GetPhase() != EDefensePhase::Night)
		{
			UE_LOG(LogHellunaSpawner, Error,
				TEXT("[ExecuteDelayedSpawn] 🚨 Phase=Day 상태 — 스폰 중단. Spawner: %s"), *GetName());
			return;
		}
	}

	if (PendingSpawnCount <= 0)
	{
		UE_LOG(LogHellunaSpawner, Verbose,
			TEXT("[ExecuteDelayedSpawn] PendingSpawnCount=0 — 시퀀스 종료. Spawner: %s"), *GetName());
		return;
	}

	const int32 SpawnCountThisBatch =
		(SpawnBatchSize > 0) ? FMath::Min(PendingSpawnCount, SpawnBatchSize) : PendingSpawnCount;

	// AMassSpawner::Count는 protected이지만 파생 클래스에서 직접 접근 가능.
	// Count를 직접 배치 수로 덮어써서 한 프레임에 전체 엔티티가 몰리지 않게 한다.
	if (SpawnCountThisBatch > 0)
	{
		Count = SpawnCountThisBatch;
		ScaleSpawningCount(1.0f); // Scale을 1로 초기화 (이전 Scale 잔재 제거)
	}

	// EntityTypes 상태 진단 — BP에 설정된 타입 수와 Proportion 확인
	// EntityTypes가 비어있으면 DoSpawning이 아무것도 생성하지 않음
	if (EntityTypes.IsEmpty())
	{
		UE_LOG(LogHellunaSpawner, Error,
			TEXT("[ExecuteDelayedSpawn] EntityTypes가 비어있음 — BP에서 EntityTypes를 설정했는지 확인하세요. Spawner: %s"), *GetName());
	}

	for (int32 i = 0; i < EntityTypes.Num(); ++i)
	{
		const bool bConfigNull = EntityTypes[i].EntityConfig.IsNull();
		const FString ConfigName = bConfigNull
			? TEXT("null")
			: EntityTypes[i].EntityConfig.ToSoftObjectPath().GetAssetName();

		if (bConfigNull)
		{
			UE_LOG(LogHellunaSpawner, Error,
				TEXT("[ExecuteDelayedSpawn] EntityTypes[%d].EntityConfig가 null — 해당 타입은 스폰되지 않음. Spawner: %s"),
				i, *GetName());
		}
		else
		{
			UE_LOG(LogHellunaSpawner, Log,
				TEXT("[ExecuteDelayedSpawn] EntityTypes[%d]: Config=%s | Proportion=%.2f"),
				i, *ConfigName, EntityTypes[i].Proportion);
		}
	}

	UE_LOG(LogHellunaSpawner, Log,
		TEXT("[ExecuteDelayedSpawn] 배치 스폰 실행 (이번 %d / 남은 %d / 총 %d, 타입 %d). Spawner: %s"),
		SpawnCountThisBatch, PendingSpawnCount, RequestedSpawnCount, EntityTypes.Num(), *GetName());

	DoSpawning();

	PendingSpawnCount -= SpawnCountThisBatch;

	UE_LOG(LogHellunaSpawner, Log,
		TEXT("[ExecuteDelayedSpawn] 배치 완료. 남은 수: %d. Spawner: %s"),
		PendingSpawnCount, *GetName());

	if (PendingSpawnCount > 0)
	{
		ScheduleNextSpawnBatch(SpawnBatchInterval, false);
	}
}

void AHellunaEnemyMassSpawner::ScheduleNextSpawnBatch(float Delay, bool bUseInitialDelayTimer)
{
	if (!GetWorld())
	{
		return;
	}

	FTimerHandle& TargetTimerHandle = bUseInitialDelayTimer ? DelayTimerHandle : SpawnBatchTimerHandle;
	GetWorldTimerManager().ClearTimer(TargetTimerHandle);

	if (Delay <= 0.f)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &AHellunaEnemyMassSpawner::ExecuteDelayedSpawn);
		return;
	}

	GetWorldTimerManager().SetTimer(TargetTimerHandle, this,
		&AHellunaEnemyMassSpawner::ExecuteDelayedSpawn,
		Delay, false);
}

float AHellunaEnemyMassSpawner::GetEstimatedSpawnSequenceSpacing(int32 InSpawnCount) const
{
	const int32 EffectiveSpawnCount = (InSpawnCount > 0) ? InSpawnCount : DefaultSpawnCount;
	if (EffectiveSpawnCount <= 0)
	{
		return 0.f;
	}

	if (SpawnBatchSize <= 0 || SpawnBatchInterval <= 0.f)
	{
		return 0.f;
	}

	const int32 BatchCount = FMath::Max(1, FMath::DivideAndRoundUp(EffectiveSpawnCount, SpawnBatchSize));
	return static_cast<float>(BatchCount) * SpawnBatchInterval;
}
