// HellunaInventorySaveGame.cpp
// 인벤토리 데이터를 저장하는 SaveGame 클래스 구현
// 
// ============================================
// 📌 작성자: Claude (Anthropic)
// 📌 작성일: 2025-01-29
// ============================================

#include "Inventory/HellunaInventorySaveGame.h"
#include "Kismet/GameplayStatics.h"

// ============================================
// 📌 상수 정의
// ============================================
const FString UHellunaInventorySaveGame::SaveSlotName = TEXT("HellunaInventory");
const int32 UHellunaInventorySaveGame::UserIndex = 0;

// ============================================
// 📌 생성자
// ============================================
UHellunaInventorySaveGame::UHellunaInventorySaveGame()
{
	// 기본 생성자
}

// ============================================
// 📌 플레이어별 인벤토리 관리 함수
// ============================================

void UHellunaInventorySaveGame::SavePlayerInventory(const FString& PlayerUniqueId, const FHellunaPlayerInventoryData& Data)
{
	if (PlayerUniqueId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[InventorySaveGame] SavePlayerInventory: PlayerUniqueId가 비어있습니다!"));
		return;
	}

	// 저장 시간 업데이트된 복사본 생성
	FHellunaPlayerInventoryData DataToSave = Data;
	DataToSave.LastSaveTime = FDateTime::Now();

	// 기존 데이터 덮어쓰기 또는 새로 추가
	PlayerInventories.Add(PlayerUniqueId, DataToSave);

	UE_LOG(LogTemp, Log, TEXT("[InventorySaveGame] SavePlayerInventory: %s 저장 완료 (아이템 %d개)"),
		*PlayerUniqueId, DataToSave.Items.Num());

	// 디버그: 각 아이템 출력
#if WITH_EDITOR
	for (int32 i = 0; i < DataToSave.Items.Num(); i++)
	{
		const FHellunaInventoryItemData& Item = DataToSave.Items[i];
		UE_LOG(LogTemp, Log, TEXT("  [%d] %s"), i, *Item.ToString());
	}
#endif
}

bool UHellunaInventorySaveGame::LoadPlayerInventory(const FString& PlayerUniqueId, FHellunaPlayerInventoryData& OutData) const
{
	if (PlayerUniqueId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[InventorySaveGame] LoadPlayerInventory: PlayerUniqueId가 비어있습니다!"));
		return false;
	}

	const FHellunaPlayerInventoryData* FoundData = PlayerInventories.Find(PlayerUniqueId);
	if (FoundData)
	{
		OutData = *FoundData;
		UE_LOG(LogTemp, Log, TEXT("[InventorySaveGame] LoadPlayerInventory: %s 로드 완료 (아이템 %d개)"),
			*PlayerUniqueId, OutData.Items.Num());
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[InventorySaveGame] LoadPlayerInventory: %s 데이터 없음 (신규 플레이어)"),
		*PlayerUniqueId);
	return false;
}

bool UHellunaInventorySaveGame::HasPlayerData(const FString& PlayerUniqueId) const
{
	return PlayerInventories.Contains(PlayerUniqueId);
}

