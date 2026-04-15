/**
 * HellunaEnemyMassSpawner.h
 *
 * AMassSpawner 서브클래스.
 * - bAutoSpawnOnBeginPlay를 끄고, 시뮬레이션 시작 확인 후 지연 스폰
 * - GameMode에서 RequestSpawn(Count) 호출 시 소환 수를 직접 지정
 * - 디버그 로그로 스폰 성공/실패 추적
 */

#pragma once

#include "CoreMinimal.h"
#include "MassSpawner.h"
#include "HellunaEnemyMassSpawner.generated.h"

class UNavigationInvokerComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogHellunaSpawner, Log, All);

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Helluna Enemy Mass Spawner"))
class HELLUNA_API AHellunaEnemyMassSpawner : public AMassSpawner
{
	GENERATED_BODY()

public:
	AHellunaEnemyMassSpawner();

	/**
	 * GameMode에서 밤 시작 시 호출하는 스폰 트리거.
	 * @param InSpawnCount  이번 밤에 소환할 엔티티 수.
	 *                      0 이하면 BP에 설정된 기본값(EntityTypes Count) 사용.
	 * @param AdditionalStartDelay  기본 SpawnDelay 뒤에 추가로 더할 지연 시간(초).
	 */
	void RequestSpawn(int32 InSpawnCount = 0, float AdditionalStartDelay = 0.f);

	/** 대기 중인 스폰 타이머/콜백 취소 (낮 전환 시 GameMode에서 호출) */
	void CancelPendingSpawn();

	/** 이번 RequestSpawn에서 실제로 소환할 수 (GameMode가 읽어서 TotalSpawned에 누적) */
	int32 GetRequestedSpawnCount() const { return RequestedSpawnCount; }

	/** 아직 배출되지 않은 잔여 스폰 수 (NightWatchdog 보정용) */
	int32 GetPendingSpawnCount() const { return PendingSpawnCount; }

	/** 디버그용: EntityTypes 수 반환 */
	int32 GetEntityTypesNum() const { return EntityTypes.Num(); }

	/** 현재 배치 스폰 간격 */
	float GetSpawnBatchInterval() const { return SpawnBatchInterval; }

	/** 지정 수량을 이 스포너가 모두 배출하는 데 걸리는 예상 시간(초). SpawnDelay는 제외한다. */
	float GetEstimatedSpawnSequenceSpacing(int32 InSpawnCount) const;

protected:
	virtual void BeginPlay() override;

private:
	void OnSimulationReady(UWorld* InWorld);
	void ExecuteDelayedSpawn();
	void ScheduleNextSpawnBatch(float Delay, bool bUseInitialDelayTimer);

	/** 스폰 전 지연 시간 (초) */
	UPROPERTY(EditAnywhere, Category = "Helluna|Spawn", meta = (DisplayName = "스폰 지연 시간 (초)"))
	float SpawnDelay = 2.0f;

	/** 한 번에 생성할 ECS 엔티티 수. 0 이하면 한 번에 모두 생성한다. */
	UPROPERTY(EditAnywhere, Category = "Helluna|Spawn", meta = (DisplayName = "배치당 스폰 수", ClampMin = "0"))
	int32 SpawnBatchSize = 5;

	/** 배치 스폰 간격 (초). */
	UPROPERTY(EditAnywhere, Category = "Helluna|Spawn", meta = (DisplayName = "배치 스폰 간격 (초)", ClampMin = "0.0"))
	float SpawnBatchInterval = 1.0f;

	/** RequestSpawn 호출 시 확정된 소환 수. ExecuteDelayedSpawn에서 사용 */
	int32 RequestedSpawnCount = 0;

	/** 아직 생성되지 않은 잔여 엔티티 수. */
	int32 PendingSpawnCount = 0;

	/** BeginPlay 시점의 기본 Count 캐시. RequestSpawn(0) 폴백에 사용. */
	int32 DefaultSpawnCount = 0;

	/** 현재 요청에 예약된 실제 시작 지연 시간. 시뮬레이션 준비 콜백 이후에도 유지한다. */
	float PendingInitialDelay = 0.f;

	FDelegateHandle HellunaSimStartedHandle;
	FTimerHandle DelayTimerHandle;
	FTimerHandle SpawnBatchTimerHandle;

	// ★ 스포너 주변 NavMesh 스트리밍 강제 생성
	UPROPERTY(VisibleAnywhere, Category = "Navigation")
	TObjectPtr<UNavigationInvokerComponent> NavInvoker;
};
