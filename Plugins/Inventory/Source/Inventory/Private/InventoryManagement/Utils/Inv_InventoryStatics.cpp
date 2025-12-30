// Gihyeon's Inventory Project


#include "InventoryManagement/Utils/Inv_InventoryStatics.h"

#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Items/Components/Inv_ItemComponent.h"
#include "Types/Inv_GridTypes.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"

UInv_InventoryComponent* UInv_InventoryStatics::GetInventoryComponent(const APlayerController* PlayerController)
{
	if (!IsValid(PlayerController)) return nullptr;
	UInv_InventoryComponent* InventoryComponent = PlayerController->FindComponentByClass<UInv_InventoryComponent>();
	return InventoryComponent;
}

EInv_ItemCategory UInv_InventoryStatics::GetItemCategoryFromItemComp(UInv_ItemComponent* ItemComp)
{
	if (!IsValid(ItemComp)) return EInv_ItemCategory::None;
	return ItemComp->GetItemManifest().GetItemCategory();
}

// 아이템이 호버되었을 때 호출되는 함수 (설명 칸 보일 때 쓰는 부분들임)
void UInv_InventoryStatics::ItemHovered(APlayerController* PC, UInv_InventoryItem* Item)
{
	UInv_InventoryComponent* IC = GetInventoryComponent(PC);
	if (!IsValid(IC)) return;
	
	UInv_InventoryBase* InventoryBase = IC->GetInventoryMenu();
	if (!IsValid(InventoryBase)) return;
	
	if (InventoryBase->HasHoverItem()) return;
	
	InventoryBase->OnItemHovered(Item);
}


void UInv_InventoryStatics::ItemUnhovered(APlayerController* PC)
{
	UInv_InventoryComponent* IC = GetInventoryComponent(PC);
	if (!IsValid(IC)) return;

	UInv_InventoryBase* InventoryBase = IC->GetInventoryMenu();
	if (!IsValid(InventoryBase)) return;

	InventoryBase->OnItemUnHovered();
}

// 인벤토리 그리드의 통계 부분을 확인해주는 것 (?) <- 추후 분석 필요
UInv_HoverItem* UInv_InventoryStatics::GetHoverItem(APlayerController* PC)
{
	UInv_InventoryComponent* IC = GetInventoryComponent(PC);
	if (!IsValid(IC)) return nullptr;
	
	UInv_InventoryBase* InventoryBase = IC->GetInventoryMenu();
	if (!IsValid(InventoryBase)) return nullptr;
	
	return InventoryBase->GetHoverItem();
}

// 인벤토리 위젯 가져오기
UInv_InventoryBase* UInv_InventoryStatics::GetInventoryWidget(APlayerController* PC)
{
	UInv_InventoryComponent* IC = GetInventoryComponent(PC);
	if (!IsValid(IC)) return nullptr;
	
	return IC->GetInventoryMenu();
}