bool UHellunaInventorySaveGame::RemovePlayerData(const FString& PlayerUniqueId)
{
	if (PlayerInventories.Contains(PlayerUniqueId))
	{
		PlayerInventories.Remove(PlayerUniqueId);
		UE_LOG(LogTemp, Log, TEXT("[InventorySaveGame] RemovePlayerData: %s 삭제 완료"), *PlayerUniqueId);
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[InventorySaveGame] RemovePlayerData: %s 데이터 없음"), *PlayerUniqueId);
	return false;
}

// ============================================
// 📌 정적 유틸리티 함수 (파일 I/O)
// ============================================

UHellunaInventorySaveGame* UHellunaInventorySaveGame::LoadOrCreate()
{
	UHellunaInventorySaveGame* LoadedSaveGame = nullptr;

	// 기존 저장 파일이 있는지 확인
	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, UserIndex))
	{
		// 기존 파일 로드
		LoadedSaveGame = Cast<UHellunaInventorySaveGame>(
			UGameplayStatics::LoadGameFromSlot(SaveSlotName, UserIndex)
		);

		if (LoadedSaveGame)
		{
			UE_LOG(LogTemp, Log, TEXT("[InventorySaveGame] LoadOrCreate: 기존 인벤토리 데이터 로드 성공 (플레이어 %d명)"),
				LoadedSaveGame->PlayerInventories.Num());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[InventorySaveGame] LoadOrCreate: 파일은 있지만 로드 실패! 새로 생성합니다."));
		}
	}

	// 로드 실패하거나 파일이 없으면 새로 생성
	if (!LoadedSaveGame)
	{
		LoadedSaveGame = Cast<UHellunaInventorySaveGame>(
			UGameplayStatics::CreateSaveGameObject(UHellunaInventorySaveGame::StaticClass())
		);

		UE_LOG(LogTemp, Log, TEXT("[InventorySaveGame] LoadOrCreate: 새 인벤토리 데이터 생성"));
	}

	return LoadedSaveGame;
}

bool UHellunaInventorySaveGame::Save(UHellunaInventorySaveGame* InventorySaveGame)
{
	if (!InventorySaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("[InventorySaveGame] Save: InventorySaveGame이 nullptr입니다!"));
		return false;
	}

	bool bSuccess = UGameplayStatics::SaveGameToSlot(InventorySaveGame, SaveSlotName, UserIndex);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[InventorySaveGame] Save: 저장 성공 (플레이어 %d명)"),
			InventorySaveGame->PlayerInventories.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[InventorySaveGame] Save: 저장 실패!"));
	}

	return bSuccess;
}

// ============================================
// 📌 디버그 함수
// ============================================

void UHellunaInventorySaveGame::DebugPrintAllData() const
{
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║   [InventorySaveGame] 저장 데이터 요약                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
	UE_LOG(LogTemp, Warning, TEXT("📊 총 플레이어: %d명"), PlayerInventories.Num());
	UE_LOG(LogTemp, Warning, TEXT("────────────────────────────────────────────────────────────"));

	int32 PlayerIndex = 0;
	for (const auto& Pair : PlayerInventories)
	{
		const FString& PlayerId = Pair.Key;
		const FHellunaPlayerInventoryData& Data = Pair.Value;

		UE_LOG(LogTemp, Warning, TEXT(""));
		UE_LOG(LogTemp, Warning, TEXT("👤 [%d] 플레이어: %s"), PlayerIndex, *PlayerId);
		UE_LOG(LogTemp, Warning, TEXT("   📦 아이템: %d개"), Data.Items.Num());
		UE_LOG(LogTemp, Warning, TEXT("   🕐 마지막 저장: %s"), *Data.LastSaveTime.ToString());
		UE_LOG(LogTemp, Warning, TEXT("   📋 버전: %d"), Data.SaveVersion);

		// 각 아이템 출력
		for (int32 i = 0; i < Data.Items.Num(); i++)
		{
			const FHellunaInventoryItemData& Item = Data.Items[i];
			FString EquipStatus = Item.EquipSlotIndex >= 0 
				? FString::Printf(TEXT("장착[%d]"), Item.EquipSlotIndex)
				: TEXT("미장착");
			
			const TCHAR* CategoryNames[] = { TEXT("장비"), TEXT("소모품"), TEXT("재료") };
			const TCHAR* CategoryName = Item.GridCategory < 3 ? CategoryNames[Item.GridCategory] : TEXT("???");
			
			UE_LOG(LogTemp, Warning, TEXT("      [%d] %s x%d @ Grid%d(%s) (%d,%d) %s"),
				i,
				*Item.ItemType.ToString(),
				Item.StackCount,
				Item.GridCategory,
				CategoryName,
				Item.GridPosition.X, Item.GridPosition.Y,
				*EquipStatus);
		}

		PlayerIndex++;
	}

	UE_LOG(LogTemp, Warning, TEXT("────────────────────────────────────────────────────────────"));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║   [InventorySaveGame] 요약 끝                              ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
}
