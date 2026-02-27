// Gihyeon's Inventory Project

#include "Widgets/Inventory/SlottedItems/Inv_SlottedItem.h"
#include "Items/Inv_InventoryItem.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"

FReply UInv_SlottedItem::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	OnSlottedItemClicked.Broadcast(GridIndex, MouseEvent); // 슬롯 아이템 클릭 델리게이트 브로드캐스트
	return FReply::Handled(); // 이벤트가 처리되었음을 나타내는 FReply 반환
}

// 아이템 Description 설명 부분들 아이템을 마우스 댈 때
void UInv_SlottedItem::NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// U17: GC된 아이템 weak ptr null 전달 방지
	if (InventoryItem.IsValid())
	{
		UInv_InventoryStatics::ItemHovered(GetOwningPlayer(), InventoryItem.Get());
	}
}

// 아이템 Description 설명 부분들 아이템을 마우스 땔 때
void UInv_SlottedItem::NativeOnMouseLeave(const FPointerEvent& MouseEvent)
{
	UInv_InventoryStatics::ItemUnhovered(GetOwningPlayer());
}

void UInv_SlottedItem::SetInventoryItem(UInv_InventoryItem* Item)
{
	InventoryItem = Item;
}

void UInv_SlottedItem::SetImageBrush(const FSlateBrush& Brush) const
{
	Image_Icon->SetBrush(Brush);
}

void UInv_SlottedItem::UpdateStackCount(int32 StackCount) // 아이템 텍스트 스택 업데이트 부분
{
	if (StackCount > 0)
	{
		Text_StackCount->SetVisibility(ESlateVisibility::Visible);
		Text_StackCount->SetText(FText::AsNumber(StackCount));
	}
	else
	{
		Text_StackCount->SetVisibility(ESlateVisibility::Collapsed);
	}
}
