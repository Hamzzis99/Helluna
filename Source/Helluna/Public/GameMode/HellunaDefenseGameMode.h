// HellunaDefenseGameMode.h
// 
// ╔══════════════════════════════════════════════════════════════════════════════╗
// ║                    HELLUNA DEFENSE GAME MODE                                  ║
// ║                    메인 게임 맵의 GameMode                                    ║
// ╠══════════════════════════════════════════════════════════════════════════════╣
// ║                                                                               ║
// ║  이 클래스는 크게 4가지 시스템을 담당합니다:                                   ║
// ║                                                                               ║
// ║  1. 🔐 로그인 시스템      - 플레이어 접속/로그인/로그아웃 처리                ║
// ║  2. 🌅 낮/밤 사이클       - 게임 페이즈 전환 (낮↔밤)                          ║
// ║  3. 👾 몬스터/보스 스폰   - 적 스폰 및 관리                                    ║
// ║  4. 📦 인벤토리 저장      - 아이템 저장/로드 시스템                           ║
// ║                                                                               ║
// ╠══════════════════════════════════════════════════════════════════════════════╣
// ║  📌 게임 시작 흐름:                                                           ║
// ║                                                                               ║
// ║  [서버 시작]                                                                   ║
// ║       ↓                                                                        ║
// ║  BeginPlay() - SaveGame 로드, 스폰포인트 캐싱                                  ║
// ║       ↓                                                                        ║
// ║  [플레이어 접속]                                                               ║
// ║       ↓                                                                        ║
// ║  PostLogin() - 로그인 대기 또는 SeamlessTravel 처리                           ║
// ║       ↓                                                                        ║
// ║  [로그인 성공]                                                                 ║
// ║       ↓                                                                        ║
// ║  SwapToGameController() - LoginController → GameController 교체               ║
// ║       ↓                                                                        ║
// ║  SpawnHeroCharacter() - 플레이어 캐릭터 소환                                   ║
// ║       ↓                                                                        ║
// ║  [첫 플레이어인 경우]                                                          ║
// ║       ↓                                                                        ║
// ║  InitializeGame() ⭐ ← 여기서 게임이 본격적으로 시작됨!                        ║
// ║       ↓                                                                        ║
// ║  EnterDay() - 첫 번째 낮 시작 → 낮/밤 사이클 시작                             ║
// ║                                                                               ║
// ╚══════════════════════════════════════════════════════════════════════════════╝
// 
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23

#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "Player/Inv_PlayerController.h"
#include "Inventory/HellunaInventorySaveGame.h"
#include "HellunaDefenseGameMode.generated.h"

class ATargetPoint;
class UHellunaAccountSaveGame;
class UHellunaInventorySaveGame;
class AHellunaPlayerState;
class AHellunaLoginController;
class UDataTable;

UCLASS()
class HELLUNA_API AHellunaDefenseGameMode : public AHellunaBaseGameMode
{
	GENERATED_BODY()
	
	friend class AHellunaLoginController;
	
public:
	AHellunaDefenseGameMode();

