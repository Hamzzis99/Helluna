#include "InventoryManagement/FastArray/Inv_FastArray.h"

#include "Inventory.h"  // INV_DEBUG_INVENTORY 매크로 정의
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Types/Inv_GridTypes.h"

TArray<UInv_InventoryItem*> FInv_InventoryFastArray::GetAllItems() const
{
	TArray<UInv_InventoryItem*> Results;
	Results.Reserve(Entries.Num());
	for (const auto& Entry : Entries)
	{
		if (!IsValid(Entry.Item)) continue;
		Results.Add(Entry.Item);
	}
	return Results;
}

void FInv_InventoryFastArray::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PreReplicatedRemove 호출됨! (FastArray) ==="));
	UE_LOG(LogTemp, Warning, TEXT("제거된 항목 개수: %d / 최종 크기: %d"), RemovedIndices.Num(), FinalSize);
#endif

	for (int32 Index : RemovedIndices)
	{
		if (!Entries.IsValidIndex(Index))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("❌ 잘못된 Index: %d"), Index);
#endif
			continue;
		}

		UInv_InventoryItem* RemovedItem = Entries[Index].Item;
		if (IsValid(RemovedItem))
		{
			// ⭐ GameplayTag 복사 (안전!)
			FGameplayTag ItemType = RemovedItem->GetItemManifest().GetItemType();

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("🗑️ 제거될 아이템: %s (Index: %d)"),
				*ItemType.ToString(), Index);
#endif

			// ⭐ OnItemRemoved 델리게이트 브로드캐스트 (모든 아이템)
			IC->OnItemRemoved.Broadcast(RemovedItem, Index);

			// ⭐⭐⭐ Stackable 아이템만 OnMaterialStacksChanged 호출!
			// Non-stackable(장비)은 UpdateMaterialStacksByTag 실행 안 함 (GameplayTag 기반 삭제 방지)
			if (RemovedItem->IsStackable())
			{
				IC->OnMaterialStacksChanged.Broadcast(ItemType);
#if INV_DEBUG_INVENTORY
				UE_LOG(LogTemp, Warning, TEXT("✅ OnItemRemoved & OnMaterialStacksChanged 브로드캐스트 완료 (Stackable)"));
#endif
			}
#if INV_DEBUG_INVENTORY
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ OnItemRemoved 브로드캐스트 완료 (Non-stackable, OnMaterialStacksChanged 스킵)"));
			}
#endif
		}
#if INV_DEBUG_INVENTORY
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Index %d의 Item이 nullptr"), Index);
		}
#endif
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PreReplicatedRemove 완료! ==="));
#endif
}

void FInv_InventoryFastArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedAdd 호출됨! (FastArray) ==="));
	UE_LOG(LogTemp, Warning, TEXT("추가된 항목 개수: %d / 전체 Entry 수: %d"), AddedIndices.Num(), Entries.Num());
#endif

	// 인벤토리 컴포넌트에 있는 아이템을 서버에서 클라이언트로 받는 거?
	for (int32 Index : AddedIndices)
	{
#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PostReplicatedAdd] 처리 중: Index=%d"), Index);
#endif

		if (!Entries.IsValidIndex(Index))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("[PostReplicatedAdd] ❌ Index %d는 유효하지 않음! Entries.Num()=%d"), Index, Entries.Num());
#endif
			continue;
		}

		if (!IsValid(Entries[Index].Item))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("[PostReplicatedAdd] ❌ Index %d의 Item이 nullptr입니다!"), Index);
#endif
			continue;
		}

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("[PostReplicatedAdd] Index: %d, ItemType: %s"),
			Index, *Entries[Index].Item->GetItemManifest().GetItemType().ToString());
#endif
		// ⭐ Entry Index도 함께 전달하여 클라이언트에서 저장 가능!
		IC->OnItemAdded.Broadcast(Entries[Index].Item, Index);
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedAdd 완료! ==="));
#endif
}

void FInv_InventoryFastArray::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return;

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedChange 호출됨 (FastArray) ==="));
	UE_LOG(LogTemp, Warning, TEXT("📋 변경된 항목 개수: %d / 전체 Entry 수: %d"), ChangedIndices.Num(), Entries.Num());
#endif

	for (int32 Index : ChangedIndices)
	{
		if (!Entries.IsValidIndex(Index))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Error, TEXT("❌ 잘못된 Index: %d (전체: %d)"), Index, Entries.Num());
#endif
			continue;
		}

		UInv_InventoryItem* ChangedItem = Entries[Index].Item;
		if (!IsValid(ChangedItem))
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Entry[%d]: Item이 nullptr (제거됨)"), Index);
#endif
			continue;
		}

		int32 NewStackCount = ChangedItem->GetTotalStackCount();
		EInv_ItemCategory Category = ChangedItem->GetItemManifest().GetItemCategory();

