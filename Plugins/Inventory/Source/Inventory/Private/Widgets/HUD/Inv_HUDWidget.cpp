// Gihyeon's Inventory Project


#include "Widgets/HUD/Inv_HUDWidget.h"
#include "Inventory.h"

#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Widgets/HUD/Inv_InfoMessage.h"

void UInv_HUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("=== [HUD] HUDWidget NativeOnInitialized 호출됨 ==="));
#endif

	// 델리게이트 바인딩 (인벤토리 꽉참 알림)
	UInv_InventoryComponent* InventoryComponent = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (IsValid(InventoryComponent))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[HUD] InventoryComponent 찾음! NoRoomInInventory 델리게이트 바인딩 중..."));
#endif
		InventoryComponent->NoRoomInInventory.AddDynamic(this, &UInv_HUDWidget::OnNoRoom);
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[HUD] ✅ 델리게이트 바인딩 완료!"));
#endif
	}
	else
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("[HUD] ❌ InventoryComponent를 찾을 수 없습니다! 델리게이트 바인딩 실패!"));
#endif
	}
}

void UInv_HUDWidget::OnNoRoom()
{
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("=== [HUD] OnNoRoom 호출됨! ==="));
#endif

	if (!IsValid(InfoMessage))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("[HUD] InfoMessage가 nullptr입니다!"));
#endif
		return;
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[HUD] 'No Room In Inventory' 메시지 표시!"));
#endif
	InfoMessage->SetMessage(FText::FromString("No Room In Inventory."));
}



