// HellunaInventorySaveGame.h
// 인벤토리 데이터를 저장하는 SaveGame 클래스
// 
// ============================================
// 📌 역할:
// - 플레이어별 인벤토리 데이터를 서버에 저장 (.sav 파일)
// - 아이템 종류, 수량, 위치, 장착 상태 관리
// - 300초 주기 자동 저장 + 로그아웃/맵이동 시 저장
// 
// 📌 저장 위치:
// Saved/SaveGames/HellunaInventory.sav
// 
// 📌 사용 위치:
// - HellunaDefenseGameMode::BeginPlay() 에서 LoadOrCreate()로 로드
// - HellunaDefenseGameMode::SavePlayerInventory() 에서 저장
// - HellunaDefenseGameMode::LoadPlayerInventory() 에서 로드
// 
// ============================================
// 📌 인벤토리 저장/로드 흐름:
// ============================================
// 
// [저장 흐름]
// ┌─────────────────────────────────────────────────────────────┐
// │ 트리거: 300초 주기 / 로그아웃 / 맵이동                       │
// │   ↓                                                          │
// │ GameMode::SavePlayerInventory(PlayerController)              │
// │   ↓                                                          │
// │ InventoryComponent에서 아이템 목록 추출                      │
// │   ↓                                                          │
// │ 각 UInv_InventoryItem → FHellunaInventoryItemData 변환       │
// │   - ItemManifest->ItemType → ItemType                        │
// │   - TotalStackCount → StackCount                             │
// │   - GridPosition → GridPosition                              │
// │   ↓                                                          │
// │ SaveGame->SavePlayerInventory(PlayerUniqueId, Data)          │
// │   ↓                                                          │
// │ UHellunaInventorySaveGame::Save() → 파일 저장                │
// └─────────────────────────────────────────────────────────────┘
// 
// [로드 흐름]
// ┌─────────────────────────────────────────────────────────────┐
// │ 트리거: 로그인 성공 후 캐릭터 소환 시                        │
// │   ↓                                                          │
// │ GameMode::LoadPlayerInventory(PlayerController)              │
// │   ↓                                                          │
// │ SaveGame->LoadPlayerInventory(PlayerUniqueId, OutData)       │
// │   ↓                                                          │
// │ 각 FHellunaInventoryItemData 순회                            │
// │   ↓                                                          │
// │ ItemType → DataTable 매핑 → Actor 클래스 조회 (Phase 1)      │
// │   ↓                                                          │
// │ Actor 스폰 → InventoryComponent에 추가                       │
// │   ↓                                                          │
// │ GridPosition, StackCount, EquipSlotIndex 복원                │
// └─────────────────────────────────────────────────────────────┘
// 
// ============================================
// 📌 관련 클래스:
// ============================================
// - UHellunaInventorySaveGame : 인벤토리 데이터 저장 (이 파일)
// - UHellunaItemTypeMapping : ItemType → Actor 클래스 매핑 (Phase 1)
// - UInv_InventoryComponent : 런타임 인벤토리 관리
// - UInv_InventoryItem : 런타임 아이템 데이터
// - AHellunaPlayerState : PlayerUniqueId 저장
// - AHellunaDefenseGameMode : 저장/로드 호출
// 
// ============================================
// 📌 Phase 2 구현 범위:
// ============================================
// - [x] FHellunaInventoryItemData 구조체
// - [x] FHellunaPlayerInventoryData 구조체
// - [x] UHellunaInventorySaveGame 클래스
// - [x] LoadOrCreate(), Save() 정적 함수
// - [x] SavePlayerInventory(), LoadPlayerInventory() 함수
// 
// 📌 작성자: Claude (Anthropic)
// 📌 작성일: 2025-01-29
// ============================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "HellunaInventorySaveGame.generated.h"

// ============================================
// 📦 단일 아이템 저장 데이터
// ============================================

