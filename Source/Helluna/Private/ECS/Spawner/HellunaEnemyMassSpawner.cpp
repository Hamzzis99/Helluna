#include "ECS/Spawner/HellunaEnemyMassSpawner.h"
#include "MassSimulationSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Kismet/GameplayStatics.h"
#include "DebugHelper.h"

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
// RequestSpawn — 밤 시작 시 GameMode에서 호출
// ============================================================
void AHellunaEnemyMassSpawner::RequestSpawn(int32 InSpawnCount)
{
	if (GetWorld()->GetNetMode() == NM_Client) return;

	if (DelayTimerHandle.IsValid() && GetWorldTimerManager().IsTimerActive(DelayTimerHandle))
	{
		UE_LOG(LogHellunaSpawner, Warning,
			TEXT("[RequestSpawn] ⚠️ 이전 타이머 활성 상태에서 재호출 — Spawner: %s"), *GetName());
	}

	// 소환 수 확정: 0 이하면 BP 기본값 사용
	RequestedSpawnCount = (InSpawnCount > 0) ? InSpawnCount : GetSpawnCount();

	GetWorldTimerManager().ClearTimer(DelayTimerHandle);

	const UMassSimulationSubsystem* Sim = UWorld::GetSubsystem<UMassSimulationSubsystem>(GetWorld());
	if (Sim && Sim->IsSimulationStarted())
	{
		UE_LOG(LogHellunaSpawner, Log,
			TEXT("[RequestSpawn] 시뮬레이션 준비됨 — %.1f초 후 %d마리 소환 예약. Spawner: %s"),
			SpawnDelay, RequestedSpawnCount, *GetName());

		GetWorldTimerManager().SetTimer(DelayTimerHandle, this,
			&AHellunaEnemyMassSpawner::ExecuteDelayedSpawn, SpawnDelay, false);
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
	const bool bCallbackWasBound = HellunaSimStartedHandle.IsValid();

	GetWorldTimerManager().ClearTimer(DelayTimerHandle);

	if (HellunaSimStartedHandle.IsValid())
	{
		UMassSimulationSubsystem::GetOnSimulationStarted().Remove(HellunaSimStartedHandle);
		HellunaSimStartedHandle.Reset();
	}

	UE_LOG(LogHellunaSpawner, Log,
		TEXT("[CancelPendingSpawn] Spawner: %s | 타이머 취소: %s | 콜백 해제: %s"),
		*GetName(),
		bTimerWasActive    ? TEXT("YES ⚠️") : TEXT("no"),
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

	GetWorldTimerManager().SetTimer(DelayTimerHandle, this,
		&AHellunaEnemyMassSpawner::ExecuteDelayedSpawn, SpawnDelay, false);
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

	// AMassSpawner::Count는 protected이지만 파생 클래스에서 직접 접근 가능.
	// ScaleSpawningCount 방식은 BP 기본값에 의존하므로
	// Count를 직접 덮어써서 정확한 수를 보장한다.
	if (RequestedSpawnCount > 0)
	{
		Count = RequestedSpawnCount;
		ScaleSpawningCount(1.0f); // Scale을 1로 초기화 (이전 Scale 잔재 제거)

		Debug::Print(FString::Printf(TEXT("[ExecuteDelayedSpawn] Count=%d 설정. Spawner: %s"),
			Count, *GetName()), FColor::Cyan);
	}

	// EntityTypes 상태 진단 — BP에 설정된 타입 수와 Proportion 확인
	Debug::Print(FString::Printf(TEXT("[ExecuteDelayedSpawn] EntityTypes:%d | Count:%d | Spawner:%s"),
		EntityTypes.Num(), Count, *GetName()), FColor::Yellow);
	for (int32 i = 0; i < EntityTypes.Num(); ++i)
	{
		const FString ConfigName = EntityTypes[i].EntityConfig.IsNull()
			? TEXT("null")
			: EntityTypes[i].EntityConfig.ToSoftObjectPath().GetAssetName();
		Debug::Print(FString::Printf(TEXT("  [%d] EntityConfig:%s | Proportion:%.2f"),
			i, *ConfigName, EntityTypes[i].Proportion), FColor::Yellow);
	}

	UE_LOG(LogHellunaSpawner, Log,
		TEXT("[ExecuteDelayedSpawn] DoSpawning 실행 (%d마리 x %d타입). Spawner: %s"),
		RequestedSpawnCount, EntityTypes.Num(), *GetName());

	DoSpawning();

	UE_LOG(LogHellunaSpawner, Log,
		TEXT("[ExecuteDelayedSpawn] 완료. Spawner: %s"), *GetName());
}