	// ╔══════════════════════════════════════════════════════════════════════════════╗
	// ║                         LIFECYCLE FUNCTIONS                                   ║
	// ║                         생명주기 함수                                         ║
	// ╚══════════════════════════════════════════════════════════════════════════════╝
	
	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	// ════════════════════════════════════════════════════════════════════════════════
	// 🔐 섹션 1: 로그인 시스템 (Gihyeon 담당 - 수정 시 상의 필요)
	// ════════════════════════════════════════════════════════════════════════════════
public:
	/**
	 * 로그인 처리 메인 함수
	 * 
	 * @param PlayerController - 로그인 요청한 Controller
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 * 
	 * 호출: LoginController::Server_RequestLogin()에서 RPC로 호출
	 */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password);

	/**
	 * 특정 플레이어가 현재 접속 중인지 확인
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

protected:
	// ─────────────────────────────────────────────────────────────────────────────
	// 로그인 관련 변수 (BP 설정)
	// ─────────────────────────────────────────────────────────────────────────────
	
	/** 로그인 성공 후 소환할 캐릭터 클래스 (BP에서 설정!) */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "히어로 캐릭터 클래스"))
	TSubclassOf<APawn> HeroCharacterClass;

	/** 계정 데이터 (ID/PW 저장) */
	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;

	/** 인벤토리 저장 데이터 */
	UPROPERTY()
	TObjectPtr<UHellunaInventorySaveGame> InventorySaveGame;

	/** 로그인 타임아웃 타이머 맵 */
	UPROPERTY()
	TMap<APlayerController*, FTimerHandle> LoginTimeoutTimers;

	/** 로그인 타임아웃 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Login", meta = (DisplayName = "로그인 타임아웃 (초)"))
	float LoginTimeoutSeconds = 60.0f;

	// ─────────────────────────────────────────────────────────────────────────────
	// 로그인 내부 함수 (수정 시 주의!)
	// ─────────────────────────────────────────────────────────────────────────────
	
	void OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId);
	void OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage);
	void OnLoginTimeout(APlayerController* PlayerController);
	void SwapToGameController(AHellunaLoginController* LoginController, const FString& PlayerId);
	void SpawnHeroCharacter(APlayerController* PlayerController);

	// ════════════════════════════════════════════════════════════════════════════════
	// ⭐ 섹션 2: 게임 시스템 (팀원 작업 영역!)
	// ════════════════════════════════════════════════════════════════════════════════
	// 
	// ✅ 여기에 게임 로직을 추가하세요!
	// 
	// 📌 주요 함수:
	//    - InitializeGame()    : 게임 시작 시 호출됨 (여기에 초기화 로직 추가!)
	//    - EnterDay()          : 낮 시작 시 호출됨
	//    - EnterNight()        : 밤 시작 시 호출됨
	//    - SpawnTestMonsters() : 몬스터 스폰 (커스터마이징 가능!)
	// 
	// 📌 BP에서 조절 가능:
	//    - TestDayDuration       : 낮 지속 시간
	//    - TestMonsterSpawnCount : 몬스터 스폰 개수
	//    - TestMonsterClass      : 몬스터 클래스
	//    - BossClass             : 보스 클래스
	// 
	// ════════════════════════════════════════════════════════════════════════════════
public:
	// ─────────────────────────────────────────────────────────────────────────────
	// 🎮 게임 상태 조회
	// ─────────────────────────────────────────────────────────────────────────────
	
	/** 게임이 초기화되었는지 확인 (첫 플레이어 캐릭터 소환 후 true) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Defense")
	bool IsGameInitialized() const { return bGameInitialized; }

	/**
	 * 게임 초기화 - 첫 플레이어 캐릭터 소환 후 자동 호출됨
	 * 
	 * ⭐ 이 함수가 호출된 후부터 게임이 본격적으로 시작됩니다!
	 * ⭐ 여기서 EnterDay()가 호출되어 낮/밤 사이클이 시작됩니다.
	 * 
	 * 팀원이 추가할 수 있는 것들:
	 * - 게임 시작 시 초기화가 필요한 로직
	 * - 튜토리얼 시작
	 * - 배경음악 재생
	 * - 등등...
	 */
	UFUNCTION(BlueprintCallable, Category = "Defense")
	void InitializeGame();

	/** 게임 재시작 */
	UFUNCTION(BlueprintCallable, Category = "Defense|Restart")
	void RestartGame();

protected:
	/** 게임 초기화 완료 플래그 */
	UPROPERTY(BlueprintReadOnly, Category = "Defense")
	bool bGameInitialized = false;

	// ═══════════════════════════════════════════════════════════════════════════════
	// 🌅 낮/밤 사이클 시스템 (DAY/NIGHT CYCLE)
	// ═══════════════════════════════════════════════════════════════════════════════
	// 
	// 📌 흐름:
	//    InitializeGame() → EnterDay() → [낮 시간 경과] → EnterNight() 
	//                                                        ↓
	//    [밤 몬스터 처치] or [타임아웃] ← ─────────────────────┘
	//                  ↓
	//            EnterDay() → ... (반복)
	// 
	// 📌 팀원 작업 포인트:
	//    - EnterDay(): 낮이 시작될 때 실행할 로직 추가
	//    - EnterNight(): 밤이 시작될 때 실행할 로직 추가
	//    - TestDayDuration: BP에서 낮 지속 시간 조절
	// 
	// ═══════════════════════════════════════════════════════════════════════════════
