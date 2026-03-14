// ════════════════════════════════════════════════════════════════════════════════
// HellunaBaseGameMode.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로그인/캐릭터 선택 시스템을 담당하는 Base GameMode
// 인벤토리 저장/로드는 부모 AInv_SaveGameMode가 처리
// DefenseGameMode는 이 클래스를 상속받아 게임 로직만 구현
//
// 📌 상속 구조:
//    AGameMode → AInv_SaveGameMode → AHellunaBaseGameMode → AHellunaDefenseGameMode
//
// 📌 작성자: Gihyeon
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Helluna.h"  // 디버그 매크로 정의 (HELLUNA_DEBUG_LOGIN 등)
#include "Persistence/Inv_SaveGameMode.h"
#include "HellunaTypes.h"
#include "HellunaBaseGameMode.generated.h"

// ════════════════════════════════════════════════════════════════════════════════
// 전처리기 - 디버그 로그 제어
// ════════════════════════════════════════════════════════════════════════════════
// 📌 HELLUNA_DEBUG_LOGIN, HELLUNA_DEBUG_INVENTORY 등은 Helluna.h에서 정의됨
// 📌 여기서 재정의하지 않음 (중복 정의 경고 방지)
// ════════════════════════════════════════════════════════════════════════════════

// 전방 선언
class UHellunaAccountSaveGame;
class AHellunaLoginController;
class AInv_PlayerController;
class UDataTable;

// ════════════════════════════════════════════════════════════════════════════════
// Phase 6: 로비 배포 정보 (ClientTravel URL에서 파싱)
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT()
struct FLobbyDeployInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerId;

	UPROPERTY()
	EHellunaHeroType HeroType = EHellunaHeroType::None;
};

UCLASS()
class HELLUNA_API AHellunaBaseGameMode : public AInv_SaveGameMode
{
	GENERATED_BODY()

	friend class AHellunaLoginController;

public:
	AHellunaBaseGameMode();

	// ── 부모 Override (인벤토리 저장/로드) — public: 사체 루팅 등 외부에서 호출 필요 ──
	virtual TSubclassOf<AActor> ResolveItemClass(const FGameplayTag& ItemType) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ── Phase 6: InitNewPlayer — URL Options에서 로비 배포 정보 파싱 ──
	virtual void PreLogin(const FString& Options, const FString& Address,
		const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

	virtual FString InitNewPlayer(APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;

	// ── 부모 Override (인벤토리 저장/로드) ──
	virtual FString GetPlayerSaveId(APlayerController* PC) const override;

	/** 크래시 복구 체크 — PostLogin 시 호출하여 비정상 종료 시 Loadout → Stash 복구 */
	void CheckAndRecoverFromCrash(const FString& PlayerId);
	bool ShouldEnforceLobbyDeployAdmission() const;
	bool ParseLobbyDeployOptions(const FString& Options, FString& OutPlayerId, int32& OutHeroTypeIndex) const;
	bool ValidateLobbyDeployAdmission(const FString& PlayerId, int32 HeroTypeIndex, FString& OutErrorMessage) const;

public:
	// ── Phase 3: SQLite 저장/로드 전환 ──
	/** SQLite 저장 → 실패 시 .sav 폴백 */
	virtual bool SaveCollectedItems(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items) override;
	/** SQLite 로드 → 실패 시 .sav 폴백 */
	virtual void LoadAndSendInventoryToClient(APlayerController* PC) override;

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
	// 🔧 디버그 설정
	// ════════════════════════════════════════════════════════════════════════════════

	/**
	 * ⭐ 디버그: 로그인 절차 스킵
	 * true 시 PostLogin에서 자동으로 디버그 GUID 부여 → 로그인/캐릭터선택 없이 바로 플레이 가능
	 * BP_DefenseGameMode 등에서 에디터 체크박스로 On/Off
	 *
	 * ⚠️ 릴리즈 빌드에서는 반드시 false!
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Debug(디버그)", meta = (DisplayName = "개발자 모드(로그인 장면 스킵)"))
	bool bDebugSkipLogin = false;

	/**
	 * 디버그 모드에서 자동 선택할 캐릭터 타입
	 * bDebugSkipLogin == true일 때 이 캐릭터로 자동 스폰됨
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Debug(디버그)", meta = (DisplayName = "디버그 캐릭터 타입", EditCondition = "bDebugSkipLogin"))
	EHellunaHeroType DebugHeroType = EHellunaHeroType::Lui;

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

	/** 인벤토리 데이터 사전 로드 (디스크 I/O를 캐릭터 스폰 전에 완료) */
	void PreCacheInventoryForPlayer(const FString& PlayerId);

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
	// 📦 인벤토리 시스템 — Controller EndPlay 핸들러
	// ════════════════════════════════════════════════════════════════════════════════
public:
	/**
	 * Controller EndPlay 시 인벤토리 저장 + 게임별 로그아웃 처리
	 * 저장은 Super::OnInventoryControllerEndPlay()에 위임
	 * 게임별 로그아웃(PlayerState, GameInstance)은 여기서 직접 처리
	 */
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
	// 계정 SaveGame
	// ════════════════════════════════════════════════════════════════════════════════

	UPROPERTY()
	TObjectPtr<UHellunaAccountSaveGame> AccountSaveGame;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory(인벤토리)", meta = (DisplayName = "아이템 타입 매핑 테이블"))
	TObjectPtr<UDataTable> ItemTypeMappingDataTable;

	// ════════════════════════════════════════════════════════════════════════════════
	// 로그인 설정
	// ════════════════════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, Category = "Login(로그인)", meta = (DisplayName = "로그인 타임아웃(초)"))
	float LoginTimeoutSeconds = 60.0f;

	UPROPERTY()
	TMap<APlayerController*, FTimerHandle> LoginTimeoutTimers;

	/** Phase 6: 로비 배포 대기 맵 (InitNewPlayer에서 추가, PostLogin에서 소비) */
	UPROPERTY()
	TMap<TObjectPtr<APlayerController>, FLobbyDeployInfo> PendingLobbyDeployMap;

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

	/** 사전 로드된 인벤토리 캐시 (PlayerId → SaveData) — SpawnHeroCharacter 전에 채워짐 */
	TMap<FString, FInv_PlayerSaveData> PreCachedInventoryMap;

	// [Fix26] 람다 기반 fire-and-forget 타이머 핸들 수집 배열
	// ClearAllTimersForObject(this)는 람다 타이머를 해제하지 않으므로, EndPlay에서 개별 ClearTimer 필요
	TArray<FTimerHandle> LambdaTimerHandles;
};