#if INV_DEBUG_INVENTORY
		UE_LOG(LogTemp, Warning, TEXT("📦 FastArray 변경 감지 [%d]: Item포인터=%p, ItemType=%s, Category=%d, NewStackCount=%d"),
			Index, ChangedItem, *ChangedItem->GetItemManifest().GetItemType().ToString(),
			(int32)Category, NewStackCount);
#endif

		// ⭐⭐⭐ Craftables(재료)만 AddStacks() 호출! (차감 로직)
		if (Category == EInv_ItemCategory::Craftable)
		{
#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("  → Craftable 재료: OnStackChange 호출 (차감/분배 로직)"));
#endif

			FInv_SlotAvailabilityResult Result;
			Result.Item = ChangedItem;
			Result.bStackable = true;
			Result.TotalRoomToFill = NewStackCount;
			Result.EntryIndex = Index;

			IC->OnStackChange.Broadcast(Result);  // AddStacks() 호출

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("✅ OnStackChange 브로드캐스트 완료! (Entry[%d], NewCount: %d)"),
				Index, NewStackCount);
#endif
		}
		else
		{
#if INV_DEBUG_INVENTORY
			// ⭐⭐⭐ Equippables, Consumables는 직접 UI 업데이트!
			UE_LOG(LogTemp, Warning, TEXT("  → Non-Craftable (Category=%d): OnItemStackChanged 호출 (직접 UI 업데이트)"),
				(int32)Category);
#endif

			// ⭐ OnStackChange 대신 OnItemStackChanged 브로드캐스트 (스택 증가 전용!)
			// EntryIndex와 NewStackCount를 포함한 Result 생성
			FInv_SlotAvailabilityResult Result;
			Result.Item = ChangedItem;
			Result.bStackable = true;
			Result.TotalRoomToFill = NewStackCount;
			Result.EntryIndex = Index;

			// ⭐ 새로운 델리게이트 대신 기존 OnItemAdded 재사용 (UI가 아이템 찾아서 업데이트)
			IC->OnItemAdded.Broadcast(ChangedItem, Index);

#if INV_DEBUG_INVENTORY
			UE_LOG(LogTemp, Warning, TEXT("✅ OnItemAdded 브로드캐스트 완료! (Entry[%d], NewCount: %d)"),
				Index, NewStackCount);
#endif
		}
	}

#if INV_DEBUG_INVENTORY
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedChange 완료 (총 %d개 Entry 처리됨) ==="), ChangedIndices.Num());
#endif
}

// FastArray에 항목을 추가해주는 기능들.
UInv_InventoryItem* FInv_InventoryFastArray::AddEntry(UInv_ItemComponent* ItemComponent) 
{
	//TODO : Implement once ItemComponent is more complete 
	check(OwnerComponent); // 소유자 컴포넌트 확인 (소유재고 확인)
	AActor* OwningActor = OwnerComponent->GetOwner(); // 소유자 확보
	check(OwningActor->HasAuthority()); // 권한이 있는지 확인
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent); // 소유자 컴포넌트를 인벤토리 컴포넌트로 캐스팅
	if (!IsValid(IC)) return nullptr;

	FInv_InventoryEntry& NewEntry = Entries.AddDefaulted_GetRef(); // 새 항목 추가
	NewEntry.Item = ItemComponent->GetItemManifest().Manifest(OwningActor); // 항목 매니페스트에서 항목 가져오기 (새로 생성된 아이템의 소유자 지정)

	IC->AddRepSubObj(NewEntry.Item); // 복제 하위 객체로 항목 추가
	MarkItemDirty(NewEntry); // 복제되어야 함을 알려주는 것.

	return NewEntry.Item; // 새로 추가된 항목 반환
}

UInv_InventoryItem* FInv_InventoryFastArray::AddEntry(UInv_InventoryItem* Item)
{
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority());
	
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return nullptr;

	FInv_InventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Item = Item;

	IC->AddRepSubObj(NewEntry.Item); // 리플리케이션 등록 (크래프팅 아이템도 클라이언트로 전송!)
	MarkItemDirty(NewEntry);
	
	return Item;
}

void FInv_InventoryFastArray::RemoveEntry(UInv_InventoryItem* Item)
{
	for (auto EntryIt = Entries.CreateIterator(); EntryIt; ++EntryIt) // 반복자가 가리키는 항목?
	{
		FInv_InventoryEntry& Entry = *EntryIt;
		if (Entry.Item == Item)
		{
			EntryIt.RemoveCurrent(); // 현재 항목 제거
			MarkArrayDirty(); 
		}
	}
}

UInv_InventoryItem* FInv_InventoryFastArray::FindFirstItemByType(const FGameplayTag& ItemType)
{
	auto* FoundItem = Entries.FindByPredicate([ItemType = ItemType](const FInv_InventoryEntry& Entry)// 프레디케이트는 부를 수 있는지 확인하는 거라고?
	{
		// 람다함수 코딩
		//게임 플레이 태그만 확인하는 부분
		return IsValid(Entry.Item) && Entry.Item->GetItemManifest().GetItemType().MatchesTagExact(ItemType);
	});
	return FoundItem ? FoundItem->Item : nullptr;
}