protected:
	FTimerHandle TimerHandle_ToNight;
	FTimerHandle TimerHandle_ToDay;

	/** 낮 지속 시간 (초) - BP에서 조절 가능! */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense|DayNight", 
		meta = (DisplayName = "낮 지속 시간 (초)"))
	float TestDayDuration = 10.f;

	/** 밤 실패 후 낮으로 돌아가는 딜레이 (초) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense|DayNight",
		meta = (DisplayName = "밤→낮 전환 딜레이 (초)"))
	float TestNightFailToDayDelay = 5.f;

	/**
	 * 낮 시작
	 * 
	 * 📌 호출 시점:
	 *    1. InitializeGame() - 게임 첫 시작
	 *    2. 밤에 몬스터 모두 처치 후
	 * 
	 * 📌 현재 동작:
	 *    - 몬스터 목록 초기화
	 *    - GameState에 Day 페이즈 설정
	 *    - TestDayDuration 후 EnterNight() 타이머 설정
	 * 
	 * ✅ 팀원 추가 가능 영역:
	 *    - 낮 시작 사운드/이펙트
	 *    - UI 업데이트
	 *    - NPC 행동 변경
	 *    - 조명 변경
	 */
	void EnterDay();

	/**
	 * 밤 시작
	 * 
	 * 📌 호출 시점:
	 *    - EnterDay()에서 TestDayDuration 경과 후
	 * 
	 * 📌 현재 동작:
	 *    - GameState에 Night 페이즈 설정
	 *    - 우주선 수리 완료 체크 → 완료면 보스 소환
	 *    - 미완료면 SpawnTestMonsters() 호출
	 * 
	 * ✅ 팀원 추가 가능 영역:
	 *    - 밤 시작 사운드/이펙트
	 *    - 몬스터 웨이브 로직
	 *    - 조명 변경 (어두워지기)
	 */
	void EnterNight();

	/** 우주선 수리 완료 여부 체크 */
	bool IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const;

	// ═══════════════════════════════════════════════════════════════════════════════
	// 👾 몬스터 스폰 시스템 (MONSTER SPAWN SYSTEM)
	// ═══════════════════════════════════════════════════════════════════════════════
	// 
	// 📌 구조:
	//    - MonsterSpawnPoints: 맵에 배치된 TargetPoint들 (태그: "MonsterSpawn")
	//    - AliveMonsters: 현재 살아있는 몬스터 목록
	//    - SpawnTestMonsters(): 밤 시작 시 몬스터 스폰
	// 
	// 📌 팀원 작업 포인트:
	//    - TestMonsterClass: BP에서 스폰할 몬스터 클래스 설정
	//    - TestMonsterSpawnCount: 스폰 개수 조절
	//    - SpawnTestMonsters(): 스폰 로직 커스터마이징
	// 
	// ═══════════════════════════════════════════════════════════════════════════════
protected:
	/** 살아있는 몬스터 목록 */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> AliveMonsters;

	/** 몬스터 스폰 포인트 (맵에서 캐싱) */
	UPROPERTY()
	TArray<ATargetPoint*> MonsterSpawnPoints;

public:
	/**
	 * 몬스터 등록 (몬스터 BeginPlay에서 호출)
	 * 
	 * ✅ 새 몬스터 클래스에서 BeginPlay()에 다음 코드 추가:
	 *    if (AHellunaDefenseGameMode* GM = GetWorld()->GetAuthGameMode<AHellunaDefenseGameMode>())
	 *    {
	 *        GM->RegisterAliveMonster(this);
	 *    }
	 */
	UFUNCTION(BlueprintCallable, Category = "Defense|Monster")
	void RegisterAliveMonster(AActor* Monster);

	/**
	 * 몬스터 사망 알림 (몬스터 사망 시 호출)
	 * 
	 * ✅ 몬스터 사망 로직에서 호출:
	 *    if (AHellunaDefenseGameMode* GM = GetWorld()->GetAuthGameMode<AHellunaDefenseGameMode>())
	 *    {
	 *        GM->NotifyMonsterDied(this);
	 *    }
	 * 
	 * 📌 모든 몬스터가 죽으면 자동으로 EnterDay() 호출됨!
	 */
	UFUNCTION(BlueprintCallable, Category = "Defense|Monster")
	void NotifyMonsterDied(AActor* DeadMonster);

	/** 현재 살아있는 몬스터 수 */
	UFUNCTION(BlueprintPure, Category = "Defense|Monster")
	int32 GetAliveMonsterCount() const { return AliveMonsters.Num(); }

