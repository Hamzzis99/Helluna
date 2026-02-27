// ════════════════════════════════════════════════════════════════════════════════
// HellunaDefenseGameMode.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 게임 로직 전용 GameMode
// 로그인/인벤토리 시스템은 HellunaBaseGameMode에서 상속
//
// 🎮 이 클래스의 역할:
//    - InitializeGame() : 게임 시작 (BaseGameMode에서 호출됨)
//    - EnterDay() / EnterNight() : 낮밤 전환
//    - SpawnTestMonsters() : 몬스터 스폰
//    - TrySummonBoss() : 보스 소환
//
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaDefenseGameMode.generated.h"

class ATargetPoint;
class AHellunaEnemyMassSpawner;

UCLASS()
class HELLUNA_API AHellunaDefenseGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()

public:
	AHellunaDefenseGameMode();

protected:
	virtual void BeginPlay() override;

public:
	// ════════════════════════════════════════════════════════════════════════════════
	// 게임 초기화 (BaseGameMode의 virtual 함수 override)
	// ════════════════════════════════════════════════════════════════════════════════

	/**
	 * 게임 초기화 - 첫 플레이어 캐릭터 소환 후 자동 호출됨
	 *
	 * 이 함수가 호출되면:
	 * - 게임이 본격적으로 시작됨
	 * - EnterDay()가 호출되어 낮/밤 사이클 시작
	 */
	virtual void InitializeGame() override;

	/** 게임 재시작 (AGameMode override) */
	virtual void RestartGame() override;

	// ════════════════════════════════════════════════════════════════════════════════
	// 낮/밤 사이클 시스템
	// ════════════════════════════════════════════════════════════════════════════════
protected:
	FTimerHandle TimerHandle_ToNight;
	FTimerHandle TimerHandle_ToDay;

	/** 낮 지속 시간 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|DayNight(낮밤)",
		meta = (DisplayName = "낮 지속 시간(초)"))
	float TestDayDuration = 10.f;

	/** 밤 실패 후 낮으로 돌아가는 딜레이 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|DayNight(낮밤)",
		meta = (DisplayName = "밤→낮 전환 딜레이(초)"))
	float TestNightFailToDayDelay = 5.f;

	/** 낮 시작 */
	void EnterDay();

	/** 밤 시작 */
	void EnterNight();

	/** 우주선 수리 완료 여부 체크 */
	bool IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const;

	// ════════════════════════════════════════════════════════════════════════════════
	// 몬스터 스폰 시스템
	// ════════════════════════════════════════════════════════════════════════════════
protected:
	/** 살아있는 몬스터 목록 */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> AliveMonsters;

	/** 이번 밤에 소환된 총 몬스터 수 (DoSpawning 시 저장, 사망마다 1 차감) */
	int32 TotalSpawnedThisNight = 0;

	/** 현재 남은 몬스터 수 (TotalSpawnedThisNight 에서 차감) */
	int32 RemainingMonstersThisNight = 0;

	/**
	 * 런타임에 동적 생성된 MassSpawner 목록.
	 * EnterNight() 최초 호출 시 MonsterSpawnTag 를 가진 TargetPoint 위치에
	 * SpawnActor 로 AHellunaEnemyMassSpawner 를 생성하고 여기에 캐싱한다.
	 * 이후 밤마다 DoSpawning() 을 재호출해 재사용한다.
	 */
	UPROPERTY()
	TArray<TObjectPtr<AHellunaEnemyMassSpawner>> CachedMassSpawners;

public:
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Monster(몬스터)")
	void RegisterAliveMonster(AActor* Monster);

	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Monster(몬스터)")
	void NotifyMonsterDied(AActor* DeadMonster);

	UFUNCTION(BlueprintPure, Category = "Defense(게임)|Monster(몬스터)")
	int32 GetAliveMonsterCount() const { return AliveMonsters.Num(); }

	/**
	 * MassSpawner 가 DoSpawning() 완료 후 호출.
	 * 이번 밤 총 소환 수에 스폰된 수를 누적한다.
	 */
	void AddSpawnedCount(int32 Count);

protected:
	/**
	 * 스폰할 MassSpawner 블루프린트 클래스.
	 * AHellunaEnemyMassSpawner 를 부모로 만든 BP 를 에디터에서 설정한다.
	 * (BP 안에 MassSpawner EntityTypes, SpawnCount 등을 설정해 두면 된다.)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "MassSpawner 클래스",
			ToolTip = "밤에 TargetPoint 위치마다 동적으로 생성할 MassSpawner 블루프린트입니다.\nHellunaEnemyMassSpawner 를 부모로 만든 BP 를 설정하세요."))
	TSubclassOf<AHellunaEnemyMassSpawner> MassSpawnerClass;

	/**
	 * MassSpawner 를 생성할 TargetPoint 태그.
	 * 레벨에 배치한 TargetPoint 액터에 이 태그를 붙이면
	 * 첫 번째 EnterNight() 에서 해당 위치에 MassSpawner 가 자동 생성된다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Monster(몬스터)",
		meta = (DisplayName = "몬스터 스폰 포인트 태그",
			ToolTip = "MassSpawner 를 생성할 위치를 나타내는 TargetPoint 의 태그입니다."))
	FName MonsterSpawnTag = TEXT("MonsterSpawn");

	/** 몬스터 스폰 포인트 TargetPoint 캐싱 (BeginPlay 에서 호출) */
	void CacheMonsterSpawnPoints();

	/**
	 * 첫 번째 밤에 TargetPoint 위치마다 MassSpawner 를 동적 생성.
	 * 이후 밤에는 이미 생성된 MassSpawner 에 DoSpawning() 만 재호출.
	 */
	void TriggerMassSpawning();

	/** 스폰 포인트 TargetPoint 목록 (BeginPlay 에서 캐싱) */
	UPROPERTY()
	TArray<ATargetPoint*> MonsterSpawnPoints;

	// ════════════════════════════════════════════════════════════════════════════════
	// 보스 스폰 시스템
	// ════════════════════════════════════════════════════════════════════════════════
public:
	UFUNCTION(BlueprintCallable, Category = "Defense(게임)|Boss(보스)")
	void SetBossReady(bool bReady);

protected:
	/** 보스 클래스 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense(게임)|Boss(보스)",
		meta = (DisplayName = "보스 클래스"))
	TSubclassOf<APawn> BossClass;

	/** 보스 스폰 포인트 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Boss(보스)")
	FName BossSpawnPointTag = TEXT("BossSpawn");

	/** 보스 스폰 Z 오프셋 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense(게임)|Boss(보스)")
	float SpawnZOffset = 150.f;

	/** 보스 준비 상태 */
	UPROPERTY(BlueprintReadOnly, Category = "Defense(게임)|Boss(보스)")
	bool bBossReady = false;

	/** 보스 스폰 포인트 */
	UPROPERTY()
	TArray<ATargetPoint*> BossSpawnPoints;

	void CacheBossSpawnPoints();
	void TrySummonBoss();
};
