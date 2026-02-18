// ════════════════════════════════════════════════════════════════════════════════
// Inv_SaveGameMode.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    인벤토리 저장/로드를 독립적으로 처리하는 GameMode 기반 클래스
//    게임별 GameMode가 이 클래스를 상속받아 Super::로 저장/로드 사용
//
// 📌 상속 구조:
//    AGameMode → AInv_SaveGameMode → (게임별 GameMode)
//
// 📌 플러그인 독립성:
//    이 클래스는 Inventory 플러그인 내부에만 의존
//    Helluna 게임 모듈의 어떤 클래스도 참조하지 않음
//    → 다른 프로젝트에 플러그인만 넣어도 저장/로드 작동
//
// 📌 게임별 커스터마이즈 (override 필요):
//    - ResolveItemClass(): ItemType → ActorClass 매핑 (게임별 DataTable)
//    - GetPlayerSaveId(): 플레이어 고유 ID 가져오기 (게임별 PlayerState)
//
// 📌 BP 노출:
//    UPROPERTY들은 자식 BP(예: BP_DefenseGameMode)의 디테일 패널에 노출
//    UFUNCTION(BlueprintCallable)은 자식 BP에서 직접 호출 가능
//
// 📌 주요 저장 경로 (4가지):
//    1. 자동저장: 타이머 → RPC 왕복 → OnPlayerInventoryStateReceived → 디스크
//    2. EndPlay: Controller EndPlay → OnInventoryControllerEndPlay → 캐시병합 → 디스크
//    3. Logout: 자식 GameMode의 Logout → Super::OnPlayerInventoryLogout → 디스크
//    4. 리슨서버 종료: EndPlay → SaveAllPlayersInventory → 디스크
//
// 📌 주요 로드 경로:
//    자식 GameMode에서 Super::LoadAndSendInventoryToClient(PC) 호출
//    → .sav 로드 → ResolveItemClass()로 Actor 스폰 → InvComp에 추가
//    → 그리드 위치 복원 → 장착 복원 → Client RPC로 클라이언트 전송
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "Persistence/Inv_SaveTypes.h"
#include "Inv_SaveGameMode.generated.h"

// 전방 선언
class UInv_InventoryComponent;
class UInv_EquipmentComponent;
class AInv_PlayerController;
class AInv_EquipActor;
class UInv_ItemComponent;
class UInv_InventoryItem;
struct FInv_SavedItemData;

