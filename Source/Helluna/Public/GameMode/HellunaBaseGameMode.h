// ════════════════════════════════════════════════════════════════════════════════
// HellunaBaseGameMode.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로그인/인벤토리 시스템을 담당하는 Base GameMode
// DefenseGameMode는 이 클래스를 상속받아 게임 로직만 구현
//
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "HellunaTypes.h"
#include "Inventory/HellunaInventorySaveGame.h"  // FHellunaPlayerInventoryData 필요
#include "HellunaBaseGameMode.generated.h"

// ════════════════════════════════════════════════════════════════════════════════
// 전처리기 - 디버그 로그 제어
// ════════════════════════════════════════════════════════════════════════════════
#define HELLUNA_DEBUG_LOGIN 1
#define HELLUNA_DEBUG_INVENTORY 1

// 전방 선언
class UHellunaAccountSaveGame;
class AHellunaLoginController;
class AInv_PlayerController;
class UDataTable;
struct FInv_SavedItemData;

UCLASS()
class HELLUNA_API AHellunaBaseGameMode : public AGameMode
{
	GENERATED_BODY()

	friend class AHellunaLoginController;

public:
	AHellunaBaseGameMode();

protected:
	virtual void BeginPlay() override;

public:
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;

	// ════════════════════════════════════════════════════════════════════════════════
	// 게임 초기화 (자식 클래스에서 override)
	// ════════════════════════════════════════════════════════════════════════════════

	/** 게임 초기화 - 자식 클래스에서 override하여 실제 게임 로직 구현 */
	virtual void InitializeGame();

	/** 게임 초기화 완료 여부 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Game")
	bool IsGameInitialized() const { return bGameInitialized; }

	// ════════════════════════════════════════════════════════════════════════════════
	// 🔐 로그인 시스템
	// ════════════════════════════════════════════════════════════════════════════════
public:
	/**
	 * 로그인 처리 메인 함수
	 * @param PlayerController - 로그인 요청한 Controller
	 * @param PlayerId - 입력한 아이디
	 * @param Password - 입력한 비밀번호
	 */
	UFUNCTION(BlueprintCallable, Category = "Login(로그인)")
	void ProcessLogin(APlayerController* PlayerController, const FString& PlayerId, const FString& Password);

	/**
	 * 특정 플레이어가 현재 접속 중인지 확인
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Login(로그인)")
	bool IsPlayerLoggedIn(const FString& PlayerId) const;

protected:
	void OnLoginSuccess(APlayerController* PlayerController, const FString& PlayerId);
	void OnLoginFailed(APlayerController* PlayerController, const FString& ErrorMessage);
	void OnLoginTimeout(APlayerController* PlayerController);
	void SwapToGameController(AHellunaLoginController* LoginController, const FString& PlayerId, EHellunaHeroType SelectedHeroType = EHellunaHeroType::None);
	void SpawnHeroCharacter(APlayerController* PlayerController);

	// ════════════════════════════════════════════════════════════════════════════════
	// 🎭 캐릭터 선택 시스템
	// ════════════════════════════════════════════════════════════════════════════════
protected:
	/**
	 * 캐릭터 선택 처리
	 * @param PlayerController - 선택 요청한 Controller
	 * @param HeroType - 선택한 캐릭터 타입
	 */
	void ProcessCharacterSelection(APlayerController* PlayerController, EHellunaHeroType HeroType);

	/** 캐릭터 사용 등록 */
	void RegisterCharacterUse(EHellunaHeroType HeroType, const FString& PlayerId);

	/** 캐릭터 사용 해제 (로그아웃 시 호출) */
	void UnregisterCharacterUse(const FString& PlayerId);

	/** 특정 캐릭터가 사용 중인지 확인 */
	bool IsCharacterInUse(EHellunaHeroType HeroType) const;

	/** HeroType으로 캐릭터 클래스 가져오기 */
	TSubclassOf<APawn> GetHeroCharacterClass(EHellunaHeroType HeroType) const;

public:
	/** 사용 가능한 캐릭터 목록 반환 (맵 버전) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "CharacterSelect(캐릭터 선택)")
	TMap<EHellunaHeroType, bool> GetAvailableCharactersMap() const;

	/** 사용 가능한 캐릭터 목록 반환 (배열 버전) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "CharacterSelect(캐릭터 선택)")
	TArray<bool> GetAvailableCharacters() const;

	/** 인덱스를 HeroType으로 변환 */
	static EHellunaHeroType IndexToHeroType(int32 Index);

	// ════════════════════════════════════════════════════════════════════════════════
	// 📦 인벤토리 시스템
	// ════════════════════════════════════════════════════════════════════════════════
public:
	/** 모든 플레이어의 인벤토리 저장 */
	UFUNCTION(BlueprintCallable, Category = "Inventory(인벤토리)")
	int32 SaveAllPlayersInventory();

