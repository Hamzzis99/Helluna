#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "HellunaDefenseGameMode.generated.h"

class ATargetPoint;
class UHellunaAccountSaveGame;
class AHellunaPlayerState;
class AHellunaLoginController;

/**
 * ============================================
 * 📌 HellunaDefenseGameMode
 * ============================================
 * 
 * 메인 게임 맵(GihyeonMap)의 GameMode
 * 로그인 처리 + 낮/밤 사이클 + 몬스터/보스 스폰 담당
 * 
 * ============================================
 * 📌 로그인 시스템 (이 GameMode의 핵심 역할)
 * ============================================
 * 
 * [플레이어 접속 시]
 * 1. PostLogin() 호출
 *    ├─ 이미 로그인됨 (SeamlessTravel) → 바로 캐릭터 소환
 *    └─ 미로그인 → 로그인 타임아웃 타이머 시작 (60초)
 * 
 * [로그인 처리]
 * 2. ProcessLogin() - LoginController에서 RPC로 호출
 *    ├─ 동시 접속 체크 (GameInstance)
 *    ├─ 계정 검증 (AccountSaveGame)
 *    └─ OnLoginSuccess() 또는 OnLoginFailed()
 * 
 * [로그인 성공]
 * 3. OnLoginSuccess()
 *    ├─ GameInstance->RegisterLogin() : 접속자 목록 추가
 *    ├─ PlayerState->SetLoginInfo() : ID 저장
 *    └─ SwapToGameController() 예약 (0.5초 후)
 * 
 * [Controller 교체]
 * 4. SwapToGameController()
 *    ├─ LoginController 파괴
 *    ├─ GameController 생성
 *    └─ SpawnHeroCharacter() 호출
 * 
 * [캐릭터 소환]
 * 5. SpawnHeroCharacter()
 *    ├─ HeroCharacter 스폰
 *    ├─ Controller에 Possess
 *    └─ 첫 플레이어면 InitializeGame() 호출
 * 
 * [로그아웃]
 * 6. Logout() - 플레이어 연결 끊김 시 자동 호출
 *    ├─ PlayerState->ClearLoginInfo()
 *    └─ GameInstance->RegisterLogout()
 * 
 * ============================================
 * 📌 BP 설정 필수 항목:
 * ============================================
 * - HeroCharacterClass : 로그인 후 소환할 캐릭터 클래스
 * - LoginTimeoutSeconds : 로그인 타임아웃 (기본 60초)
 * 
 * 📌 작성자: Gihyeon
 */
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

	// ============================================
	// 📌 로그인 시스템 (공개 함수)
	// ============================================
	
	/**
	 * 로그인 처리 (LoginController::Server_RequestLogin에서 호출)
	 * 
	 * @param PlayerController - 로그인 요청한 Controller
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 * 
	 * 내부 동작:
	 * 1. 동시 접속 체크 (GameInstance)
	 * 2. 계정 검증/생성 (AccountSaveGame)
	 * 3. OnLoginSuccess() 또는 OnLoginFailed() 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password);

	/**
	 * 특정 플레이어가 현재 접속 중인지 확인
	 * 
	 * @param PlayerId - 확인할 플레이어 ID
	 * @return true면 접속 중
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

	// ============================================
	// 📌 게임 시스템 (비로그인 관련)
	// ============================================
	
	UFUNCTION(BlueprintCallable, Category = "Defense|Restart")
	void RestartGame();

	UFUNCTION(BlueprintCallable, Category = "Defense|Boss")
	void SetBossReady(bool bReady);

	/** 게임이 초기화되었는지 여부 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Defense")
	bool IsGameInitialized() const { return bGameInitialized; }

	/** 게임 초기화 (첫 플레이어 캐릭터 소환 후 호출) */
	UFUNCTION(BlueprintCallable, Category = "Defense")
	void InitializeGame();

protected:
	// ============================================
	// 📌 로그인 관련 변수 (BP 설정)
	// ============================================
	
	/** 
	 * 로그인 성공 후 소환할 캐릭터 클래스
	 * BP에서 BP_HeroCharacter 등으로 설정
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "히어로 캐릭터 클래스"))
	TSubclassOf<APawn> HeroCharacterClass;

	/** 
	 * 계정 데이터 (BeginPlay에서 로드)
	 * 아이디/비밀번호 저장
	 */
	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;

	/** 
	 * 로그인 타임아웃 타이머 맵
	 * Key: PlayerController, Value: 타이머 핸들
	 */
	UPROPERTY()
	TMap<APlayerController*, FTimerHandle> LoginTimeoutTimers;

	/** 
	 * 로그인 타임아웃 시간 (초)
	 * 이 시간 내에 로그인하지 않으면 킥
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "로그인 타임아웃 (초)"))
	float LoginTimeoutSeconds = 60.0f;

	/** 게임 초기화 여부 (첫 플레이어 캐릭터 소환 후 true) */
	UPROPERTY(BlueprintReadOnly, Category = "Defense")
	bool bGameInitialized = false;

	// ============================================
	// 📌 로그인 내부 함수
	// ============================================
	
	/**
	 * 로그인 성공 처리
	 * - 타임아웃 타이머 취소
	 * - GameInstance에 등록
	 * - PlayerState에 ID 저장
	 * - Controller 교체 예약
	 */
	void OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId);

	/**
	 * 로그인 실패 처리
	 * - Client RPC로 에러 메시지 전달
	 */
	void OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage);

	/**
	 * 로그인 타임아웃 처리
	 * - 플레이어 킥
	 */
	void OnLoginTimeout(APlayerController* PlayerController);

	/** 
	 * LoginController → GameController 교체
	 * 
	 * @param LoginController - 로그인 완료된 LoginController
	 * @param PlayerId - 로그인한 플레이어 ID
	 * 
	 * 내부 동작:
	 * 1. LoginController의 GameControllerClass 가져옴
	 * 2. 새 GameController 생성
	 * 3. PlayerState 복사
	 * 4. LoginController 파괴
	 * 5. SpawnHeroCharacter() 호출
	 */
	void SwapToGameController(AHellunaLoginController* LoginController, const FString& PlayerId);

	/** 
	 * HeroCharacter 소환 및 Possess
	 * 
	 * 내부 동작:
	 * 1. PlayerStart 위치에 캐릭터 스폰
	 * 2. Controller에 Possess
	 * 3. 첫 플레이어면 InitializeGame() 호출
	 */
	void SpawnHeroCharacter(APlayerController* PlayerController);

	// ============================================
	// 📌 보스 관련 (비로그인)
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
	// 📌 낮/밤 사이클 (비로그인)
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
	// 📌 몬스터 관련 (비로그인)
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
