// Gihyeon's Inventory Project


#include "Widgets/Inventory/GridSlots/Inv_EquippedGridSlot.h"
#include "Inventory.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Items/Inv_InventoryItem.h"
#include "Items/Fragments/Inv_FragmentTags.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Widgets/Inventory/HoverItem/Inv_HoverItem.h"
#include "Widgets/Inventory/SlottedItems/Inv_EquippedSlottedItem.h"
#include "Components/OverlaySlot.h"


void UInv_EquippedGridSlot::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!IsAvailable()) return;
	UInv_HoverItem* HoverItem = UInv_InventoryStatics::GetHoverItem(GetOwningPlayer());
	if (!IsValid(HoverItem)) return;

	if (HoverItem->GetItemType().MatchesTag(EquipmentTypeTag))
	{
		SetOccupiedTexture();
		Image_GrayedOutIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UInv_EquippedGridSlot::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	if (!IsAvailable()) return;
	UInv_HoverItem* HoverItem = UInv_InventoryStatics::GetHoverItem(GetOwningPlayer());
	if (!IsValid(HoverItem)) return;
	
	if (IsValid(EquippedSlottedItem)) return; // 이미 장착된 아이템이 있으면 패스 (그렇게 되면 자연스러운 장착 애니메이션 보임)

	if (HoverItem->GetItemType().MatchesTag(EquipmentTypeTag))
	{
		SetUnoccupiedTexture();
		Image_GrayedOutIcon->SetVisibility(ESlateVisibility::Visible);
	}
}

FReply UInv_EquippedGridSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	EquippedGridSlotClicked.Broadcast(this, EquipmentTypeTag); // 델리게이트 브로드캐스트
	return FReply::Handled();
}

// 아이템 장착 시 호출되는 상황들. 단계별로.
UInv_EquippedSlottedItem* UInv_EquippedGridSlot::OnItemEquipped(UInv_InventoryItem* Item, const FGameplayTag& EquipmentTag, float TileSize)
{
#if INV_DEBUG_EQUIP
	// 🔍 [Phase 8] 디버깅 로그 시작
	UE_LOG(LogTemp, Warning, TEXT("╔══════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║  🔧 [OnItemEquipped] 장착 위젯 생성 시작                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠══════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Item: %s"), Item ? *Item->GetItemManifest().GetItemType().ToString() : TEXT("nullptr"));
	UE_LOG(LogTemp, Warning, TEXT("║ EquipmentTag: %s"), *EquipmentTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("║ EquipmentTypeTag(슬롯): %s"), *EquipmentTypeTag.ToString());
	UE_LOG(LogTemp, Warning, TEXT("║ TileSize: %.1f"), TileSize);
	UE_LOG(LogTemp, Warning, TEXT("║ WeaponSlotIndex: %d"), WeaponSlotIndex);
#endif
	
	// Check the Equipment Type Tag
	// 장비 유형 태그 확인 (MatchesTag로 변경 - 하위 태그도 허용)
	// 예: 슬롯이 GameItems.Equipment.Weapons면 GameItems.Equipment.Weapons.Axe도 장착 가능
	if (!EquipmentTag.MatchesTag(EquipmentTypeTag))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("║ ❌ 태그 불일치! 장착 실패"));
		UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
		return nullptr;
	}
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ 태그 매칭 성공!"));
#endif
	
	// Get Grid Dimensions
	// 그리드 크기 가져오기
	const FInv_GridFragment* GridFragment = GetFragment<FInv_GridFragment>(Item, FragmentTags::GridFragment);
	if (!GridFragment)
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("║ ❌ GridFragment nullptr!"));
		UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
		return nullptr;
	}
	const FIntPoint GridDimensions = GridFragment->GetGridSize();
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ GridDimensions: (%d, %d)"), GridDimensions.X, GridDimensions.Y);
	UE_LOG(LogTemp, Warning, TEXT("║ GridPadding: %.1f"), GridFragment->GetGridPadding());
#endif
	
	// Calculate the Draw Size for the Equipped Slotted Item
	// 장착된 슬롯 아이템의 그리기 크기 계산
	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2;
	const FVector2D DrawSize = GridDimensions * IconTileWidth;
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ IconTileWidth: %.1f"), IconTileWidth);
	UE_LOG(LogTemp, Warning, TEXT("║ DrawSize: (%.1f, %.1f)"), DrawSize.X, DrawSize.Y);
