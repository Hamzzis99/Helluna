// ════════════════════════════════════════════════════════════════════════════════
// Inv_SaveGameMode.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이 파일의 역할:
//    인벤토리 저장/로드를 독립적으로 처리하는 GameMode 기반 클래스
//    Helluna 게임 모듈의 HellunaBaseGameMode가 이 클래스를 상속
//
// 📌 상속 구조:
//    AGameMode → AInv_SaveGameMode → AHellunaBaseGameMode → AHellunaDefenseGameMode
//
// 📌 주요 기능:
//    💾 저장: SavePlayerInventory, SaveCollectedItems, SaveAllPlayersInventory
//    📂 로드: LoadAndSendInventoryToClient, LoadPlayerInventoryData
//    ⏰ 자동저장: StartAutoSave, StopAutoSave, ForceAutoSave
//    📦 캐시: CachePlayerData, GetCachedData, RemoveCachedDataDeferred
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Persistence/Inv_SaveGameMode.h"
#include "Inventory.h"
#include "Player/Inv_PlayerController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "GameplayTagContainer.h"

// ════════════════════════════════════════════════════════════════════════════════
// 📌 생성자
// ════════════════════════════════════════════════════════════════════════════════
AInv_SaveGameMode::AInv_SaveGameMode()
{
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 BeginPlay — SaveGame 로드 + 자동저장 시작
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority()) return;

	// SaveGame 로드
	InventorySaveGame = UInv_InventorySaveGame::LoadOrCreate(InventorySaveSlotName);

	// 자동저장 시작
	StartAutoSave();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 EndPlay — 리슨서버 종료 시 강제저장 + 자동저장 정리
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 리슨서버 호스트 종료 시 모든 플레이어 인벤토리 강제 저장
	if (bForceSaveOnListenServerShutdown &&
		(GetNetMode() == NM_ListenServer || EndPlayReason == EEndPlayReason::Quit))
	{
		UE_LOG(LogInventory, Warning, TEXT("[SaveGameMode] EndPlay - 리슨서버 종료 감지, 인벤토리 강제 저장 시작"));
		SaveAllPlayersInventory();
	}

	StopAutoSave();
	Super::EndPlay(EndPlayReason);
}

