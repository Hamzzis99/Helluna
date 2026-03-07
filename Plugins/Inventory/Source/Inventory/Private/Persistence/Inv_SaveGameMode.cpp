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
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "GameplayTagContainer.h"

// [Phase 4] CDO/SCS 컴포넌트 템플릿 접근용
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

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
#if INV_DEBUG_SAVE
		UE_LOG(LogInventory, Warning, TEXT("[SaveGameMode] EndPlay - 리슨서버 종료 감지, 인벤토리 강제 저장 시작"));
#endif
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

	UWorld* World = GetWorld();
	if (!World) return 0;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
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
// 📌 [Phase 5] SaveAllPlayersInventoryDirect — 서버 직접 수집 저장
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. 전체 PlayerController 순회
//    2. InventoryComponent->CollectInventoryDataForSave() 직접 호출
//    3. MergeEquipmentState()로 장착 정보 병합
//    4. InventorySaveGame->SavePlayer()로 메모리 저장 (반복)
//    5. AsyncSaveToDisk() 1회 호출 (Phase 2 비동기 저장)
//
// 📌 장점:
//    - RPC 왕복 없음 (네트워크 부하 제거)
//    - 클라이언트 응답 대기 없음 (타임아웃 불필요)
//    - 미응답 플레이어 데이터 손실 없음
//
// 📌 전제조건:
//    - 아이템 이동 시 Server_UpdateItemGridPosition RPC로 서버 Entry 갱신 필수
//
// ════════════════════════════════════════════════════════════════════════════════
int32 AInv_SaveGameMode::SaveAllPlayersInventoryDirect()
{
	if (!HasAuthority()) return 0;

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [Phase 5] SaveAllPlayersInventoryDirect — 서버 직접 저장   ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	// Phase 2: 비동기 저장 중이면 스킵
	if (bAsyncSaveInProgress)
	{
#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 5] ⚠️ 비동기 저장 진행 중 — 스킵"));
#endif
		return 0;
	}

	int32 SavedCount = 0;

	UWorld* World = GetWorld();
	if (!World) return 0;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		FString PlayerId = GetPlayerSaveId(PC);
		if (PlayerId.IsEmpty()) continue;

		// ── Step 1: InventoryComponent에서 직접 수집 ──
		UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
		if (!IsValid(InvComp)) continue;

		UE_LOG(LogTemp, Error, TEXT("[서버저장진단] 저장 시작 — HasAuthority=%s, 호출함수=%s, PlayerId=%s"),
			HasAuthority() ? TEXT("서버") : TEXT("클라"),
			TEXT(__FUNCTION__), *PlayerId);

		TArray<FInv_SavedItemData> CollectedItems = InvComp->CollectInventoryDataForSave();

		// ── Step 2: EquipmentComponent에서 장착 상태 병합 ──
		MergeEquipmentState(PC, CollectedItems);

		// Fix 8: 장착 아이템의 GridPosition 강제 무효화 — 좌표 중복 방어
		for (FInv_SavedItemData& SavedItem : CollectedItems)
		{
			if (SavedItem.bEquipped && SavedItem.GridPosition != FIntPoint(-1, -1))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Fix8] 장착 아이템 GridPosition 강제 초기화: %s, 기존 Pos=(%d,%d) → (-1,-1)"),
					*SavedItem.ItemType.ToString(), SavedItem.GridPosition.X, SavedItem.GridPosition.Y);
				SavedItem.GridPosition = FIntPoint(-1, -1);
			}
		}

		UE_LOG(LogTemp, Error, TEXT("[서버저장진단] 수집된 총 아이템: %d개 (PlayerId=%s)"), CollectedItems.Num(), *PlayerId);
		for (int32 DiagIdx = 0; DiagIdx < CollectedItems.Num(); DiagIdx++)
		{
			const FInv_SavedItemData& DiagItem = CollectedItems[DiagIdx];
			UE_LOG(LogTemp, Error, TEXT("[서버저장진단]   [%d] %s, bEquipped=%s, WeaponSlot=%d, Pos=(%d,%d), GridCat=%d"),
				DiagIdx,
				*DiagItem.ItemType.ToString(),
				DiagItem.bEquipped ? TEXT("TRUE") : TEXT("false"),
				DiagItem.WeaponSlotIndex,
				DiagItem.GridPosition.X, DiagItem.GridPosition.Y,
				DiagItem.GridCategory);
		}

		if (CollectedItems.Num() == 0) continue;

		// ── Step 3: 메모리에만 저장 (디스크 쓰기는 마지막에 1회) ──
		FInv_PlayerSaveData SaveData;
		SaveData.Items = CollectedItems;
		SaveData.LastSaveTime = FDateTime::Now();

		if (IsValid(InventorySaveGame))
		{
			InventorySaveGame->SavePlayer(PlayerId, SaveData);
		}

		// 캐시 갱신
		CachePlayerData(PlayerId, SaveData);

		SavedCount++;