UCLASS()
class INVENTORY_API AInv_SaveGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AInv_SaveGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ════════════════════════════════════════════════════════════════
	// 💾 저장 — 자식 클래스에서 Super:: 호출
	// ════════════════════════════════════════════════════════════════

	/**
	 * 단일 플레이어 인벤토리 저장
	 *
	 * 📌 처리 흐름:
	 *   1. PC → FindComponentByClass<UInv_InventoryComponent>()
	 *   2. InvComp->CollectInventoryDataForSave() → TArray<FInv_SavedItemData>
	 *   3. MergeEquipmentState(PC, Items) → 장착 정보 병합
	 *   4. SaveCollectedItems(PlayerId, Items) → 디스크 저장
	 *
	 * @param PlayerId  저장할 플레이어의 고유 ID
	 * @param PC        대상 PlayerController (InvComp, EquipComp 소유)
	 * @return true = 저장 성공
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 저장",
		meta = (DisplayName = "플레이어 인벤토리 저장(SavePlayerInventory)"))
	virtual bool SavePlayerInventory(const FString& PlayerId, APlayerController* PC);

	/**
	 * 이미 수집된 데이터로 저장
	 *
	 * 📌 처리 흐름:
	 *   1. FInv_PlayerSaveData 생성 (LastSaveTime = Now)
	 *   2. InventorySaveGame->SavePlayer(PlayerId, Data) → 메모리 저장
	 *   3. UInv_InventorySaveGame::SaveToDisk() → .sav 파일 저장
	 *   4. CachedPlayerData에도 캐싱
	 *
	 * @param PlayerId       플레이어 고유 ID
	 * @param Items          저장할 아이템 배열 (장착 정보 포함)
	 * @return true = 저장 성공
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 저장",
		meta = (DisplayName = "수집 데이터 저장(SaveCollectedItems)"))
	virtual bool SaveCollectedItems(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items);

	/**
	 * 전체 플레이어 인벤토리 저장
	 *
	 * 📌 처리 흐름:
	 *   1. GetWorld()->GetPlayerControllerIterator()로 전체 PC 순회
	 *   2. 각 PC에 대해 GetPlayerSaveId() → PlayerId
	 *   3. SavePlayerInventory(PlayerId, PC) 호출
	 *
	 * @return 저장된 플레이어 수
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 저장",
		meta = (DisplayName = "전체 플레이어 저장(SaveAllPlayersInventory)"))
	virtual int32 SaveAllPlayersInventory();

	/**
	 * 플레이어 로그아웃 시 저장 처리
	 *
	 * 📌 처리 흐름:
	 *   1. SavePlayerInventory(PlayerId, PC)
	 *   2. CachedPlayerData 지연 삭제 (2초 후 — EndPlay 장착 병합 대기)
	 *
	 * ⚠️ 주의:
	 *   Logout()은 EndPlay()보다 먼저 호출됨
	 *   캐시를 즉시 삭제하면 EndPlay에서 장착 정보 병합 불가
	 *   → 2초 지연 삭제로 해결
	 */
	virtual void OnPlayerInventoryLogout(const FString& PlayerId, APlayerController* PC);

	/**
	 * Controller EndPlay 델리게이트 수신 → 저장
	 *
	 * 📌 처리 흐름:
	 *   1. ControllerToPlayerIdMap에서 PlayerId 찾기
	 *   2. SavedItems에 장착 정보가 없으면 → CachedPlayerData에서 병합
	 *   3. SaveCollectedItems(PlayerId, MergedItems)
	 *
	 * ⚠️ 주의:
	 *   이 함수는 UFUNCTION()이어야 함 — Dynamic Delegate 바인딩 필요
	 */
	UFUNCTION()
	virtual void OnInventoryControllerEndPlay(
		AInv_PlayerController* PlayerController,
		const TArray<FInv_SavedItemData>& SavedItems);

	// ════════════════════════════════════════════════════════════════
	// 📂 로드 — 자식 클래스에서 Super:: 호출
	// ════════════════════════════════════════════════════════════════

	/**
	 * 인벤토리 로드 후 클라이언트 전송
	 *
	 * 📌 처리 흐름 (Phase 4 — CDO 기반, SpawnActor 제거):
	 *   1. GetPlayerSaveId(PC) → PlayerId
	 *   2. InventorySaveGame->LoadPlayer(PlayerId, LoadedData)
	 *   3. 각 아이템에 대해:
	 *      a. ResolveItemClass(ItemType) → ActorClass (게임별 override)
	 *      b. FindItemComponentTemplate(CDO/SCS) → Manifest 복사
	 *      c. 부착물 복원 (CDO 기반), Fragment 역직렬화
	 *      d. AddItemFromManifest() → 인벤토리에 직접 추가
	 *      e. 그리드 위치 복원 (SetLastEntryGridPosition)
	 *   4. 장착 복원:
	 *      a. 데디서버: OnItemEquipped.Broadcast() 실행
	 *      b. 리슨서버: 스킵 (Client RPC 경로에서 처리 — 이중 실행 방지)
	 *   5. Client_ReceiveInventoryData() RPC로 클라이언트에 전송
	 *
	 * ⚠️ 리슨서버 주의:
	 *   4번에서 GetNetMode() == NM_DedicatedServer 체크 필수
	 *   리슨서버 호스트에서 Broadcast하면 Client RPC에서 또 실행되어 이중 장착
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 로드",
		meta = (DisplayName = "인벤토리 로드 및 전송(LoadAndSendInventoryToClient)"))
	virtual void LoadAndSendInventoryToClient(APlayerController* PC);

	/**
	 * 플레이어 인벤토리 데이터만 로드 (스폰하지 않음)
	 *
	 * @param PlayerId  대상 플레이어 ID
	 * @param OutData   로드된 데이터 (출력)
	 * @return true = 데이터 있음
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "인벤토리 로드",
		meta = (DisplayName = "인벤토리 데이터 로드(LoadPlayerInventoryData)"))
	bool LoadPlayerInventoryData(const FString& PlayerId, FInv_PlayerSaveData& OutData) const;

	// ════════════════════════════════════════════════════════════════
	// 🔧 게임별 Override 포인트
	// ════════════════════════════════════════════════════════════════

	/**
	 * ItemType → ActorClass 매핑 (게임별로 override)
	 *
	 * 📌 기본 구현: nullptr 반환 (아이템 스폰 안 함)
	 *
	 * @param ItemType  아이템 GameplayTag
	 * @return 스폰할 Actor 클래스, nullptr이면 해당 아이템 스킵
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 로드",
		meta = (DisplayName = "아이템 클래스 결정(ResolveItemClass)"))
	virtual TSubclassOf<AActor> ResolveItemClass(const FGameplayTag& ItemType);

	/**
	 * 플레이어 고유 ID 가져오기 (게임별로 override)
	 *
	 * 📌 기본 구현:
	 *   PlayerState->GetUniqueId().ToString()
	 *   없으면 ControllerToPlayerIdMap에서 폴백 검색
	 *
	 * @param PC  대상 PlayerController
	 * @return 플레이어 고유 ID 문자열 (빈 문자열 = 실패)
	 */
	virtual FString GetPlayerSaveId(APlayerController* PC) const;

	// ════════════════════════════════════════════════════════════════
	// ⏰ 자동저장
	// ════════════════════════════════════════════════════════════════

	/** 자동저장 타이머 시작 — BeginPlay()에서 자동 호출 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 저장",
		meta = (DisplayName = "자동저장 시작(StartAutoSave)"))
	void StartAutoSave();

	/** 자동저장 타이머 중지 — EndPlay()에서 자동 호출 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 저장",
		meta = (DisplayName = "자동저장 중지(StopAutoSave)"))
	void StopAutoSave();

	/** 수동으로 자동저장 즉시 실행 (디버그/BP용) */
	UFUNCTION(BlueprintCallable, Category = "인벤토리 저장",
		meta = (DisplayName = "강제 자동저장(ForceAutoSave)"))
	void ForceAutoSave();

	// ════════════════════════════════════════════════════════════════
	// 📦 캐시 관리
	// ════════════════════════════════════════════════════════════════

	/** 캐시에 데이터 저장 — SaveCollectedItems() 내부에서 자동 호출됨 */
	void CachePlayerData(const FString& PlayerId, const FInv_PlayerSaveData& Data);

	/** 캐시에서 데이터 조회 — 없으면 nullptr */
	FInv_PlayerSaveData* GetCachedData(const FString& PlayerId);

	/**
	 * 캐시 지연 삭제
	 * Logout() → EndPlay() 순서 문제로 즉시 삭제하면 장착 병합 불가
	 * → Delay초 후 삭제
	 */
	void RemoveCachedDataDeferred(const FString& PlayerId, float Delay = 2.0f);

	// ════════════════════════════════════════════════════════════════
	// 🔧 유틸리티
	// ════════════════════════════════════════════════════════════════

	/**
	 * EquipmentComponent에서 장착 상태를 Items에 병합
	 *
	 * 📌 매칭 규칙:
	 *   ItemType이 같고 && 아직 bEquipped가 false인 첫 번째 아이템에 병합
	 *
	 * @param PC     대상 PlayerController
	 * @param Items  병합 대상 아이템 배열 (in-out)
	 */
	static void MergeEquipmentState(APlayerController* PC, TArray<FInv_SavedItemData>& Items);

	/**
	 * [Phase 4] CDO/SCS에서 UInv_ItemComponent 템플릿 추출
	 *
	 * Blueprint에서 추가된 컴포넌트는 CDO->FindComponentByClass()로 접근 불가.
	 * SimpleConstructionScript의 노드 트리에서 ComponentTemplate을 직접 탐색.
	 *
	 * @param ActorClass  아이템 Actor의 Blueprint 클래스
	 * @return UInv_ItemComponent 템플릿 (CDO 소유, 수정 금지!). 실패 시 nullptr.
	 */
	static UInv_ItemComponent* FindItemComponentTemplate(TSubclassOf<AActor> ActorClass);

	/** Controller에 EndPlay 델리게이트 바인딩 */
	void BindInventoryEndPlay(AInv_PlayerController* InvPC);

	/** ControllerToPlayerIdMap에 등록 */
	void RegisterControllerPlayerId(AController* Controller, const FString& PlayerId);

	// ════════════════════════════════════════════════════════════════
	// 🎛️ 설정 — BP 디테일 패널에서 조정 가능
	// ════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리 저장",
		meta = (DisplayName = "자동저장 활성화(bAutoSaveEnabled)"))
	bool bAutoSaveEnabled = true;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리 저장",
		meta = (DisplayName = "자동저장 주기(AutoSaveIntervalSeconds)",
				ClampMin = "30.0", UIMin = "30.0"))
	float AutoSaveIntervalSeconds = 300.0f;

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리 저장",
		meta = (DisplayName = "저장 슬롯 이름(SaveSlotName)"))
	FString InventorySaveSlotName = TEXT("InventorySave");

	UPROPERTY(EditDefaultsOnly, Category = "인벤토리 저장",
		meta = (DisplayName = "리슨서버 종료 시 강제저장(bForceSaveOnListenServerShutdown)"))
	bool bForceSaveOnListenServerShutdown = true;

	// ════════════════════════════════════════════════════════════════
	// 🆕 [Phase 5] 서버 권위 자동저장
	// ════════════════════════════════════════════════════════════════

	/**
	 * [Phase 5] 서버 직접 수집 자동저장 활성화
	 *
	 * true = 자동저장 시 RPC 없이 서버 Entry의 GridIndex/GridCategory 사용
	 * false = 기존 방식 (클라이언트 RPC 왕복)
	 *
	 * 📌 장점:
	 *   - 자동저장 시 RPC 0회 (네트워크 부하 제거)
	 *   - 클라이언트 응답 대기 불필요 (타임아웃 문제 없음)
	 *   - 미응답 플레이어 데이터 손실 없음
	 *
	 * 📌 전제조건:
	 *   - 아이템 이동/Split 시 Server_UpdateItemGridPosition RPC로 서버 동기화 필수
	 *   - UI에서 Grid 위치 변경 시마다 RPC 호출되어야 함
	 */
	UPROPERTY(EditDefaultsOnly, Category = "인벤토리 저장",
		meta = (DisplayName = "[Phase 5] 서버 직접 저장(bUseServerDirectSave)"))
	bool bUseServerDirectSave = true;

	/**
	 * [Phase 5] 서버에서 직접 인벤토리 수집 후 저장
	 *
	 * RPC 없이 서버의 InventoryComponent->CollectInventoryDataForSave()를 직접 호출.
	 * Entry에 저장된 GridIndex/GridCategory를 사용하므로
	 * 클라이언트에서 Server_UpdateItemGridPosition RPC로 미리 동기화되어 있어야 함.
	 *
	 * @return 저장된 플레이어 수
	 */
	int32 SaveAllPlayersInventoryDirect();