// ════════════════════════════════════════════════════════════════════════════════
// 💾 저장 시스템
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SavePlayerInventory — 단일 플레이어 인벤토리 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. PC → FindComponentByClass<UInv_InventoryComponent>()
//    2. InvComp->CollectInventoryDataForSave() → TArray<FInv_SavedItemData>
//    3. MergeEquipmentState(PC, Items) → 장착 정보 병합
//    4. SaveCollectedItems(PlayerId, Items) → 디스크 저장
//
// 📌 리슨서버 고려사항:
//    없음 — 데이터 수집은 서버에서만 실행되므로 넷모드 분기 불필요
//
// ════════════════════════════════════════════════════════════════════════════════
bool AInv_SaveGameMode::SavePlayerInventory(const FString& PlayerId, APlayerController* PC)
{
	if (PlayerId.IsEmpty() || !IsValid(PC)) return false;

	// ── 1단계: InvComp에서 아이템 수집 ──
	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return false;

	TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();

	// ── 2단계: EquipComp에서 장착 상태 병합 ──
	MergeEquipmentState(PC, CollectedItems);

	// ── 3단계: 디스크 저장 ──
	if (CollectedItems.Num() == 0) return false;

	return SaveCollectedItems(PlayerId, CollectedItems);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SaveCollectedItems — 이미 수집된 데이터로 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. FInv_PlayerSaveData 생성 (LastSaveTime = Now)
//    2. InventorySaveGame->SavePlayer() → 메모리 저장
//    3. UInv_InventorySaveGame::SaveToDisk() → .sav 파일 저장
//    4. CachedPlayerData에도 캐싱
//
// ════════════════════════════════════════════════════════════════════════════════
bool AInv_SaveGameMode::SaveCollectedItems(const FString& PlayerId, const TArray<FInv_SavedItemData>& Items)
{
	if (PlayerId.IsEmpty() || Items.Num() == 0) return false;

	// SaveData 생성
	FInv_PlayerSaveData SaveData;
	SaveData.Items = Items;
	SaveData.LastSaveTime = FDateTime::Now();

	// 메모리 저장 + 파일 저장
	if (IsValid(InventorySaveGame))
	{
		InventorySaveGame->SavePlayer(PlayerId, SaveData);
		UInv_InventorySaveGame::SaveToDisk(InventorySaveGame, InventorySaveSlotName);
	}

	// 캐시에 저장 (Logout↔EndPlay 타이밍 문제 해결용)
	CachePlayerData(PlayerId, SaveData);

	return true;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 SaveAllPlayersInventory — 전체 플레이어 인벤토리 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. GetWorld()->GetPlayerControllerIterator()로 전체 PC 순회
//    2. 각 PC에 대해 GetPlayerSaveId() → PlayerId
//    3. SavePlayerInventory(PlayerId, PC) 호출
//
// ════════════════════════════════════════════════════════════════════════════════
int32 AInv_SaveGameMode::SaveAllPlayersInventory()
{
	int32 SavedCount = 0;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		FString PlayerId = GetPlayerSaveId(PC);
		if (PlayerId.IsEmpty()) continue;

		if (SavePlayerInventory(PlayerId, PC))
		{
			SavedCount++;
		}
	}

	return SavedCount;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnPlayerInventoryLogout — 플레이어 로그아웃 시 저장 처리
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. SavePlayerInventory(PlayerId, PC)
//    2. CachedPlayerData 지연 삭제 (2초 후 — EndPlay 장착 병합 대기)
//
// ⚠️ 주의:
//    Logout()은 EndPlay()보다 먼저 호출됨
//    캐시를 즉시 삭제하면 EndPlay에서 장착 정보 병합 불가
//    → 2초 지연 삭제로 해결
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::OnPlayerInventoryLogout(const FString& PlayerId, APlayerController* PC)
{
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] ❌ PlayerId 비어있음! 저장 중단!"));
		return;
	}
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);

	// 인벤토리 저장
	SavePlayerInventory(PlayerId, PC);

	// CachedPlayerData는 OnInventoryControllerEndPlay()에서 장착 정보 병합에 필요하므로
	// Logout 시점에서 즉시 삭제하지 않고, 타이머로 지연 삭제
	RemoveCachedDataDeferred(PlayerId, 2.0f);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnInventoryControllerEndPlay — Controller EndPlay 델리게이트 핸들러
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. ControllerToPlayerIdMap에서 PlayerId 찾기
//    2. SavedItems에 장착 정보가 없으면 → CachedPlayerData에서 병합
//       (EndPlay 시점에 EquipmentComponent가 이미 파괴되었을 수 있음)
//    3. SaveCollectedItems(PlayerId, MergedItems)
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::OnInventoryControllerEndPlay(
	AInv_PlayerController* PlayerController,
	const TArray<FInv_SavedItemData>& SavedItems)
{
	if (!IsValid(PlayerController)) return;

	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] OnInventoryControllerEndPlay 진입! Controller=%s, SavedItems=%d"),
		*GetNameSafe(PlayerController), SavedItems.Num());

	// ── PlayerId 찾기 ──
	FString PlayerId;
	if (FString* FoundPlayerId = ControllerToPlayerIdMap.Find(PlayerController))
	{
		PlayerId = *FoundPlayerId;
		ControllerToPlayerIdMap.Remove(PlayerController);
	}
	else
	{
		PlayerId = GetPlayerSaveId(PlayerController);
	}

	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] ❌ PlayerId 비어있음! 저장 중단!"));
		return;
	}
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);

	// ── 장착 정보 병합 ──
	// SavedItems에 장착 정보가 없으면 캐시된 데이터에서 복원
	// ⚠️ EndPlay 시점에서 EquipmentComponent가 이미 파괴되었을 수 있음
	TArray<FInv_SavedItemData> MergedItems = SavedItems;

	int32 EquippedCount = 0;
	for (const FInv_SavedItemData& Item : MergedItems)
	{
		if (Item.bEquipped) EquippedCount++;
	}

	if (EquippedCount == 0)
	{
		if (FInv_PlayerSaveData* Cached = GetCachedData(PlayerId))
		{
			for (const FInv_SavedItemData& CachedItem : Cached->Items)
			{
				if (!CachedItem.bEquipped) continue;

				for (FInv_SavedItemData& Item : MergedItems)
				{
					if (Item.ItemType == CachedItem.ItemType && !Item.bEquipped)
					{
						Item.bEquipped = true;
						Item.WeaponSlotIndex = CachedItem.WeaponSlotIndex;
						break;
					}
				}
			}
		}
	}

	// ── 디스크 저장 ──
	if (MergedItems.Num() > 0)
	{
		SaveCollectedItems(PlayerId, MergedItems);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📂 로드 시스템
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 LoadAndSendInventoryToClient — 인벤토리 로드 후 클라이언트 전송
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. GetPlayerSaveId(PC) → PlayerId
//    2. InventorySaveGame->LoadPlayer(PlayerId, LoadedData)
//    3. 각 아이템: ResolveItemClass() → SpawnActor → InvComp에 추가
//    4. 장착 복원 (데디서버에서만 Broadcast — 리슨서버 이중 실행 방지)
//    5. Client_ReceiveInventoryData() RPC로 클라이언트에 전송
//
// ⚠️ 리슨서버 주의:
//    GetNetMode() == NM_DedicatedServer 체크로 이중 장착 방지
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::LoadAndSendInventoryToClient(APlayerController* PC)
{
	if (!HasAuthority() || !IsValid(PC)) return;

	FString PlayerId = GetPlayerSaveId(PC);
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] ❌ PlayerId 비어있음! 저장 중단!"));
		return;
	}
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);

	if (!IsValid(InventorySaveGame)) return;

	// ── 저장된 데이터 로드 ──
	FInv_PlayerSaveData LoadedData;
	if (!InventorySaveGame->LoadPlayer(PlayerId, LoadedData)) return;
	if (LoadedData.IsEmpty()) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return;

	// ── 아이템 액터 스폰 및 인벤토리 추가 ──
	for (const FInv_SavedItemData& ItemData : LoadedData.Items)
	{
		if (!ItemData.ItemType.IsValid()) continue;

		// ItemType → ActorClass 변환 (게임별 override)
		TSubclassOf<AActor> ActorClass = ResolveItemClass(ItemData.ItemType);
		if (!ActorClass) continue;

		// 아이템 액터 스폰 (맵 아래 안 보이는 곳)
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedActor = GetWorld()->SpawnActor<AActor>(ActorClass, FVector(0.f, 0.f, -10000.f), FRotator::ZeroRotator, SpawnParams);
		if (!IsValid(SpawnedActor)) continue;

		UInv_ItemComponent* ItemComp = SpawnedActor->FindComponentByClass<UInv_ItemComponent>();
		if (!IsValid(ItemComp))
		{
			SpawnedActor->Destroy();
			continue;
		}

		// ── [Phase 6 Attachment] 부착물 복원 ──
		// 아이템을 인벤토리에 추가하기 전에, 저장된 부착물 데이터를
		// ItemComponent의 Manifest에 주입하여 복원
		if (ItemData.Attachments.Num() > 0)
		{
			FInv_ItemManifest WeaponManifest = ItemComp->GetItemManifest();
			FInv_AttachmentHostFragment* HostFrag = WeaponManifest.GetFragmentOfTypeMutable<FInv_AttachmentHostFragment>();
			if (HostFrag)
			{
				for (const FInv_SavedAttachmentData& AttSave : ItemData.Attachments)
				{
					// 부착물 아이템 액터를 임시 스폰하여 Manifest 복사
					TSubclassOf<AActor> AttachClass = ResolveItemClass(AttSave.AttachmentItemType);
					if (!AttachClass) continue;

					AActor* TempActor = GetWorld()->SpawnActor<AActor>(
						AttachClass,
						FVector(0.f, 0.f, -10000.f),
						FRotator::ZeroRotator,
						SpawnParams);
					if (!IsValid(TempActor)) continue;

					UInv_ItemComponent* TempItemComp = TempActor->FindComponentByClass<UInv_ItemComponent>();
					if (!IsValid(TempItemComp))
					{
						TempActor->Destroy();
						continue;
					}

					// FInv_AttachedItemData 구성 → HostFragment에 부착
					FInv_AttachedItemData AttachedData;
					AttachedData.SlotIndex = AttSave.SlotIndex;
					AttachedData.AttachmentItemType = AttSave.AttachmentItemType;
					AttachedData.ItemManifestCopy = TempItemComp->GetItemManifest();

					HostFrag->AttachItem(AttSave.SlotIndex, AttachedData);

					UE_LOG(LogInventory, Log,
						TEXT("[Attachment Save] 부착물 복원: %s → 슬롯 %d"),
						*AttSave.AttachmentItemType.ToString(),
						AttSave.SlotIndex);

					// 임시 액터 정리
					TempActor->Destroy();
				}

				// 수정된 Manifest를 ItemComponent에 반영
				ItemComp->InitItemManifest(WeaponManifest);
			}
		}

		// 인벤토리에 추가 (부착물 데이터가 이미 포함된 상태)
		InvComp->Server_AddNewItem(ItemComp, ItemData.StackCount, 0);

		// 그리드 위치 설정
		const int32 Columns = 8;
		int32 SavedGridIndex = ItemData.GridPosition.Y * Columns + ItemData.GridPosition.X;
		InvComp->SetLastEntryGridPosition(SavedGridIndex, ItemData.GridCategory);
	}

	// ── 장착 상태 복원 ──
	TSet<UInv_InventoryItem*> ServerProcessedItems;
	for (const FInv_SavedItemData& ItemData : LoadedData.Items)
	{
		if (!ItemData.bEquipped || ItemData.WeaponSlotIndex < 0) continue;

		UInv_InventoryItem* FoundItem = InvComp->FindItemByTypeExcluding(ItemData.ItemType, ServerProcessedItems);
		if (FoundItem)
		{
			// 데디서버에서만 서버측 장착 브로드캐스트 실행
			// 리슨서버 호스트는 Client_ReceiveInventoryData → RestoreInventoryFromState에서 처리
			if (GetNetMode() == NM_DedicatedServer)
			{
				InvComp->OnItemEquipped.Broadcast(FoundItem, ItemData.WeaponSlotIndex);
			}
			ServerProcessedItems.Add(FoundItem);
		}
	}

	// ── 클라이언트에 데이터 전송 ──
	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		InvPC->Client_ReceiveInventoryData(LoadedData.Items);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 LoadPlayerInventoryData — 데이터만 로드 (스폰하지 않음)