#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 5] ✅ %s: %d개 아이템 수집 완료"),
			*PlayerId, CollectedItems.Num());
#endif
	}

	// ── Step 4: Phase 2 비동기 디스크 저장 (1회) ──
	if (SavedCount > 0 && IsValid(InventorySaveGame))
	{
		bAsyncSaveInProgress = true;

#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 5] 🚀 비동기 디스크 저장 시작! (%d명)"), SavedCount);
#endif

		TWeakObjectPtr<AInv_SaveGameMode> WeakThis(this);
		UInv_InventorySaveGame::AsyncSaveToDisk(InventorySaveGame, InventorySaveSlotName,
			[WeakThis, SavedCount](bool bSuccess)
			{
				if (WeakThis.IsValid())
				{
					WeakThis->bAsyncSaveInProgress = false;
				}

#if INV_DEBUG_SAVE
				UE_LOG(LogTemp, Warning, TEXT("[Phase 5] 💾 비동기 저장 완료! %d명 (성공=%s)"),
					SavedCount, bSuccess ? TEXT("Y") : TEXT("N"));
#endif
			});
	}

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning, TEXT("[Phase 5] 서버 직접 저장 완료: %d명"), SavedCount);
#endif

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
#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);
#endif

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

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] OnInventoryControllerEndPlay 진입! Controller=%s, SavedItems=%d"),
		*GetNameSafe(PlayerController), SavedItems.Num());
