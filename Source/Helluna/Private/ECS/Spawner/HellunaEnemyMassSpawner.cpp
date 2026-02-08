/**
 * HellunaEnemyMassSpawner.cpp
 *
 * 시뮬레이션 시작 확인 후 지연 스폰하는 MassSpawner 서브클래스.
 * Processing Graph가 완전히 빌드된 후 엔티티를 생성하여
 * Processor의 Execute가 정상 호출되도록 보장한다.
 */

#include "ECS/Spawner/HellunaEnemyMassSpawner.h"
#include "MassSimulationSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogHellunaSpawner);

AHellunaEnemyMassSpawner::AHellunaEnemyMassSpawner()
{
	// ★ 부모의 자동 스폰을 끈다!
	// 부모 AMassSpawner::BeginPlay()에서 bAutoSpawnOnBeginPlay가 false이면
	// DoSpawning()을 호출하지 않는다.
	bAutoSpawnOnBeginPlay = false;
}

void AHellunaEnemyMassSpawner::BeginPlay()
{
	// 부모 BeginPlay 호출 (bAutoSpawnOnBeginPlay == false이므로 스폰 안 함)
	Super::BeginPlay();

	UE_LOG(LogHellunaSpawner, Warning, TEXT("[HellunaSpawner] BeginPlay - 자동스폰 OFF, 시뮬레이션 대기 시작"));

	// 서버/Standalone에서만 스폰
	const ENetMode NetMode = GetWorld()->GetNetMode();
	if (NetMode == NM_Client)
	{
		UE_LOG(LogHellunaSpawner, Log, TEXT("[HellunaSpawner] 클라이언트 → 스폰 스킵"));
		return;
	}

	// 시뮬레이션이 이미 시작되었는지 확인
	const UMassSimulationSubsystem* SimSubsystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld());
	if (SimSubsystem && SimSubsystem->IsSimulationStarted())
	{
		UE_LOG(LogHellunaSpawner, Warning, TEXT("[HellunaSpawner] 시뮬레이션 이미 시작됨 → 지연 스폰 예약 (%.1f초)"), SpawnDelay);
		GetWorldTimerManager().SetTimer(DelayTimerHandle, this,
			&AHellunaEnemyMassSpawner::ExecuteDelayedSpawn, SpawnDelay, false);
	}
	else
	{
		UE_LOG(LogHellunaSpawner, Warning, TEXT("[HellunaSpawner] 시뮬레이션 미시작 → OnSimulationStarted 콜백 등록"));
		HellunaSimStartedHandle = UMassSimulationSubsystem::GetOnSimulationStarted().AddUObject(
			this, &AHellunaEnemyMassSpawner::OnSimulationReady);
	}
}

void AHellunaEnemyMassSpawner::OnSimulationReady(UWorld* InWorld)
{
	if (GetWorld() != InWorld)
	{
		return;
	}

	UE_LOG(LogHellunaSpawner, Warning, TEXT("[HellunaSpawner] 시뮬레이션 시작 감지! → 지연 스폰 예약 (%.1f초)"), SpawnDelay);

	// 콜백 해제
	UMassSimulationSubsystem::GetOnSimulationStarted().Remove(HellunaSimStartedHandle);
	HellunaSimStartedHandle.Reset();

	// 지연 후 스폰 (Processing Graph 빌드 시간 확보)
	GetWorldTimerManager().SetTimer(DelayTimerHandle, this,
		&AHellunaEnemyMassSpawner::ExecuteDelayedSpawn, SpawnDelay, false);
}

void AHellunaEnemyMassSpawner::ExecuteDelayedSpawn()
{
	UE_LOG(LogHellunaSpawner, Warning, TEXT("╔═══════════════════════════════════════════════╗"));
	UE_LOG(LogHellunaSpawner, Warning, TEXT("║  [HellunaSpawner] DoSpawning 호출!            ║"));
	UE_LOG(LogHellunaSpawner, Warning, TEXT("╚═══════════════════════════════════════════════╝"));

	// 시뮬레이션 상태 확인 로그
	const UMassSimulationSubsystem* SimSubsystem = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld());
	UE_LOG(LogHellunaSpawner, Warning, TEXT("  SimSubsystem: %s, IsStarted: %s"),
		SimSubsystem ? TEXT("Valid") : TEXT("NULL"),
		(SimSubsystem && SimSubsystem->IsSimulationStarted()) ? TEXT("YES ✅") : TEXT("NO ❌"));

	// EntityManager 상태 확인
	const UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(GetWorld());
	UE_LOG(LogHellunaSpawner, Warning, TEXT("  EntitySubsystem: %s"),
		EntitySubsystem ? TEXT("Valid ✅") : TEXT("NULL ❌"));

	// 부모의 DoSpawning 호출 (엔티티 실제 생성)
	DoSpawning();

	UE_LOG(LogHellunaSpawner, Warning, TEXT("[HellunaSpawner] DoSpawning 완료!"));
}