// ════════════════════════════════════════════════════════════════════════════════
bool AInv_SaveGameMode::LoadPlayerInventoryData(const FString& PlayerId, FInv_PlayerSaveData& OutData) const
{
	if (!IsValid(InventorySaveGame)) return false;
	return InventorySaveGame->LoadPlayer(PlayerId, OutData);
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔧 게임별 Override 포인트
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 ResolveItemClass — 기본 구현 (nullptr)
// ════════════════════════════════════════════════════════════════════════════════
TSubclassOf<AActor> AInv_SaveGameMode::ResolveItemClass(const FGameplayTag& ItemType)
{
	// 기본 구현: nullptr 반환
	// 자식 GameMode에서 override하여 DataTable 매핑 등 구현
	return nullptr;
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 GetPlayerSaveId — 기본 구현 (ControllerMap 조회)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 기본 동작:
//    ControllerToPlayerIdMap에서 PlayerId 검색
//    게임별로 UniqueId 등을 사용하려면 자식 클래스에서 override
//
// ════════════════════════════════════════════════════════════════════════════════
FString AInv_SaveGameMode::GetPlayerSaveId(APlayerController* PC) const
{
	if (!IsValid(PC)) return FString();

	// ControllerToPlayerIdMap에서 검색
	if (const FString* Found = ControllerToPlayerIdMap.Find(PC))
	{
		return *Found;
	}

	return FString();
}

// ════════════════════════════════════════════════════════════════════════════════
// ⏰ 자동저장 시스템
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 StartAutoSave — 자동저장 타이머 시작
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::StartAutoSave()
{
	if (!bAutoSaveEnabled || AutoSaveIntervalSeconds <= 0.0f) return;

	StopAutoSave();

	GetWorldTimerManager().SetTimer(
		AutoSaveTimerHandle,
		this,
		&AInv_SaveGameMode::OnAutoSaveTimer,
		AutoSaveIntervalSeconds,
		true  // Looping
	);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 StopAutoSave — 자동저장 타이머 중지
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::StopAutoSave()
{
	if (AutoSaveTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AutoSaveTimerHandle);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 ForceAutoSave — 수동 자동저장 즉시 실행
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::ForceAutoSave()
{
	OnAutoSaveTimer();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnAutoSaveTimer — 자동저장 타이머 콜백
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::OnAutoSaveTimer()
{
	RequestAllPlayersInventoryState();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RequestAllPlayersInventoryState — 전체 플레이어 인벤토리 상태 요청
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. 모든 PlayerController 순회
//    2. Inv_PlayerController로 캐스트
//    3. 델리게이트 바인딩 (OnInventoryStateReceived)
//    4. Client_RequestInventoryState() RPC 호출
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::RequestAllPlayersInventoryState()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
		if (!IsValid(InvPC)) continue;

		// 델리게이트 바인딩 (중복 방지)
		if (!InvPC->OnInventoryStateReceived.IsBound())
		{
			InvPC->OnInventoryStateReceived.AddDynamic(this, &AInv_SaveGameMode::OnPlayerInventoryStateReceived);
		}

		RequestPlayerInventoryState(PC);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RequestPlayerInventoryState — 단일 플레이어 인벤토리 상태 요청
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::RequestPlayerInventoryState(APlayerController* PC)
{
	if (!IsValid(PC)) return;

	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		InvPC->Client_RequestInventoryState();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnPlayerInventoryStateReceived — 클라이언트로부터 인벤토리 상태 수신
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. GetPlayerSaveId(PlayerController) → PlayerId
//    2. SaveCollectedItems(PlayerId, SavedItems) → 디스크 저장
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::OnPlayerInventoryStateReceived(
	AInv_PlayerController* PlayerController,
	const TArray<FInv_SavedItemData>& SavedItems)
{
	FString PlayerId = GetPlayerSaveId(PlayerController);
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] ❌ PlayerId 비어있음! 저장 중단!"));
		return;
	}
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);

	SaveCollectedItems(PlayerId, SavedItems);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📦 캐시 관리
// ════════════════════════════════════════════════════════════════════════════════

void AInv_SaveGameMode::CachePlayerData(const FString& PlayerId, const FInv_PlayerSaveData& Data)
{
	CachedPlayerData.Add(PlayerId, Data);
}

FInv_PlayerSaveData* AInv_SaveGameMode::GetCachedData(const FString& PlayerId)
{
	return CachedPlayerData.Find(PlayerId);
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RemoveCachedDataDeferred — 캐시 지연 삭제
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 이유:
//    Logout() → EndPlay() 순서로 호출되는데
//    Logout에서 즉시 삭제하면 EndPlay에서 장착 병합 불가
//    → Delay초 후 삭제하여 EndPlay가 완료된 뒤 정리
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::RemoveCachedDataDeferred(const FString& PlayerId, float Delay)
{
	FString PlayerIdCopy = PlayerId;
	FTimerHandle CacheCleanupTimer;
	GetWorldTimerManager().SetTimer(CacheCleanupTimer, [this, PlayerIdCopy]()
	{
		CachedPlayerData.Remove(PlayerIdCopy);
	}, Delay, false);
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔧 유틸리티
// ════════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════════
// 📌 MergeEquipmentState — EquipmentComponent에서 장착 상태를 Items에 병합
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. PC → FindComponentByClass<UInv_EquipmentComponent>()
//    2. EquipComp->GetEquippedActors() 순회
//    3. 각 장착 Actor의 ItemType + SlotIndex를 Items에서 찾아 병합
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::MergeEquipmentState(APlayerController* PC, TArray<FInv_SavedItemData>& Items)
{
	if (!IsValid(PC)) return;

	UInv_EquipmentComponent* EquipComp = PC->FindComponentByClass<UInv_EquipmentComponent>();
	if (!IsValid(EquipComp)) return;

	const TArray<TObjectPtr<AInv_EquipActor>>& EquippedActors = EquipComp->GetEquippedActors();
	for (const TObjectPtr<AInv_EquipActor>& EquipActor : EquippedActors)
	{
		if (!EquipActor.Get()) continue;

		FGameplayTag ItemType = EquipActor->GetEquipmentType();
		int32 SlotIndex = EquipActor->GetWeaponSlotIndex();

		for (FInv_SavedItemData& Item : Items)
		{
			if (Item.ItemType == ItemType && !Item.bEquipped)
			{
				Item.bEquipped = true;
				Item.WeaponSlotIndex = SlotIndex;
				break;
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 BindInventoryEndPlay — Controller에 EndPlay 델리게이트 바인딩
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::BindInventoryEndPlay(AInv_PlayerController* InvPC)
{
	if (IsValid(InvPC))
	{
		InvPC->OnControllerEndPlay.AddDynamic(this, &AInv_SaveGameMode::OnInventoryControllerEndPlay);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RegisterControllerPlayerId — ControllerToPlayerIdMap에 등록
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::RegisterControllerPlayerId(AController* Controller, const FString& PlayerId)
{
	if (IsValid(Controller) && !PlayerId.IsEmpty())
	{
		ControllerToPlayerIdMap.Add(Controller, PlayerId);
	}
}
