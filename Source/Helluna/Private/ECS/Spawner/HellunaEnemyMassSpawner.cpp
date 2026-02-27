#include "ECS/Spawner/HellunaEnemyMassSpawner.h"
#include "MassSimulationSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogHellunaSpawner);

AHellunaEnemyMassSpawner::AHellunaEnemyMassSpawner()
{
	bAutoSpawnOnBeginPlay = false;
}

void AHellunaEnemyMassSpawner::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogHellunaSpawner, Log, TEXT("[HellunaEnemyMassSpawner] BeginPlay — 자동스폰 OFF"));
}

// ============================================================
// RequestSpawn — 밤 시작 시 GameMode 에서 호출
// ============================================================
void AHellunaEnemyMassSpawner::RequestSpawn()
{
	if (GetWorld()->GetNetMode() == NM_Client) return;

	GetWorldTimerManager().ClearTimer(DelayTimerHandle);

	const UMassSimulationSubsystem* Sim = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld());
	if (Sim && Sim->IsSimulationStarted())
	{
		GetWorldTimerManager().SetTimer(DelayTimerHandle, this,
			&AHellunaEnemyMassSpawner::ExecuteDelayedSpawn, SpawnDelay, false);
	}
	else
	{
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
	GetWorldTimerManager().ClearTimer(DelayTimerHandle);

	if (HellunaSimStartedHandle.IsValid())
	{
		UMassSimulationSubsystem::GetOnSimulationStarted().Remove(HellunaSimStartedHandle);
		HellunaSimStartedHandle.Reset();
	}

	UE_LOG(LogHellunaSpawner, Log, TEXT("[HellunaEnemyMassSpawner] CancelPendingSpawn"));
}

// ============================================================
// OnSimulationReady — 시뮬레이션 준비 완료 콜백
// ============================================================
void AHellunaEnemyMassSpawner::OnSimulationReady(UWorld* InWorld)
{
	if (GetWorld() != InWorld) return;

	UMassSimulationSubsystem::GetOnSimulationStarted().Remove(HellunaSimStartedHandle);
	HellunaSimStartedHandle.Reset();

	GetWorldTimerManager().SetTimer(DelayTimerHandle, this,
		&AHellunaEnemyMassSpawner::ExecuteDelayedSpawn, SpawnDelay, false);
}

// ============================================================
// ExecuteDelayedSpawn — 실제 스폰 실행
// ============================================================
void AHellunaEnemyMassSpawner::ExecuteDelayedSpawn()
{
	DoSpawning();

	if (AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(
		UGameplayStatics::GetGameMode(GetWorld())))
	{
		const int32 SpawnCount = GetSpawnCount();
		GM->AddSpawnedCount(SpawnCount);
		UE_LOG(LogHellunaSpawner, Log,
			TEXT("[HellunaEnemyMassSpawner] DoSpawning 완료 — 소환 수: %d"), SpawnCount);
	}
}