#endif
	
	// Create the Equipped Slotted Item Widget
	// 장착된 슬롯 아이템 위젯 생성
	EquippedSlottedItem = CreateWidget<UInv_EquippedSlottedItem>(GetOwningPlayer(), EquippedSlottedItemClass);
	if (!IsValid(EquippedSlottedItem))
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("║ ❌ EquippedSlottedItem 생성 실패!"));
		UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
		return nullptr;
	}
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ EquippedSlottedItem 생성 성공!"));
#endif
	
	// Set the Slotted Item's Inventory Item
	// 슬롯 아이템의 인벤토리 아이템 설정
	EquippedSlottedItem->SetInventoryItem(Item);
	
	// Set the Slotted Item's Equipment Type Tag
	// 슬롯 아이템의 장비 유형 태그 설정
	EquippedSlottedItem->SetEquipmentTypeTag(EquipmentTag);
	
	// Hide the Stack Count widget on the Slotted Item
	// 슬롯 아이템에서 스택 수량 위젯 숨기기
	EquippedSlottedItem->UpdateStackCount(0);
	
	// Set Inventory Item on this class (the Equipped Grid Slot)
	// 이 클래스(장착된 그리드 슬롯)에 인벤토리 아이템 설정
	SetInventoryItem(Item);
	
	// Set the Image Brush on the Equipped Slotted Item
	// 장착된 슬롯 아이템칸에 이미지 브러시 설정
	const FInv_ImageFragment* ImageFragment = GetFragment<FInv_ImageFragment>(Item, FragmentTags::IconFragment);
	if (!ImageFragment)
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Error, TEXT("║ ❌ ImageFragment nullptr!"));
		UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
		return nullptr;
	}
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ ImageFragment 유효!"));
	UE_LOG(LogTemp, Warning, TEXT("║ Icon: %s"), ImageFragment->GetIcon() ? *ImageFragment->GetIcon()->GetName() : TEXT("nullptr"));
#endif

	FSlateBrush Brush;
	Brush.SetResourceObject(ImageFragment->GetIcon());
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.ImageSize = DrawSize;
	
	EquippedSlottedItem->SetImageBrush(Brush);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ 이미지 브러시 설정 완료!"));
#endif
	
	// 🆕 [Phase 8] DrawSize 캐시 (RefreshLayout용)
	CachedDrawSize = DrawSize;
	
	// Add the Slotted Item as a child to this widget's Overlay
	// 이 위젯의 오버레이에 슬롯 아이템을 자식으로 추가
	Overlay_Root->AddChildToOverlay(EquippedSlottedItem);
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ Overlay에 추가 완료!"));
#endif
	
	// 🆕 [Phase 8] Alignment를 Center로 설정 (Padding 계산 대신 - 더 안정적!)
	UOverlaySlot* OverlaySlot = UWidgetLayoutLibrary::SlotAsOverlaySlot(EquippedSlottedItem);
	OverlaySlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
	OverlaySlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
	
#if INV_DEBUG_EQUIP
	UE_LOG(LogTemp, Warning, TEXT("║ ✅ Alignment Center 설정 완료!"));
	UE_LOG(LogTemp, Warning, TEXT("║ EquippedSlottedItem Visibility: %d"), (int32)EquippedSlottedItem->GetVisibility());
	UE_LOG(LogTemp, Warning, TEXT("╚══════════════════════════════════════════════════════════════╝"));
#endif
	
	// Return the Equipped Slotted Item
	// 장착된 슬롯 아이템 반환
	return EquippedSlottedItem;
}

// ============================================
// 🆕 [Phase 6] 장착된 아이템 가져오기
// ============================================
UInv_InventoryItem* UInv_EquippedGridSlot::GetEquippedInventoryItem() const
{
	if (IsValid(EquippedSlottedItem))
	{
		return EquippedSlottedItem->GetInventoryItem();
	}
	return nullptr;
}

// ============================================
// 🆕 [Phase 8] 레이아웃 갱신 (인벤토리 열릴 때 호출)
// ============================================
void UInv_EquippedGridSlot::RefreshLayout()
{
	if (!IsValid(EquippedSlottedItem) || CachedDrawSize.IsNearlyZero())
	{
		return; // 장착된 아이템이 없거나 DrawSize가 없으면 스킵
	}
	
	// 레이아웃 강제 갱신
	Overlay_Root->ForceLayoutPrepass();
	
	// Geometry 다시 가져오기
	FVector2D OverlaySize = Overlay_Root->GetCachedGeometry().Size;
	if (OverlaySize.IsNearlyZero())
	{
		OverlaySize = Overlay_Root->GetDesiredSize();
	}
	
	if (OverlaySize.IsNearlyZero())
	{
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("[RefreshLayout] OverlaySize가 여전히 0, 스킵"));
#endif
		return;
	}
	
	// Padding 재계산
	const float LeftPadding = OverlaySize.X / 2.f - CachedDrawSize.X / 2.f;
	const float TopPadding = OverlaySize.Y / 2.f - CachedDrawSize.Y / 2.f;
	
	UOverlaySlot* OverlaySlot = UWidgetLayoutLibrary::SlotAsOverlaySlot(EquippedSlottedItem);
	if (OverlaySlot)
	{
		OverlaySlot->SetPadding(FMargin(LeftPadding, TopPadding));
#if INV_DEBUG_EQUIP
		UE_LOG(LogTemp, Warning, TEXT("[RefreshLayout] 슬롯 %d: Padding 재설정 (%.1f, %.1f)"), 
			WeaponSlotIndex, LeftPadding, TopPadding);
#endif
	}
}
