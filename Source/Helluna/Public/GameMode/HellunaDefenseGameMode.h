// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaDefenseGameMode.generated.h"


class ATargetPoint;
/**
 * 
 */

UENUM(BlueprintType)
enum class EDefenseGameModeState : uint8
{
	Waiting,     // 아직 아무것도 안 함
	Running,     // 조건 체크 중
	BossSpawned, // 보스 소환됨
	Lose,      // 패배
	Victory      // (필요하면) 승리
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDefenseStateChanged, EDefenseGameModeState, OldState, EDefenseGameModeState, NewState);

UCLASS()
class HELLUNA_API AHellunaDefenseGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()
	
public:
	AHellunaDefenseGameMode();

	virtual void BeginPlay() override;

	// ===== 외부에서 "조건 달성" 신호 주기 =====
	UFUNCTION(BlueprintCallable, Category = "Defense|Condition")
	void SetBossReady(bool bReady);

	UFUNCTION(BlueprintCallable, Category = "Defense|Condition")
	void TriggerLose();

	// 원하면 “승리”도 쓸 수 있게
	UFUNCTION(BlueprintCallable, Category = "Defense|Condition")
	void TriggerVictory();

	// 상태
	UPROPERTY(BlueprintReadOnly, Category = "Defense|State")
	EDefenseGameModeState CurrentState = EDefenseGameModeState::Waiting;

	UPROPERTY(BlueprintAssignable, Category = "Defense|State")
	FOnDefenseStateChanged OnDefenseStateChanged;

protected:
	// ===== 보스 설정 =====
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	TSubclassOf<APawn> BossClass;

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	FName BossSpawnPointTag = TEXT("BossSpawn");

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	float SpawnZOffset = 150.f;

	// ===== 조건 체크 =====
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Condition")
	float ConditionCheckInterval = 0.2f;

	// "보스 소환 가능한 상태" 플래그(트리거나 카운터 등으로 바꿔치기)
	UPROPERTY(BlueprintReadOnly, Category = "Defense|Condition")
	bool bBossReady = false;

	float ElapsedSeconds = 0.f;

	FTimerHandle TimerHandle_ConditionCheck;

	// 스폰포인트 캐시
	UPROPERTY()
	TArray<ATargetPoint*> BossSpawnPoints;

	// ===== 내부 로직 =====
	void SetState(EDefenseGameModeState NewState);

	void CacheBossSpawnPoints();

	void EvaluateConditions();

	bool CanSummonBoss() const;
	void TrySummonBoss();

	void RestartCurrentLevelForAll();
	
	
};