#endif

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
#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);
#endif

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
#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);
#endif

	if (!IsValid(InventorySaveGame)) return;

	// ── 저장된 데이터 로드 ──
	FInv_PlayerSaveData LoadedData;
	if (!InventorySaveGame->LoadPlayer(PlayerId, LoadedData)) return;
	if (LoadedData.IsEmpty()) return;

	UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
	if (!IsValid(InvComp)) return;

	// ── 아이템 복원 (컴포넌트에 위임) ──
	FInv_ItemTemplateResolver Resolver;
	Resolver.BindLambda([this](const FGameplayTag& ItemType) -> UInv_ItemComponent* {
		TSubclassOf<AActor> ActorClass = ResolveItemClass(ItemType);
		if (!ActorClass) return nullptr;
		return FindItemComponentTemplate(ActorClass);
	});
	// Fix 10: 로드된 데이터 정리 — 이전 세션(Fix 7/8 미적용) 세이브 파일 호환
	// 장착 아이템의 stale GridPosition이 남아있으면 클라이언트 Grid 배치가 꼬임
	for (FInv_SavedItemData& LoadItem : LoadedData.Items)
	{
		if (LoadItem.bEquipped && LoadItem.GridPosition != FIntPoint(-1, -1))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Fix10-Load] 장착 아이템 GridPosition 정리: %s, 기존 Pos=(%d,%d) → (-1,-1)"),
				*LoadItem.ItemType.ToString(), LoadItem.GridPosition.X, LoadItem.GridPosition.Y);
			LoadItem.GridPosition = FIntPoint(-1, -1);
		}
	}

	InvComp->RestoreFromSaveData(LoadedData, Resolver);

	// ── 클라이언트에 데이터 전송 (청크 분할) ──
	// UE 네트워크 최대 번치 크기(65536 bytes) 초과 방지
	AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
	if (IsValid(InvPC))
	{
		const TArray<FInv_SavedItemData>& AllItems = LoadedData.Items;
		constexpr int32 ChunkSize = 5;

		// [Fix10-Chunk진단] 전송 전 데이터 확인
		for (int32 DiagIdx = 0; DiagIdx < AllItems.Num(); DiagIdx++)
		{
			const FInv_SavedItemData& DiagItem = AllItems[DiagIdx];
			UE_LOG(LogTemp, Error, TEXT("[Fix10-Chunk진단] 전송 Item[%d] %s: GridPos=(%d,%d), bEquipped=%s, WeaponSlot=%d"),
				DiagIdx, *DiagItem.ItemType.ToString(),
				DiagItem.GridPosition.X, DiagItem.GridPosition.Y,
				DiagItem.bEquipped ? TEXT("TRUE") : TEXT("FALSE"),
				DiagItem.WeaponSlotIndex);
		}

		if (AllItems.Num() <= ChunkSize)
		{
			// 소량이면 기존 방식 (하위호환)
			InvPC->Client_ReceiveInventoryData(AllItems);
		}
		else
		{
			// 청크 분할 전송
			const int32 TotalItems = AllItems.Num();
			for (int32 StartIdx = 0; StartIdx < TotalItems; StartIdx += ChunkSize)
			{
				const int32 EndIdx = FMath::Min(StartIdx + ChunkSize, TotalItems);
				const bool bIsLast = (EndIdx >= TotalItems);

				TArray<FInv_SavedItemData> Chunk;
				Chunk.Reserve(EndIdx - StartIdx);
				for (int32 i = StartIdx; i < EndIdx; ++i)
				{
					Chunk.Add(AllItems[i]);
				}

				InvPC->Client_ReceiveInventoryDataChunk(Chunk, bIsLast);

				UE_LOG(LogTemp, Log, TEXT("[InventoryChunk] 전송: [%d~%d] / %d, bIsLast=%s"),
					StartIdx, EndIdx - 1, TotalItems,
					bIsLast ? TEXT("true") : TEXT("false"));
			}
		}
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
	// ════════════════════════════════════════════════════════════════════════════
	// 📌 [Phase 5] 서버 직접 저장 vs 기존 RPC 방식 분기
	// ════════════════════════════════════════════════════════════════════════════
	if (bUseServerDirectSave)
	{
		// Phase 5: 서버에서 직접 수집 (RPC 없음!)
		SaveAllPlayersInventoryDirect();
	}
	else
	{
		// 기존 방식: 클라이언트 RPC 왕복
		RequestAllPlayersInventoryState();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 RequestAllPlayersInventoryState — 전체 플레이어 인벤토리 상태 요청
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름 (Phase 1 배칭):
//    1. 이미 배칭 중이면 중복 실행 방지
//    2. 모든 PlayerController 순회 → RPC 발송 + RequestCount 카운트
//    3. bAutoSaveBatchInProgress = true, PendingAutoSaveCount = RequestCount
//    4. 타임아웃 타이머 시작 (5초) → 미응답 플레이어 무시하고 FlushAutoSaveBatch()
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::RequestAllPlayersInventoryState()
{
	// ── Phase 1+2: 배칭 중이거나 비동기 저장 중이면 중복 실행 방지 ──
	if (bAutoSaveBatchInProgress || bAsyncSaveInProgress)
	{
#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 1 배칭] ⚠️ %s — 스킵"),
			bAutoSaveBatchInProgress ? TEXT("배칭 진행 중") : TEXT("비동기 저장 진행 중"));
#endif
		return;
	}

	// 응답 대기할 플레이어 수 카운트
	int32 RequestCount = 0;

	UWorld* World = GetWorld();
	if (!World) return;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC)) continue;

		AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
		if (!IsValid(InvPC)) continue;

		// 델리게이트 바인딩 (중복 방지)
		if (!InvPC->OnInventoryStateReceived.IsBound())
		{
			InvPC->OnInventoryStateReceived.AddUniqueDynamic(this, &AInv_SaveGameMode::OnPlayerInventoryStateReceived);
		}

		RequestPlayerInventoryState(PC);
		RequestCount++;
	}

	if (RequestCount > 0)
	{
		bAutoSaveBatchInProgress = true;
		PendingAutoSaveCount = RequestCount;

#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 1 배칭] 🚀 배칭 시작: %d명 응답 대기"), RequestCount);
#endif

		// 타임아웃 타이머 설정 — 미응답 플레이어가 있어도 일정 시간 후 강제 저장
		GetWorldTimerManager().SetTimer(
			AutoSaveBatchTimeoutHandle,
			this,
			&AInv_SaveGameMode::OnAutoSaveBatchTimeout,
			AutoSaveBatchTimeoutSeconds,
			false  // 1회성
		);
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
// 📌 처리 흐름 (Phase 1 배칭):
//    배칭 중: 메모리에만 저장 + 캐시 갱신 → 카운터 감소 → 0이면 FlushAutoSaveBatch()
//    배칭 아님: 기존처럼 SaveCollectedItems() → 즉시 디스크 쓰기
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::OnPlayerInventoryStateReceived(
	AInv_PlayerController* PlayerController,
	const TArray<FInv_SavedItemData>& SavedItems)
{
	// [BugFix] Phase 5 우선 가드: 서버 직접 저장(비동기) 진행 중이면 클라이언트 데이터 무시
	// Phase 5(SaveAllPlayersInventoryDirect)가 FastArray에서 직접 수집한 데이터가 더 정확함
	if (bAsyncSaveInProgress)
	{
#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 5 Guard] ⚠️ 서버 직접 저장 진행 중 — 클라이언트 RPC 데이터 무시"));
#endif
		return;
	}

	FString PlayerId = GetPlayerSaveId(PlayerController);
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] ❌ PlayerId 비어있음! 저장 중단!"));
		// 배칭 카운터는 여전히 감소시켜야 함 (응답은 왔으므로)
		if (bAutoSaveBatchInProgress)
		{
			PendingAutoSaveCount--;
			if (PendingAutoSaveCount <= 0)
			{
				FlushAutoSaveBatch();
			}
		}
		return;
	}

