// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaDefenseGameMode.generated.h"


class ATargetPoint;
class UHellunaAccountSaveGame;
class AHellunaPlayerState;

/**
 * ============================================
 * 📌 HellunaDefenseGameMode
 * 
 * [Phase B 역할]:
 * - 로그인 처리 (계정 검증, 동시접속 체크)
 * - 로그인 성공 시 HellunaHeroCharacter 소환
 * - 로그인 전에는 DefaultPawn 상태
 * - 캐릭터 선택창 (TODO: 구현 예정)
 * - 게임 진행 (낮/밤, 몬스터, 보스 등)
 * 
 * [플레이어 접속 흐름]:
 * 1. LoginLevel에서 IP 입력 → 서버 접속
 * 2. GihyeonMap으로 이동 (SeamlessTravel 또는 직접 접속)
 * 3. PostLogin() 호출 → DefaultPawn 상태로 시작
 * 4. 클라이언트에 로그인 UI 표시 요청
 * 5. 로그인 성공 → HellunaHeroCharacter 소환
 * 6. (TODO) 캐릭터 선택창 표시
 * 7. 게임 시작!
 * ============================================
 */
UCLASS()
class HELLUNA_API AHellunaDefenseGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()
	
public:
	AHellunaDefenseGameMode();

	virtual void BeginPlay() override;

	// ============================================
	// 📌 플레이어 접속/로그아웃 처리
	// ============================================

	/**
	 * 플레이어 접속 시 호출
	 * - DefaultPawn 상태로 시작
	 * - 로그인 UI 표시 요청
	 */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/**
	 * 플레이어 로그아웃 시 호출
	 * - GameInstance에서 로그인 정보 제거
	 */
	virtual void Logout(AController* Exiting) override;

	/**
	 * SeamlessTravel 플레이어 처리
	 * - PlayerState 로그인 정보 복원
	 */
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	// ============================================
	// 📌 [Phase B] 로그인 시스템
	// ============================================

	/**
	 * 로그인 요청 처리 (서버에서만 호출)
	 * @param PlayerController - 요청한 플레이어 컨트롤러
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password);

	/**
	 * 동시 접속 여부 확인
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	// ============================================
	// 📌 게임 기능
	// ============================================

	UFUNCTION(BlueprintCallable, Category = "Defense|Restart")
	void RestartGame();

	UFUNCTION(BlueprintCallable, Category = "Defense|Boss")
	void SetBossReady(bool bReady);

protected:
	// ============================================
	// 📌 [Phase B] 로그인 관련 설정
	// ============================================

	/** 
	 * 로그인 성공 후 소환할 캐릭터 클래스
	 * Blueprint에서 설정 (BP_HellunaHeroCharacter)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login|Character", meta = (DisplayName = "히어로 캐릭터 클래스"))
	TSubclassOf<APawn> HeroCharacterClass;

	/** 계정 데이터 (SaveGame) */
	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;

	/** 로그인 대기 타이머 (타임아웃용) */
	UPROPERTY()
	TMap<APlayerController*, FTimerHandle> LoginTimeoutTimers;

	/** 로그인 타임아웃 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "로그인 타임아웃 (초)"))
	float LoginTimeoutSeconds = 60.0f;

	// ============================================
	// 📌 [Phase B] 로그인 내부 함수
	// ============================================

	/** 로그인 성공 처리 */
	void OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId);

	/** 로그인 실패 처리 */
	void OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage);

	/** 로그인 타임아웃 처리 */
	void OnLoginTimeout(APlayerController* PlayerController);

	/**
	 * ============================================
	 * 📌 [Phase B] 히어로 캐릭터 소환
	 * 
	 * 로그인 성공 후 DefaultPawn → HeroCharacter로 교체
	 * 
	 * [TODO: 캐릭터 선택창 구현 시 이 함수 수정 필요]
	 * 
	 * 현재 흐름:
	 * 1. 로그인 성공
	 * 2. SpawnHeroCharacter() 호출
	 * 3. HeroCharacter 스폰
	 * 4. 기존 Pawn 제거
	 * 5. HeroCharacter로 Possess
	 * 
	 * 캐릭터 선택창 구현 후 흐름:
	 * 1. 로그인 성공
	 * 2. 클라이언트에 캐릭터 선택 UI 표시
	 * 3. 플레이어가 캐릭터 선택
	 * 4. Server RPC로 선택 정보 전송
	 * 5. SpawnHeroCharacter(SelectedCharacterClass) 호출
	 * 6. 선택한 캐릭터 스폰 및 Possess
	 * ============================================
	 */
	void SpawnHeroCharacter(APlayerController* PlayerController);

	// ============================================
	// 📌 보스 관련
	// ============================================

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

	// ============================================
	// 📌 낮/밤 사이클
	// ============================================

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

	// ============================================
	// 📌 몬스터 관련
	// ============================================

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