	/** 인벤토리 데이터 로드 후 클라이언트로 전송 */
	void LoadAndSendInventoryToClient(APlayerController* PC);

	/** 캐릭터 EndPlay 시 인벤토리 저장 */
	void SaveInventoryFromCharacterEndPlay(const FString& PlayerId, const TArray<FInv_SavedItemData>& CollectedItems);

protected:
	// ───────────────────────────────────────────────────────────────────────────────
	// 자동저장
	// ───────────────────────────────────────────────────────────────────────────────
	void StartAutoSaveTimer();
	void StopAutoSaveTimer();
	void OnAutoSaveTimer();
	void RequestAllPlayersInventoryState();
	void RequestPlayerInventoryState(APlayerController* PC);

	UFUNCTION()
	void OnPlayerInventoryStateReceived(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems);

public:
	UFUNCTION()
	void OnInvControllerEndPlay(AInv_PlayerController* PlayerController, const TArray<FInv_SavedItemData>& SavedItems);

	// ════════════════════════════════════════════════════════════════════════════════
	// 디버그 함수
	// ════════════════════════════════════════════════════════════════════════════════
public:
	UFUNCTION(BlueprintCallable, Category = "Debug|Inventory")
	void DebugTestItemTypeMapping();

	UFUNCTION(BlueprintCallable, Category = "Debug|Inventory")
	void DebugPrintAllItemMappings();

	UFUNCTION(BlueprintCallable, Category = "Debug|Inventory")
	void DebugTestInventorySaveGame();

	UFUNCTION(BlueprintCallable, Category = "Debug|Inventory")
	void DebugRequestSaveAllInventory();

	UFUNCTION(BlueprintCallable, Category = "Debug|Inventory")
	void DebugForceAutoSave();

	UFUNCTION(BlueprintCallable, Category = "Debug|Inventory")
	void DebugTestLoadInventory();

protected:
	// ════════════════════════════════════════════════════════════════════════════════
	// 게임 초기화 상태
	// ════════════════════════════════════════════════════════════════════════════════

	UPROPERTY(BlueprintReadOnly, Category = "Game")
	bool bGameInitialized = false;

	// ════════════════════════════════════════════════════════════════════════════════
	// 계정/인벤토리 SaveGame
	// ════════════════════════════════════════════════════════════════════════════════

	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;

	UPROPERTY()
	TObjectPtr<UHellunaInventorySaveGame> InventorySaveGame;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory(인벤토리)", meta = (DisplayName = "아이템 타입 매핑 테이블"))
	TObjectPtr<UDataTable> ItemTypeMappingDataTable;

	// ════════════════════════════════════════════════════════════════════════════════
	// 로그인 설정
	// ════════════════════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, Category = "Login(로그인)", meta = (DisplayName = "로그인 타임아웃(초)"))
	float LoginTimeoutSeconds = 60.0f;

	UPROPERTY()
	TMap<APlayerController*, FTimerHandle> LoginTimeoutTimers;

	// ════════════════════════════════════════════════════════════════════════════════
	// 캐릭터 선택
	// ════════════════════════════════════════════════════════════════════════════════

	/** 히어로 캐릭터 클래스 매핑 (TMap) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterSelect(캐릭터 선택)", meta = (DisplayName = "히어로 캐릭터 클래스 매핑"))
	TMap<EHellunaHeroType, TSubclassOf<APawn>> HeroCharacterMap;

	/** 기본 히어로 클래스 (폴백용) */
	UPROPERTY(EditDefaultsOnly, Category = "CharacterSelect(캐릭터 선택)", meta = (DisplayName = "기본 히어로 클래스 (폴백)"))
	TSubclassOf<APawn> HeroCharacterClass;

	/** 현재 사용 중인 캐릭터 맵 (타입 → PlayerId) */
	UPROPERTY()
	TMap<EHellunaHeroType, FString> UsedCharacterMap;

	// ════════════════════════════════════════════════════════════════════════════════
	// 인벤토리 캐시
	// ════════════════════════════════════════════════════════════════════════════════

	UPROPERTY()
	TMap<FString, FHellunaPlayerInventoryData> CachedPlayerInventoryData;

	UPROPERTY()
	TMap<AController*, FString> ControllerToPlayerIdMap;

	// ════════════════════════════════════════════════════════════════════════════════
	// 자동저장
	// ════════════════════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, Category = "Inventory(인벤토리)", meta = (DisplayName = "자동저장 주기(초)"))
	float AutoSaveIntervalSeconds = 300.0f;

	FTimerHandle AutoSaveTimerHandle;
};