protected:
	/** 스폰할 몬스터 클래스 - BP에서 설정! */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense|Monster|Test",
		meta = (DisplayName = "테스트 몬스터 클래스"))
	TSubclassOf<APawn> TestMonsterClass;

	/** 몬스터 스폰 포인트 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Monster|Test")
	FName MonsterSpawnPointTag = TEXT("MonsterSpawn");

	/** 한 번에 스폰할 몬스터 수 - BP에서 조절 가능! */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense|Monster|Test", 
		meta = (DisplayName = "몬스터 스폰 개수", ClampMin = "0"))
	int32 TestMonsterSpawnCount = 3;

	/** 몬스터 스폰 포인트 캐싱 (BeginPlay에서 호출) */
	void CacheMonsterSpawnPoints();

	/**
	 * 테스트 몬스터 스폰
	 * 
	 * 📌 현재 동작:
	 *    - MonsterSpawnPoints 중 랜덤 위치에
	 *    - TestMonsterSpawnCount 개수만큼 스폰
	 * 
	 * ✅ 팀원 커스터마이징 가능:
	 *    - 웨이브 시스템 추가
	 *    - 몬스터 종류별 스폰
	 *    - 난이도에 따른 스폰 개수 조절
	 */
	void SpawnTestMonsters();

	// ═══════════════════════════════════════════════════════════════════════════════
	// 👹 보스 스폰 시스템 (BOSS SPAWN SYSTEM)
	// ═══════════════════════════════════════════════════════════════════════════════
	// 
	// 📌 보스 소환 조건: 우주선 수리 100% 완료 시
	// 
	// ═══════════════════════════════════════════════════════════════════════════════
public:
	UFUNCTION(BlueprintCallable, Category = "Defense|Boss")
	void SetBossReady(bool bReady);

protected:
	/** 보스 클래스 - BP에서 설정! */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Defense|Boss",
		meta = (DisplayName = "보스 클래스"))
	TSubclassOf<APawn> BossClass;

	/** 보스 스폰 포인트 태그 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	FName BossSpawnPointTag = TEXT("BossSpawn");

	/** 보스 스폰 Z 오프셋 */
	UPROPERTY(EditDefaultsOnly, Category = "Defense|Boss")
	float SpawnZOffset = 150.f;

	/** 보스 준비 상태 */
	UPROPERTY(BlueprintReadOnly, Category = "Defense|Boss")
	bool bBossReady = false;

	/** 보스 스폰 포인트 */
	UPROPERTY()
	TArray<ATargetPoint*> BossSpawnPoints;

	void CacheBossSpawnPoints();
	void TrySummonBoss();

	// ╔══════════════════════════════════════════════════════════════════════════════╗
	// ║                                                                               ║║
	// ║  📦 인벤토리 저장 시스템 (INVENTORY SAVE SYSTEM) - Gihyeon 담당              ║
	// ║                                                                               ║
	// ║  ⚠️ 팀원 주의: 이 영역은 인벤토리 저장/로드 시스템입니다.                    ║
	// ║     직접 수정하지 않는 것을 권장합니다.                                       ║
	// ║                                                                               ║
	// ╚══════════════════════════════════════════════════════════════════════════════╝
public:
	/** 모든 플레이어의 인벤토리 저장 (맵 이동 전 호출) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 SaveAllPlayersInventory();

	// ─────────────────────────────────────────────────────────────────────────────
	// 디버그 함수 (콘솔에서 "ke * FunctionName"으로 호출)
	// ─────────────────────────────────────────────────────────────────────────────
	
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugTestItemTypeMapping();

	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugPrintAllItemMappings();

	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugTestInventorySaveGame();

	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugRequestSaveAllInventory();

	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugForceAutoSave();

	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugTestLoadInventory();

protected:
	// ─────────────────────────────────────────────────────────────────────────────
	// 인벤토리 시스템 내부 변수
	// ─────────────────────────────────────────────────────────────────────────────

	UPROPERTY(EditDefaultsOnly, Category = "Helluna|Inventory")
	TObjectPtr<UDataTable> ItemTypeMappingDataTable;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Helluna|Inventory|AutoSave")
	float AutoSaveIntervalSeconds = 300.0f;

	FTimerHandle AutoSaveTimerHandle;

	UPROPERTY()
	TMap<FString, FHellunaPlayerInventoryData> CachedPlayerInventoryData;

	UPROPERTY()
	TMap<AController*, FString> ControllerToPlayerIdMap;

	// ─────────────────────────────────────────────────────────────────────────────
	// 인벤토리 시스템 내부 함수
	// ─────────────────────────────────────────────────────────────────────────────

	void StartAutoSaveTimer();
	void StopAutoSaveTimer();
	void OnAutoSaveTimer();
	void RequestAllPlayersInventoryState();
	void RequestPlayerInventoryState(APlayerController* PC);

	UFUNCTION()
	void OnPlayerInventoryStateReceived(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems);

public:
	void LoadAndSendInventoryToClient(APlayerController* PC);
	void SaveInventoryFromCharacterEndPlay(const FString& PlayerId, const TArray<FInv_SavedItemData>& CollectedItems);

	UFUNCTION()
	void OnInvControllerEndPlay(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems);
};
