// ════════════════════════════════════════════════════════════════════════════════
// Inv_SaveTypes.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    인벤토리 저장 시스템에서 사용하는 데이터 구조체와 SaveGame 클래스 정의
//    플러그인 독립 — Helluna 게임 모듈에 의존하지 않음
//
// 📌 포함 내용:
//    - FInv_PlayerSaveData: 플레이어 1명의 인벤토리 저장 데이터
//    - UInv_InventorySaveGame: .sav 파일 I/O를 담당하는 SaveGame 클래스
//
// 📌 사용 위치:
//    - AInv_SaveGameMode: 저장/로드 시 이 구조체 사용
//    - 자식 GameMode: Super:: 호출 시 이 구조체로 데이터 주고받음
//
// 📌 설계 의도:
//    기존에는 FInv_SavedItemData(플러그인) ↔ FHellunaInventoryItemData(게임모듈)
//    변환을 6군데에서 반복했음. 이제 FInv_SavedItemData를 직접 직렬화하여
//    변환 코드를 완전히 제거
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData
#include "Inv_SaveTypes.generated.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📦 FInv_PlayerSaveData — 플레이어 1명의 인벤토리 저장 데이터
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    한 플레이어가 소유한 모든 아이템의 스냅샷
//    FInv_SavedItemData 배열을 그대로 저장 (변환 없음)
//
// 📌 저장되는 정보 (FInv_SavedItemData 1개당):
//    - ItemType (GameplayTag): 아이템 종류
//    - StackCount (int32): 스택 수량
//    - GridPosition (FIntPoint): 인벤토리 그리드 위치
//    - GridCategory (uint8): 0=장비, 1=소모품, 2=재료
//    - bEquipped (bool): 장착 여부
//    - WeaponSlotIndex (int32): 장착 슬롯 번호 (미장착 시 -1)
//    - Attachments (TArray<FInv_SavedAttachmentData>): 무기 부착물 목록 (v2+)
//
// ════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct INVENTORY_API FInv_PlayerSaveData
{
	GENERATED_BODY()

	/** 인벤토리 아이템 목록 (장착 아이템 포함) */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "인벤토리 저장",
		meta = (DisplayName = "아이템 목록(Items)"))
	TArray<FInv_SavedItemData> Items;

	/** 마지막 저장 시간 — 디버깅 및 데이터 검증용 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "인벤토리 저장",
		meta = (DisplayName = "마지막 저장 시간(LastSaveTime)"))
	FDateTime LastSaveTime = FDateTime::MinValue();

	/**
	 * 저장 데이터 버전 — 데이터 구조 변경 시 마이그레이션용
	 *
	 * Version 1: 기본 인벤토리 (아이템 + 장착 상태)
	 * Version 2: 부착물 시스템 추가 (FInv_SavedAttachmentData)
	 * Version 3: Manifest Fragment 직렬화 추가 (SerializedManifest)
	 *            → 랜덤 스탯/부착물 Modifier 값 보존
	 *            → v1/v2 하위 호환: SerializedManifest가 빈 배열이면 CDO 기본값 사용
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "인벤토리 저장",
		meta = (DisplayName = "저장 버전(SaveVersion)"))
	int32 SaveVersion = 3;

	/** 비어있는지 확인 */
	bool IsEmpty() const { return Items.Num() == 0; }

	/** 아이템 개수 반환 */
	int32 GetItemCount() const { return Items.Num(); }

	/** 디버그 문자열 */
	FString ToString() const
	{
		return FString::Printf(TEXT("아이템 %d개, 마지막 저장: %s, 버전: %d"),
			Items.Num(), *LastSaveTime.ToString(), SaveVersion);
	}
};

// ════════════════════════════════════════════════════════════════════════════════
// 📦 UInv_InventorySaveGame — .sav 파일 I/O
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    플레이어별 인벤토리 데이터를 .sav 파일로 저장/로드
//    AInv_SaveGameMode가 이 클래스의 인스턴스를 소유
//
// 📌 저장 구조:
//    TMap<FString, FInv_PlayerSaveData>
//    Key = PlayerId, Value = 해당 플레이어의 인벤토리 스냅샷
//
// 📌 파일 위치:
//    Saved/SaveGames/{SlotName}.sav
//    SlotName은 AInv_SaveGameMode의 InventorySaveSlotName으로 설정
//
// 📌 사용법:
//    // 로드 (없으면 새로 생성)
//    UInv_InventorySaveGame* SG = UInv_InventorySaveGame::LoadOrCreate("MySlot");
//
//    // 저장
//    SG->SavePlayer("player_001", PlayerData);
//    UInv_InventorySaveGame::SaveToDisk(SG, "MySlot");
//
//    // 로드
//    FInv_PlayerSaveData Data;
//    if (SG->LoadPlayer("player_001", Data)) { ... }
//
// ════════════════════════════════════════════════════════════════════════════════
UCLASS()
class INVENTORY_API UInv_InventorySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 플레이어별 인벤토리 데이터 (Key: PlayerId) */
	UPROPERTY(SaveGame)
	TMap<FString, FInv_PlayerSaveData> PlayerInventories;

	// ── 플레이어별 데이터 관리 ──

	/** 메모리에 저장 (파일 저장은 SaveToDisk 별도 호출) */
	void SavePlayer(const FString& PlayerId, const FInv_PlayerSaveData& Data);

	/** 플레이어 데이터 로드 — 없으면 false 반환 */
	bool LoadPlayer(const FString& PlayerId, FInv_PlayerSaveData& OutData) const;

	/** 플레이어 데이터 존재 확인 */
	bool HasPlayer(const FString& PlayerId) const;

	/** 플레이어 데이터 삭제 — 없었으면 false 반환 */
	bool RemovePlayer(const FString& PlayerId);

	// ── 파일 I/O (정적 함수) ──

	/** .sav 파일에서 로드. 없으면 새 인스턴스 생성 */
	static UInv_InventorySaveGame* LoadOrCreate(const FString& SlotName);

	/** .sav 파일에 저장 (동기 — 게임 스레드 블로킹) */
	static bool SaveToDisk(UInv_InventorySaveGame* SaveGame, const FString& SlotName);

	/** .sav 파일에 비동기 저장 (게임 스레드 블로킹 없음) */
	static void AsyncSaveToDisk(UInv_InventorySaveGame* SaveGame, const FString& SlotName, TFunction<void(bool bSuccess)> OnComplete = nullptr);
};
