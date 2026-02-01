#pragma once

#include "CoreMinimal.h"
#include "GameMode/HellunaBaseGameMode.h"
#include "Player/Inv_PlayerController.h"
#include "Inventory/HellunaInventorySaveGame.h"  // FHellunaPlayerInventoryData 사용을 위해 필요
#include "HellunaDefenseGameMode.generated.h"

class ATargetPoint;
class UHellunaAccountSaveGame;
class UHellunaInventorySaveGame;
class AHellunaPlayerState;
class AHellunaLoginController;
class UDataTable;

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
	 * 인벤토리 저장 데이터 (BeginPlay에서 로드)
	 * 플레이어별 아이템/수량/위치 저장
	 */
	UPROPERTY()
	TObjectPtr<UHellunaInventorySaveGame> InventorySaveGame;

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

	// ============================================
	// 📌 인벤토리 저장 시스템 (Phase 1~6)
	// ============================================
	// 
	// 인벤토리 저장/로드에 필요한 변수와 함수들
	// 
	// ▶ 저장 시점:
	//   1. Logout() - 플레이어 연결 끊김
	//   2. 맵 이동 - Server_SaveAndMoveLevel()
	//   3. 자동저장 - AutoSaveIntervalSeconds 마다
	// 
	// ▶ 로드 시점:
	//   1. SpawnHeroCharacter() 직후
	// 
	// ============================================

public:
	// ============================================
	// 📌 [Phase 1] DataTable 매핑 테스트
	// ============================================
	
	/**
	 * [디버깅] ItemType → ActorClass 매핑 테스트
	 * 
	 * 콘솔에서 호출 방법:
	 * 1. 에디터에서 ~ 키로 콘솔 열기
	 * 2. "ke * DebugTestItemTypeMapping" 입력
	 * 
	 * Output Log에서 확인할 것:
	 * - "[ItemTypeMapping] 매핑 성공" 메시지
	 * - 5개 아이템 모두 매핑 성공해야 함
	 * - 매핑 실패 시 DataTable 행 추가 확인!
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugTestItemTypeMapping();

	/**
	 * [디버깅] DataTable의 모든 매핑 출력
	 * 
	 * 콘솔에서 호출 방법:
	 * "ke * DebugPrintAllItemMappings"
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugPrintAllItemMappings();

	// ============================================
	// 📌 [Phase 2] SaveGame 테스트
	// ============================================
	
	/**
	 * [디버깅] InventorySaveGame 저장/로드 테스트
	 * 
	 * 테스트 내용:
	 * 1. SaveGame 로드 또는 생성
	 * 2. 더미 데이터로 저장 테스트
	 * 3. 로드 테스트
	 * 4. 파일 생성 확인 (Saved/SaveGames/HellunaInventory.sav)
	 * 
	 * Output Log에서 확인할 것:
	 * - "[InventorySaveGame]" 로그 메시지들
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugTestInventorySaveGame();

protected:
	// ============================================
	// 📌 [Phase 1] DataTable 참조
	// ============================================
	
	/**
	 * ItemType(GameplayTag) → Actor Blueprint 클래스 매핑 테이블
	 * 
	 * ▶ 설정 방법 (BP에서):
	 *   1. BP_DefenseGameMode 열기
	 *   2. Details 패널에서 "Item Type Mapping Data Table" 찾기
	 *   3. DT_ItemTypeMapping 선택
	 * 
	 * ▶ 사용 목적:
	 *   - 인벤토리 로드 시 GameplayTag로 Actor 클래스 조회
	 *   - Actor를 임시 스폰하여 ItemManifest 추출
	 * 
	 * ▶ DataTable 위치: Content/Data/Inventory/DT_ItemTypeMapping
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Helluna|Inventory", 
		meta = (DisplayName = "아이템 타입 매핑 DataTable"))
	TObjectPtr<UDataTable> ItemTypeMappingDataTable;

	// ============================================
	// 📌 [Phase 4] 자동저장 설정
	// ============================================
	
	/**
	 * 자동저장 주기 (초 단위)
	 * 
	 * ▶ 기본값: 300초 (5분)
	 * ▶ BP에서 수정 가능
	 * ▶ 0으로 설정 시 자동저장 비활성화
	 * 
	 * 예: 60.0f = 1분마다 자동저장
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Helluna|Inventory|AutoSave",
		meta = (DisplayName = "자동저장 주기 (초)", ClampMin = "0", UIMin = "0"))
	float AutoSaveIntervalSeconds = 300.0f;

	/** 자동저장 타이머 핸들 */
	FTimerHandle AutoSaveTimerHandle;

	/**
	 * 플레이어별 마지막 인벤토리 상태 캐시
	 * 
	 * Key: PlayerUniqueId
	 * Value: 마지막으로 수신한 인벤토리 데이터
	 * 
	 * 용도:
	 * - Logout 시 클라이언트 연결 끊긴 상태에서도 저장 가능
	 * - 자동저장 시 업데이트됨
	 */
	UPROPERTY()
	TMap<FString, FHellunaPlayerInventoryData> CachedPlayerInventoryData;

	/**
	 * Controller → PlayerId 매핑
	 * 
	 * 용도:
	 * - Controller EndPlay 시점에 PlayerState가 이미 파괴되어 있을 수 있음
	 * - 이 매핑을 통해 Controller에서 PlayerId를 직접 조회
	 * 
	 * 추가 시점: SwapToGameController() 
	 * 사용 시점: OnInvControllerEndPlay()
	 * 삭제 시점: OnInvControllerEndPlay() (저장 완료 후)
	 */
	UPROPERTY()
	TMap<AController*, FString> ControllerToPlayerIdMap;

	// ============================================
	// 📌 [Phase 4] 자동저장 함수
	// ============================================

	/**
	 * 자동저장 타이머 시작
	 * BeginPlay() 또는 첫 플레이어 로그인 시 호출
	 */
	void StartAutoSaveTimer();

	/**
	 * 자동저장 타이머 정지
	 * EndPlay() 시 호출
	 */
	void StopAutoSaveTimer();

	/**
	 * 자동저장 타이머 콜백
	 * AutoSaveIntervalSeconds마다 호출됨
	 */
	void OnAutoSaveTimer();

	/**
	 * 모든 플레이어의 인벤토리 저장 요청
	 * 각 플레이어에게 Client_RequestInventoryState() RPC 전송
	 */
	void RequestAllPlayersInventoryState();

	/**
	 * 개별 플레이어 인벤토리 저장 요청
	 * @param PC - 대상 PlayerController
	 */
	void RequestPlayerInventoryState(APlayerController* PC);

	/**
	 * 인벤토리 상태 수신 콜백 (델리게이트 바인딩)
	 * PlayerController에서 Server_ReceiveInventoryState() 호출 시 실행됨
	 * 
	 * @param PlayerController - 데이터를 보낸 플레이어
	 * @param SavedItems - 수신된 인벤토리 데이터
	 */
	UFUNCTION()
	void OnPlayerInventoryStateReceived(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems);

