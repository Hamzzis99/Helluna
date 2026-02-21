/**
 * HellunaEnemyMassSpawner.h
 *
 * AMassSpawner 서브클래스.
 * - bAutoSpawnOnBeginPlay를 끄고, 시뮬레이션 시작 확인 후 지연 스폰
 * - 나중에 GameMode에서 "밤 조건" 시 DoSpawning() 호출 가능
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

protected:
	virtual void BeginPlay() override;

private:
	/** 시뮬레이션 시작 후 호출되는 콜백 */
	void OnSimulationReady(UWorld* InWorld);

	/** 실제 스폰 실행 (지연 후 호출) */
	void ExecuteDelayedSpawn();

	/** 스폰 전 지연 시간 (초). 시뮬레이션 시작 후 Processing Graph 빌드 대기 */
	UPROPERTY(EditAnywhere, Category = "Helluna|Spawn", meta = (DisplayName = "스폰 지연 시간 (초)"))
	float SpawnDelay = 2.0f;

	FDelegateHandle HellunaSimStartedHandle;

	FTimerHandle DelayTimerHandle;
};
