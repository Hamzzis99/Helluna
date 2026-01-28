#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaDefenseGameMode.generated.h"

class ATargetPoint;
class UHellunaAccountSaveGame;
class AHellunaPlayerState;
class AHellunaLoginController;

UCLASS()
class HELLUNA_API AHellunaDefenseGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()
	
public:
	AHellunaDefenseGameMode();
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	UFUNCTION(BlueprintCallable, Category = "Login")
	void ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	UFUNCTION(BlueprintCallable, Category = "Defense|Restart")
	void RestartGame();

	UFUNCTION(BlueprintCallable, Category = "Defense|Boss")
	void SetBossReady(bool bReady);

	// ============================================
	// 📌 게임 초기화 (캐릭터 소환 후 호출)
	// ============================================
	
	/** 게임이 초기화되었는지 여부 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Defense")
	bool IsGameInitialized() const { return bGameInitialized; }

	/** 게임 초기화 (첫 플레이어 캐릭터 소환 후 호출) */
	UFUNCTION(BlueprintCallable, Category = "Defense")
	void InitializeGame();

protected:
	/** 로그인 성공 후 소환할 캐릭터 */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "히어로 캐릭터 클래스"))
	TSubclassOf<APawn> HeroCharacterClass;

	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;

	UPROPERTY()
	TMap<APlayerController*, FTimerHandle> LoginTimeoutTimers;

	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "로그인 타임아웃 (초)"))
	float LoginTimeoutSeconds = 60.0f;

	/** 게임 초기화 여부 (첫 플레이어 캐릭터 소환 후 true) */
	UPROPERTY(BlueprintReadOnly, Category = "Defense")
	bool bGameInitialized = false;

	void OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId);
	void OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage);
	void OnLoginTimeout(APlayerController* PlayerController);

	/** HeroCharacter 소환 및 Possess */
	void SpawnHeroCharacter(APlayerController* PlayerController);

	// 보스 관련
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

	// 낮/밤 사이클
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

	// 몬스터 관련
protected:
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> AliveMonsters;

public:
	UFUNCTION(BlueprintCallable, Category = "Defense|Monster")
	void RegisterAliveMonster(AActor* Monster);

	UFUNCTION(BlueprintCallable, Category = "Defense|Monster")
	void NotifyMonsterDied(AActor* DeadMonster);

	UFUNCTION(BlueprintPure, Category = "Defense|Monster")
	int32 GetAliveMonsterCount() const { return AliveMonsters.Num(); }

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Monster|Test")
	TSubclassOf<APawn> TestMonsterClass;

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Monster|Test")
	FName MonsterSpawnPointTag = TEXT("MonsterSpawn");

	UPROPERTY(EditDefaultsOnly, Category = "Defense|Monster|Test", meta = (ClampMin = "0"))
	int32 TestMonsterSpawnCount = 3;

	UPROPERTY()
	TArray<ATargetPoint*> MonsterSpawnPoints;

	void CacheMonsterSpawnPoints();
	void SpawnTestMonsters();
};