/**
 * 단일 인벤토리 아이템의 저장 데이터
 * 런타임 UInv_InventoryItem에서 필요한 정보만 추출
 */
USTRUCT(BlueprintType)
struct FHellunaInventoryItemData
{
	GENERATED_BODY()

	FHellunaInventoryItemData()
		: ItemType(FGameplayTag::EmptyTag)
		, StackCount(1)
		, GridPosition(FIntPoint(-1, -1))
		, EquipSlotIndex(-1)
	{
	}

	FHellunaInventoryItemData(const FGameplayTag& InItemType, int32 InStackCount, const FIntPoint& InGridPosition, int32 InEquipSlotIndex = -1)
		: ItemType(InItemType)
		, StackCount(InStackCount)
		, GridPosition(InGridPosition)
		, EquipSlotIndex(InEquipSlotIndex)
	{
	}

	/**
	 * 아이템 종류 (GameplayTag)
	 * Phase 1 DataTable 매핑으로 Actor 클래스 복원에 사용
	 * 예: "GameItems.Equipment.Weapons.Axe"
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	FGameplayTag ItemType;

	/**
	 * 스택 수량
	 * 포션 3개 스택 등
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	int32 StackCount;

	/**
	 * 인벤토리 Grid 위치
	 * X = Column, Y = Row
	 * (-1, -1) = 위치 미지정
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	FIntPoint GridPosition;

	/**
	 * 장착 슬롯 인덱스
	 * -1 = 미장착 (인벤토리에만 있음)
	 * 0~5 = 장착 슬롯 번호 (Phase 6에서 사용)
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	int32 EquipSlotIndex;

	/** 유효한 아이템 데이터인지 확인 */
	bool IsValid() const
	{
		return ItemType.IsValid() && StackCount > 0;
	}

	/** 디버그 문자열 반환 */
	FString ToString() const
	{
		return FString::Printf(TEXT("[%s x%d @ (%d,%d) Slot:%d]"),
			*ItemType.ToString(),
			StackCount,
			GridPosition.X, GridPosition.Y,
			EquipSlotIndex);
	}
};

// ============================================
// 📦 플레이어별 인벤토리 저장 데이터
// ============================================

/**
 * 한 플레이어의 전체 인벤토리 저장 데이터
 */
USTRUCT(BlueprintType)
struct FHellunaPlayerInventoryData
{
	GENERATED_BODY()

	FHellunaPlayerInventoryData()
		: LastSaveTime(FDateTime::MinValue())
		, SaveVersion(1)
	{
	}

	/**
	 * 인벤토리 아이템 목록
	 * 모든 소지 아이템 (장착 포함)
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	TArray<FHellunaInventoryItemData> Items;

	/**
	 * 마지막 저장 시간
	 * 디버깅 및 데이터 검증용
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	FDateTime LastSaveTime;

	/**
	 * 저장 데이터 버전
	 * 나중에 데이터 구조 변경 시 마이그레이션용
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "Inventory")
	int32 SaveVersion;

	/** 아이템 개수 반환 */
	int32 GetItemCount() const { return Items.Num(); }

	/** 비어있는지 확인 */
	bool IsEmpty() const { return Items.Num() == 0; }

	/** 모든 아이템 초기화 */
	void Clear()
	{
		Items.Empty();
		LastSaveTime = FDateTime::MinValue();
	}

	/** 디버그 문자열 반환 */
	FString ToString() const
	{
		return FString::Printf(TEXT("아이템 %d개, 마지막 저장: %s, 버전: %d"),
			Items.Num(),
			*LastSaveTime.ToString(),
			SaveVersion);
	}
};

// ============================================
// 📦 메인 SaveGame 클래스
// ============================================