public:
	// ============================================
	// 📌 [Phase 4] 디버그 함수
	// ============================================

	/**
	 * [디버깅] 수동으로 모든 플레이어 인벤토리 저장 요청
	 * 
	 * 콘솔에서 호출 방법:
	 * "ke * DebugRequestSaveAllInventory"
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugRequestSaveAllInventory();

	/**
	 * [디버깅] 자동저장 타이머 강제 실행 (테스트용)
	 * AutoSaveIntervalSeconds를 짧게 설정하지 않아도 즉시 저장 테스트 가능
	 *
	 * 콘솔에서 호출 방법:
	 * "ke * DebugForceAutoSave"
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugForceAutoSave();

	// ============================================
	// 📌 [Phase 5] 인벤토리 로드 함수
	// ============================================

	/**
	 * 저장된 인벤토리 데이터를 로드하여 클라이언트에 전송
	 *
	 * ============================================
	 * 📌 호출 시점:
	 * ============================================
	 * - SpawnHeroCharacter() 완료 직후
	 * - 플레이어가 게임에 처음 입장했을 때
	 *
	 * ============================================
	 * 📌 처리 흐름:
	 * ============================================
	 * 1. PlayerState에서 PlayerUniqueId 가져오기
	 * 2. InventorySaveGame->LoadPlayerInventory()로 데이터 로드
	 * 3. 각 아이템에 대해:
	 *    a. DataTable에서 ItemType → ActorClass 조회
	 *    b. 서버에서 Actor 스폰 → ItemComponent 추출
	 *    c. InventoryComponent에 아이템 추가
	 * 4. Client RPC로 Grid 위치 복원 데이터 전송
	 *
	 * @param PC - 인벤토리를 로드할 PlayerController
	 */
	void LoadAndSendInventoryToClient(APlayerController* PC);

	/**
	 * [Phase 4 개선] Character EndPlay에서 호출되는 인벤토리 저장
	 *
	 * 📌 호출 시점: HeroCharacter::EndPlay() (Pawn 파괴 직전)
	 * 📌 목적: Logout()에서 Pawn이 이미 nullptr이므로, 미리 저장
	 *
	 * @param PlayerId - 플레이어 고유 ID
	 * @param CollectedItems - InventoryComponent에서 수집한 아이템 데이터
	 */
	void SaveInventoryFromCharacterEndPlay(const FString& PlayerId, const TArray<FInv_SavedItemData>& CollectedItems);

	/**
	 * ⭐ [Phase 4 개선] Inv_PlayerController EndPlay 델리게이트 핸들러
	 *
	 * 📌 호출 시점: Inv_PlayerController::EndPlay() (Controller 파괴 직전)
	 * 📌 장점: Controller에 InventoryComponent가 있으므로 확실히 접근 가능!
	 *
	 * @param PlayerController - 종료되는 PlayerController
	 * @param SavedItems - 수집된 인벤토리 데이터
	 */
	UFUNCTION()
	void OnInvControllerEndPlay(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems);

	/**
	 * [디버깅] 수동으로 인벤토리 로드 테스트
	 *
	 * 콘솔에서 호출 방법:
	 * "ke * DebugTestLoadInventory"
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugTestLoadInventory();
};
