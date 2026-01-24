// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaDefenseGameMode.generated.h"


class ATargetPoint;
/**
 * 
 */

UCLASS()
class HELLUNA_API AHellunaDefenseGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()
	
public:
	AHellunaDefenseGameMode();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "Defense|Restart")
	void RestartGame();

	UFUNCTION(BlueprintCallable, Category = "Defense|Boss")
	void SetBossReady(bool bReady);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	TSubclassOf<APawn> BossClass;

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	FName BossSpawnPointTag = TEXT("BossSpawn");

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	float SpawnZOffset = 150.f;

	UPROPERTY(BlueprintReadOnly, Category = "Defense|Boss")
	bool bBossReady = false;

	UPROPERTY()
	TArray<ATargetPoint*> BossSpawnPoints;

	//===========================================================
	FTimerHandle TimerHandle_ToNight;
	FTimerHandle TimerHandle_ToDay;

	UPROPERTY(EditDefaultsOnly, Category = "Defense|DayNight")
	float TestDayDuration = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "Defense|DayNight")
	float TestNightFailToDayDelay = 5.f;

	void EnterDay();
	void EnterNight();

	bool IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const;


	void CacheBossSpawnPoints();
	void TrySummonBoss();

	//==============몬스터 개체 수 조건으로 낮, 밤 전환================

protected:
	// 살아있는 몬스터 수
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> AliveMonsters;

public:
	// ✅ 몬스터가 스폰될 때 “살아있는 몬스터”로 등록
	UFUNCTION(BlueprintCallable, Category = "Defense|Monster")
	void RegisterAliveMonster(AActor* Monster);

	// - AliveMonsters에서 제거하고, 0이면 낮 전환 예약
	UFUNCTION(BlueprintCallable, Category = "Defense|Monster")
	void NotifyMonsterDied(AActor* DeadMonster);

	// ✅ 디버그/확인용(서버에서만 의미가 큼)
	UFUNCTION(BlueprintPure, Category = "Defense|Monster")
	int32 GetAliveMonsterCount() const { return AliveMonsters.Num(); }

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Monster|Test")
	TSubclassOf<APawn> TestMonsterClass;

	// ✅ 몬스터 스폰 위치로 쓸 TargetPoint 태그
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Monster|Test")
	FName MonsterSpawnPointTag = TEXT("MonsterSpawn");

	// ✅ 밤마다 소환할 몬스터 수 (기본 3)
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Monster|Test", meta = (ClampMin = "0"))
	int32 TestMonsterSpawnCount = 3;

	// ✅ 스폰 포인트 목록 캐싱
	UPROPERTY()
	TArray<ATargetPoint*> MonsterSpawnPoints;

	void CacheMonsterSpawnPoints();
	void SpawnTestMonsters();

};
