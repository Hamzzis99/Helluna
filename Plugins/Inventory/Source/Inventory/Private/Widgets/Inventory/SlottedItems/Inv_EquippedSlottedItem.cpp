// Gihyeon's Inventory Project


#include "Widgets/Inventory/SlottedItems/Inv_EquippedSlottedItem.h"

FReply UInv_EquippedSlottedItem::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	OnEquippedSlottedItemClicked.Broadcast(this); // 장착된 슬롯 아이템 클릭 델리게이트 브로드캐스트
	return FReply::Handled(); // 이벤트가 처리되었음을 나타내는 FReply 반환
}