#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Error, TEXT("🔍 [SavePipeline] PlayerId='%s' 찾음!"), *PlayerId);
#endif

	if (bAutoSaveBatchInProgress)
	{
		// ── Phase 1 배칭 중: 메모리에만 저장 (디스크 쓰기 안 함) ──
		FInv_PlayerSaveData SaveData;
		SaveData.Items = SavedItems;
		SaveData.LastSaveTime = FDateTime::Now();

		if (IsValid(InventorySaveGame))
		{
			InventorySaveGame->SavePlayer(PlayerId, SaveData);
		}

		// 캐시 갱신
		CachePlayerData(PlayerId, SaveData);

		// 카운터 감소
		PendingAutoSaveCount--;

#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 1 배칭] 📥 응답 수신: %s (남은 대기: %d)"),
			*PlayerId, PendingAutoSaveCount);
#endif

		if (PendingAutoSaveCount <= 0)
		{
			FlushAutoSaveBatch();
		}
	}
	else
	{
		// 배칭 중이 아닌 경우 (개별 저장) → 기존처럼 즉시 디스크 쓰기
		SaveCollectedItems(PlayerId, SavedItems);
	}
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
// 📌 FlushAutoSaveBatch — 배칭된 데이터를 디스크에 1회 기록
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점:
//    1. 모든 플레이어 응답 수신 완료 (PendingAutoSaveCount <= 0)
//    2. 타임아웃 (미응답 플레이어 무시)
//
// 📌 처리:
//    타임아웃 타이머 클리어 → 배칭 상태 해제 → SaveToDisk() 1회
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::FlushAutoSaveBatch()
{
	// 타임아웃 타이머 클리어
	if (AutoSaveBatchTimeoutHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(AutoSaveBatchTimeoutHandle);
	}

	bAutoSaveBatchInProgress = false;
	PendingAutoSaveCount = 0;

	// ── Phase 2: 비동기 디스크 쓰기 ──
	if (IsValid(InventorySaveGame))
	{
		if (bAsyncSaveInProgress)
		{
#if INV_DEBUG_SAVE
			UE_LOG(LogTemp, Warning, TEXT("[Phase 2 비동기] ⚠️ 이전 비동기 저장 진행 중 — 스킵"));
#endif
			return;
		}

		bAsyncSaveInProgress = true;

#if INV_DEBUG_SAVE
		UE_LOG(LogTemp, Warning, TEXT("[Phase 2 비동기] 🚀 비동기 디스크 저장 시작!"));
#endif

		TWeakObjectPtr<AInv_SaveGameMode> WeakThis(this);
		UInv_InventorySaveGame::AsyncSaveToDisk(InventorySaveGame, InventorySaveSlotName,
			[WeakThis](bool bSuccess)
			{
				if (WeakThis.IsValid())
				{
					WeakThis->bAsyncSaveInProgress = false;
				}

#if INV_DEBUG_SAVE
				UE_LOG(LogTemp, Warning, TEXT("[Phase 2 비동기] 💾 비동기 저장 완료! (성공=%s)"),
					bSuccess ? TEXT("Y") : TEXT("N"));
#endif
			});
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 📌 OnAutoSaveBatchTimeout — 배칭 타임아웃 콜백
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 역할:
//    AutoSaveBatchTimeoutSeconds(5초) 내에 응답이 오지 않은 플레이어가 있으면
//    미응답 무시하고 현재까지 수신된 데이터만으로 디스크 저장
//
// ════════════════════════════════════════════════════════════════════════════════
void AInv_SaveGameMode::OnAutoSaveBatchTimeout()
{
#if INV_DEBUG_SAVE
	UE_LOG(LogTemp, Warning, TEXT("[Phase 1 배칭] ⏰ 타임아웃! 미응답 %d명 무시하고 강제 저장"),
		PendingAutoSaveCount);
#endif

	FlushAutoSaveBatch();
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
	// ⚠️ 기존 타이머가 있으면 취소 (빠른 재접속 시 새 세션 캐시를 삭제하는 버그 방지)
	if (FTimerHandle* ExistingHandle = CacheCleanupTimerHandles.Find(PlayerId))
	{
		GetWorldTimerManager().ClearTimer(*ExistingHandle);
	}

	FString PlayerIdCopy = PlayerId;
	FTimerHandle& CacheCleanupTimer = CacheCleanupTimerHandles.FindOrAdd(PlayerId);
	TWeakObjectPtr<AInv_SaveGameMode> WeakThis(this);
	GetWorldTimerManager().SetTimer(CacheCleanupTimer, [WeakThis, PlayerIdCopy]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CachedPlayerData.Remove(PlayerIdCopy);
			WeakThis->CacheCleanupTimerHandles.Remove(PlayerIdCopy);
		}
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
		InvPC->OnControllerEndPlay.AddUniqueDynamic(this, &AInv_SaveGameMode::OnInventoryControllerEndPlay);
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

// ════════════════════════════════════════════════════════════════════════════════
// 📌 [Phase 4] FindItemComponentTemplate — CDO/SCS에서 ItemComponent 템플릿 추출
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 처리 흐름:
//    1. Blueprint 클래스 계층을 상위로 탐색
//    2. 각 BlueprintGeneratedClass의 SCS 노드에서 UInv_ItemComponent 검색
//    3. 찾으면 해당 ComponentTemplate 반환 (CDO 소유)
//    4. SCS에서 못 찾으면 CDO->FindComponentByClass() 폴백 (C++ 컴포넌트용)
//
// 📌 주의:
//    반환된 포인터는 CDO/SCS가 소유 — 절대 수정 금지!
//    GetItemManifest()로 값 복사를 받아서 사용할 것
//
// ════════════════════════════════════════════════════════════════════════════════
UInv_ItemComponent* AInv_SaveGameMode::FindItemComponentTemplate(TSubclassOf<AActor> ActorClass)
{
	if (!ActorClass) return nullptr;

	// ── SCS 탐색 (Blueprint 에디터에서 추가된 컴포넌트) ──
	for (UClass* CurrentClass = ActorClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(CurrentClass);
		if (!BPGC || !BPGC->SimpleConstructionScript) continue;

		for (USCS_Node* Node : BPGC->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;

			if (UInv_ItemComponent* ItemComp = Cast<UInv_ItemComponent>(Node->ComponentTemplate))
			{
				return ItemComp;
			}
		}
	}

	// ── 폴백: CDO 직접 접근 (C++ 생성자에서 추가된 컴포넌트) ──
	AActor* CDO = ActorClass->GetDefaultObject<AActor>();
	if (CDO)
	{
		return CDO->FindComponentByClass<UInv_ItemComponent>();
	}

	return nullptr;
}