private:
	// ── 자동저장 내부 ──

	/** 자동저장 타이머 콜백 */
	void OnAutoSaveTimer();

	/** 전체 플레이어에게 인벤토리 상태 요청 RPC 발송 */
	void RequestAllPlayersInventoryState();

	/** 단일 플레이어에게 인벤토리 상태 요청 */
	void RequestPlayerInventoryState(APlayerController* PC);

	/** 클라이언트로부터 인벤토리 상태 수신 → 저장 */
	UFUNCTION()
	void OnPlayerInventoryStateReceived(
		AInv_PlayerController* PlayerController,
		const TArray<FInv_SavedItemData>& SavedItems);

	// ── 내부 데이터 ──

	/** SaveGame 인스턴스 (BeginPlay에서 LoadOrCreate) */
	UPROPERTY()
	TObjectPtr<UInv_InventorySaveGame> InventorySaveGame;

	/** 플레이어별 캐시 (Logout↔EndPlay 타이밍 문제 해결용) */
	UPROPERTY()
	TMap<FString, FInv_PlayerSaveData> CachedPlayerData;

protected:
	/**
	 * Controller → PlayerId 매핑
	 * EndPlay 시점에 PlayerState가 무효화되어 PlayerId를 못 가져올 수 있으므로
	 * 미리 등록해두는 맵
	 */
	UPROPERTY()
	TMap<AController*, FString> ControllerToPlayerIdMap;

private:
	/** 자동저장 타이머 핸들 */
	FTimerHandle AutoSaveTimerHandle;

	// ── Phase 1: 자동저장 배칭 ──

	/** 자동저장 배칭 진행 중 여부 */
	bool bAutoSaveBatchInProgress = false;

	/** 아직 응답을 받지 못한 플레이어 수 */
	int32 PendingAutoSaveCount = 0;

	/** 배칭 타임아웃 타이머 핸들 */
	FTimerHandle AutoSaveBatchTimeoutHandle;

	/** 배칭 타임아웃 시간 (초) — 이 시간 안에 미응답 플레이어는 무시 */
	static constexpr float AutoSaveBatchTimeoutSeconds = 5.0f;

	/** 배칭된 데이터를 디스크에 1회 기록 */
	void FlushAutoSaveBatch();

	/** 배칭 타임아웃 콜백 */
	void OnAutoSaveBatchTimeout();

	// ── Phase 2: 비동기 저장 중복 방지 ──

	/** 비동기 디스크 저장이 진행 중인지 여부 */
	bool bAsyncSaveInProgress = false;
};
