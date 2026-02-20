// Gihyeon's Inventory Project


#include "Items/Inv_InventoryItem.h"

#include "Items/Fragments/Inv_ItemFragment.h"
#include "Items/Fragments/Inv_AttachmentFragments.h"
#include "Net/UnrealNetwork.h"

void UInv_InventoryItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	UObject::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, ItemManifest);
	DOREPLIFETIME(ThisClass, TotalStackCount);
	DOREPLIFETIME(ThisClass, GridPosition);  // ⭐ Grid 위치 동기화!
}

void UInv_InventoryItem::SetItemManifest(const FInv_ItemManifest& Manifest)
{
	ItemManifest = FInstancedStruct::Make<FInv_ItemManifest>(Manifest);
}

bool UInv_InventoryItem::IsStackable() const
{
	const FInv_StackableFragment* Stackable = GetItemManifest().GetFragmentOfType<FInv_StackableFragment>();
	return Stackable != nullptr;
}

bool UInv_InventoryItem::IsConsumable() const // 아이템이 소비 가능한지 여부 확인
{
	return GetItemManifest().GetItemCategory() == EInv_ItemCategory::Consumable;
}

void UInv_InventoryItem::SetTotalStackCount(int32 Count)
{
	// MaxStackSize 검증!
	if (const FInv_StackableFragment* StackableFragment = GetItemManifest().GetFragmentOfType<FInv_StackableFragment>())
	{
		const int32 MaxStack = StackableFragment->GetMaxStackSize();
		if (Count > MaxStack)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SetTotalStackCount] ⚠️ MaxStackSize 초과! 요청: %d, Max: %d → %d로 클램프"), 
				Count, MaxStack, MaxStack);
		}
		TotalStackCount = FMath::Clamp(Count, 0, MaxStack);
	}
	else
	{
		// Stackable Fragment가 없으면 스택 불가 아이템 (1개만 가능)
		TotalStackCount = FMath::Clamp(Count, 0, 1);
	}
}

// ════════════════════════════════════════════════════════════════
// 📌 [부착물 시스템 Phase 2] 부착물 관련 헬퍼 함수
// ════════════════════════════════════════════════════════════════

bool UInv_InventoryItem::HasAttachmentSlots() const
{
	return GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>() != nullptr;
}

int32 UInv_InventoryItem::GetAttachmentSlotCount() const
{
	const FInv_AttachmentHostFragment* HostFragment = GetItemManifest().GetFragmentOfType<FInv_AttachmentHostFragment>();
	return HostFragment ? HostFragment->GetSlotCount() : 0;
}

bool UInv_InventoryItem::IsAttachableItem() const
{
	return GetItemManifest().GetFragmentOfType<FInv_AttachableFragment>() != nullptr;
}
