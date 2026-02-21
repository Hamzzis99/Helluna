// ════════════════════════════════════════════════════════════════════════════════
// Inv_SaveTypes.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    UInv_InventorySaveGame의 SavePlayer/LoadPlayer/파일I/O 구현
//
// 📌 구현 로직:
//    - SavePlayer(): TMap에 Add/Overwrite
//    - LoadPlayer(): TMap에서 Find → 복사
//    - LoadOrCreate(): UGameplayStatics::LoadGameFromSlot() → 없으면 CreateSaveGameObject()
//    - SaveToDisk(): UGameplayStatics::SaveGameToSlot()
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Persistence/Inv_SaveTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Inventory.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SavePlayer — 메모리에 플레이어 데이터 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리:
//    TMap에 Add (이미 있으면 덮어쓰기)
//    ⚠️ 파일 저장은 하지 않음 — SaveToDisk() 별도 호출 필요
//
// ════════════════════════════════════════════════════════════════════════════════
void UInv_InventorySaveGame::SavePlayer(const FString& PlayerId, const FInv_PlayerSaveData& Data)
{
	PlayerInventories.Add(PlayerId, Data);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 LoadPlayer — 메모리에서 플레이어 데이터 로드
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 반환:
//    true = 데이터 있음, OutData에 복사됨
//    false = 해당 PlayerId 없음
//
// ════════════════════════════════════════════════════════════════════════════════
bool UInv_InventorySaveGame::LoadPlayer(const FString& PlayerId, FInv_PlayerSaveData& OutData) const
{
	if (const FInv_PlayerSaveData* Found = PlayerInventories.Find(PlayerId))
	{
		OutData = *Found;
		return true;
	}
	return false;
}

bool UInv_InventorySaveGame::HasPlayer(const FString& PlayerId) const
{
	return PlayerInventories.Contains(PlayerId);
}

bool UInv_InventorySaveGame::RemovePlayer(const FString& PlayerId)
{
	return PlayerInventories.Remove(PlayerId) > 0;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 LoadOrCreate — .sav 파일에서 로드 (없으면 새로 생성)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. UGameplayStatics::LoadGameFromSlot(SlotName, 0)
//    2. 성공 → Cast<UInv_InventorySaveGame>
//    3. 실패 → UGameplayStatics::CreateSaveGameObject() 으로 새 인스턴스
//
// 📌 호출 시점:
//    AInv_SaveGameMode::BeginPlay()에서 1회 호출
//
// ════════════════════════════════════════════════════════════════════════════════
UInv_InventorySaveGame* UInv_InventorySaveGame::LoadOrCreate(const FString& SlotName)
{
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		USaveGame* Loaded = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
		if (UInv_InventorySaveGame* Casted = Cast<UInv_InventorySaveGame>(Loaded))
		{
			UE_LOG(LogInventory, Log, TEXT("[InventorySaveGame] LoadOrCreate: 기존 데이터 로드 성공 (플레이어 %d명)"),
				Casted->PlayerInventories.Num());
			return Casted;
		}
	}

	UE_LOG(LogInventory, Log, TEXT("[InventorySaveGame] LoadOrCreate: 새 인벤토리 데이터 생성 (Slot: %s)"), *SlotName);
	return Cast<UInv_InventorySaveGame>(
		UGameplayStatics::CreateSaveGameObject(UInv_InventorySaveGame::StaticClass()));
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SaveToDisk — .sav 파일에 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리:
//    UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0)
//
// 📌 호출 시점:
//    - SaveCollectedItems() 호출 후
//    - 자동저장 타이머 만료 시
//    - 로그아웃/EndPlay 시
//
// ════════════════════════════════════════════════════════════════════════════════
bool UInv_InventorySaveGame::SaveToDisk(UInv_InventorySaveGame* SaveGame, const FString& SlotName)
{
	if (!IsValid(SaveGame)) return false;
	return UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, 0);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 AsyncSaveToDisk — .sav 파일에 비동기 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리:
//    UGameplayStatics::AsyncSaveGameToSlot() → 백그라운드 스레드에서 디스크 쓰기
//    완료 시 OnComplete 콜백 호출 (게임 스레드에서)
//
// 📌 호출 시점:
//    FlushAutoSaveBatch()에서 자동저장 배칭 완료 시
//
// 📌 주의:
//    로그아웃/EndPlay 경로에서는 사용하지 않음 (월드 파괴 전 저장 완료 보장 필요)
//
// ════════════════════════════════════════════════════════════════════════════════
void UInv_InventorySaveGame::AsyncSaveToDisk(UInv_InventorySaveGame* SaveGame, const FString& SlotName, TFunction<void(bool bSuccess)> OnComplete)
{
	if (!IsValid(SaveGame))
	{
		if (OnComplete) OnComplete(false);
		return;
	}

	FAsyncSaveGameToSlotDelegate AsyncDelegate;
	AsyncDelegate.BindLambda([OnComplete](const FString& SavedSlotName, const int32 UserIndex, bool bSuccess)
	{
#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 2 비동기] 💾 AsyncSave 완료! Slot=%s (성공=%s)"),
			*SavedSlotName, bSuccess ? TEXT("Y") : TEXT("N"));
#endif
		if (OnComplete) OnComplete(bSuccess);
	});

	UGameplayStatics::AsyncSaveGameToSlot(SaveGame, SlotName, 0, AsyncDelegate);

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning, TEXT("[Phase 2 비동기] 🚀 AsyncSave 요청됨! Slot=%s"), *SlotName);
#endif
}
