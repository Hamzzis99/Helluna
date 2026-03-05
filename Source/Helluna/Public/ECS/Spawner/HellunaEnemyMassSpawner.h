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
	 */
	void RequestSpawn(int32 InSpawnCount = 0);

	/** 대기 중인 스폰 타이머/콜백 취소 (낮 전환 시 GameMode에서 호출) */
	void CancelPendingSpawn();

	/** 이번 RequestSpawn에서 실제로 소환할 수 (GameMode가 읽어서 TotalSpawned에 누적) */
	int32 GetRequestedSpawnCount() const { return RequestedSpawnCount; }

	/** 디버그용: EntityTypes 수 반환 */
	int32 GetEntityTypesNum() const { return EntityTypes.Num(); }

protected:
	virtual void BeginPlay() override;

private:
	void OnSimulationReady(UWorld* InWorld);
	void ExecuteDelayedSpawn();

	/** 스폰 전 지연 시간 (초) */
	UPROPERTY(EditAnywhere, Category = "Helluna|Spawn", meta = (DisplayName = "스폰 지연 시간 (초)"))
	float SpawnDelay = 2.0f;

	/** RequestSpawn 호출 시 확정된 소환 수. ExecuteDelayedSpawn에서 사용 */
	int32 RequestedSpawnCount = 0;

	FDelegateHandle HellunaSimStartedHandle;
	FTimerHandle DelayTimerHandle;
};