/**
 * 인벤토리 데이터를 저장하는 SaveGame 클래스
 * 서버에서만 사용됨
 * 
 * 사용 예시:
 * // 로드
 * UHellunaInventorySaveGame* SaveGame = UHellunaInventorySaveGame::LoadOrCreate();
 * 
 * // 플레이어 인벤토리 로드
 * FHellunaPlayerInventoryData PlayerData;
 * if (SaveGame->LoadPlayerInventory(PlayerUniqueId, PlayerData))
 * {
 *     // 아이템 복원 로직
 * }
 * 
 * // 플레이어 인벤토리 저장
 * SaveGame->SavePlayerInventory(PlayerUniqueId, NewData);
 * UHellunaInventorySaveGame::Save(SaveGame);
 */
UCLASS()
class HELLUNA_API UHellunaInventorySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UHellunaInventorySaveGame();

	// ============================================
	// 📌 저장 슬롯 설정 (상수)
	// ============================================

	/** SaveGame 슬롯 이름 (파일명: HellunaInventory.sav) */
	static const FString SaveSlotName;

	/** 사용자 인덱스 (싱글 서버이므로 0 고정) */
	static const int32 UserIndex;

	// ============================================
	// 📌 저장 데이터
	// ============================================

	/**
	 * 플레이어별 인벤토리 데이터
	 * Key: PlayerUniqueId (PlayerState에서 가져옴)
	 * Value: 해당 플레이어의 인벤토리 데이터
	 */
	UPROPERTY(SaveGame)
	TMap<FString, FHellunaPlayerInventoryData> PlayerInventories;

	// ============================================
	// 📌 플레이어별 인벤토리 관리 함수
	// ============================================

	/**
	 * 플레이어 인벤토리 저장 (메모리에만, 파일 저장은 Save() 별도 호출)
	 * @param PlayerUniqueId - 플레이어 고유 ID
	 * @param Data - 저장할 인벤토리 데이터
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Save")
	void SavePlayerInventory(const FString& PlayerUniqueId, const FHellunaPlayerInventoryData& Data);

	/**
	 * 플레이어 인벤토리 로드
	 * @param PlayerUniqueId - 플레이어 고유 ID
	 * @param OutData - 로드된 인벤토리 데이터 (출력)
	 * @return 데이터가 존재하면 true, 없으면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Save")
	bool LoadPlayerInventory(const FString& PlayerUniqueId, FHellunaPlayerInventoryData& OutData) const;

	/**
	 * 플레이어 데이터 존재 확인
	 * @param PlayerUniqueId - 플레이어 고유 ID
	 * @return 데이터가 존재하면 true
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Inventory|Save")
	bool HasPlayerData(const FString& PlayerUniqueId) const;

	/**
	 * 플레이어 데이터 삭제
	 * @param PlayerUniqueId - 플레이어 고유 ID
	 * @return 삭제 성공하면 true (데이터가 없었으면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Save")
	bool RemovePlayerData(const FString& PlayerUniqueId);

	/**
	 * 저장된 플레이어 수 반환
	 * @return 저장된 플레이어 수
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Helluna|Inventory|Save")
	int32 GetPlayerCount() const { return PlayerInventories.Num(); }

	// ============================================
	// 📌 정적 유틸리티 함수 (파일 I/O)
	// ============================================

	/**
	 * 인벤토리 데이터 로드 (없으면 새로 생성)
	 * @return 로드된 InventorySaveGame 인스턴스
	 */
	static UHellunaInventorySaveGame* LoadOrCreate();

	/**
	 * 인벤토리 데이터를 파일에 저장
	 * @param InventorySaveGame - 저장할 인스턴스
	 * @return 저장 성공하면 true
	 */
	static bool Save(UHellunaInventorySaveGame* InventorySaveGame);

	// ============================================
	// 📌 디버그 함수
	// ============================================

	/**
	 * 모든 저장 데이터 요약 출력 (디버그용)
	 */
	UFUNCTION(BlueprintCallable, Category = "Helluna|Inventory|Debug")
	void DebugPrintAllData() const;
};
