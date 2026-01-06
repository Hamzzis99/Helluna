#include "InventoryManagement/FastArray/Inv_FastArray.h"

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

	for (int32 Index : RemovedIndices)
	{
		IC->OnItemRemoved.Broadcast(Entries[Index].Item); // 아이템 제거 델리게이트 브로드캐스트 항목에 접근?
	}
}

void FInv_InventoryFastArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return;

	// 인벤토리 컴포넌트에 있는 아이템을 서버에서 클라이언트로 받는 거?
	for (int32 Index : AddedIndices) // 
	{
		IC->OnItemAdded.Broadcast(Entries[Index].Item); // 브로드캐스트가 뭐였지? 까먹었어 ToonTanks 다시 봐야해?
	}
}

void FInv_InventoryFastArray::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	UInv_InventoryComponent* IC = Cast<UInv_InventoryComponent>(OwnerComponent);
	if (!IsValid(IC)) return;

	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedChange 호출됨 (FastArray) ==="));
	UE_LOG(LogTemp, Warning, TEXT("📋 변경된 항목 개수: %d / 전체 Entry 수: %d"), ChangedIndices.Num(), Entries.Num());

	// ⭐ 변경된 모든 Entry 처리 (멀티스택 UI 업데이트 지원!)
	for (int32 Index : ChangedIndices)
	{
		if (!Entries.IsValidIndex(Index))
		{
			UE_LOG(LogTemp, Error, TEXT("❌ 잘못된 Index: %d (전체: %d)"), Index, Entries.Num());
			continue;
		}

		UInv_InventoryItem* ChangedItem = Entries[Index].Item;
		if (!IsValid(ChangedItem))
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Entry[%d]: Item이 nullptr (제거됨)"), Index);
			continue;
		}

		int32 NewStackCount = ChangedItem->GetTotalStackCount();
		
		UE_LOG(LogTemp, Warning, TEXT("📦 FastArray 변경 감지 [%d]: Item포인터=%p, ItemType=%s, NewStackCount=%d"), 
			Index, ChangedItem, *ChangedItem->GetItemManifest().GetItemType().ToString(), NewStackCount);
		
		// OnStackChange 델리게이트 브로드캐스트 → InventoryGrid::AddStacks 호출!
		FInv_SlotAvailabilityResult Result;
		Result.Item = ChangedItem;
		Result.bStackable = true;
		Result.TotalRoomToFill = NewStackCount;
		
		IC->OnStackChange.Broadcast(Result);
		
		UE_LOG(LogTemp, Warning, TEXT("✅ OnStackChange 브로드캐스트 완료! UI 업데이트 요청됨 (Entry[%d], NewCount: %d)"), 
			Index, NewStackCount);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== PostReplicatedChange 완료 (총 %d개 Entry 처리됨) ==="), ChangedIndices.Num());
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
	//빠른 배열 직렬화 방침이 있다고? 강사님 입장에선? 서버에 이 복제된 배열을 덮어씌운다.
	check(OwnerComponent);
	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority()); // 권한이 있는지 확인 

	FInv_InventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Item = Item;

	MarkItemDirty(NewEntry); // 복제되어야 함을 알려주는 것.
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
