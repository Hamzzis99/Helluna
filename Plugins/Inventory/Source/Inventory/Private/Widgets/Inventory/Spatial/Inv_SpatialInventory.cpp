// Gihyeon's Inventory Project


#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"

#include "Inventory.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Items/Inv_InventoryItem.h"
#include "Widgets/ItemDescription/Inv_ItemDescription.h"
#include "Blueprint/WidgetTree.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Widgets/Inventory/GridSlots/Inv_EquippedGridSlot.h"
#include "Widgets/Inventory/HoverItem/Inv_HoverItem.h"
#include "Widgets/Inventory/SlottedItems/Inv_EquippedSlottedItem.h"

//버튼 생성할 때 필요한 것들
void UInv_SpatialInventory::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 🔍 [디버깅] 현재 맵 이름 출력
	FString CurrentMapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("Unknown");
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║ [SpatialInventory] NativeOnInitialized                     ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 현재 맵: %s"), *CurrentMapName);
	UE_LOG(LogTemp, Warning, TEXT("║ 📍 위젯 클래스: %s"), *GetClass()->GetName());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	//인벤토리 장비 칸들
	Button_Equippables->OnClicked.AddDynamic(this, &ThisClass::ShowEquippables);
	Button_Consumables->OnClicked.AddDynamic(this, &ThisClass::ShowConsumables);
	Button_Craftables->OnClicked.AddDynamic(this, &ThisClass::ShowCraftables);
	
	// 툴팁 캔버스 설정
	Grid_Equippables->SetOwningCanvas(CanvasPanel);
	Grid_Consumables->SetOwningCanvas(CanvasPanel);
	Grid_Craftables->SetOwningCanvas(CanvasPanel);

	ShowEquippables(); // 기본값으로 장비창을 보여주자.

	// 🔍 [디버깅] WidgetTree 순회 전 상태
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("▶ [NativeOnInitialized] WidgetTree에서 EquippedGridSlot 수집 시작..."));
#endif

	CollectEquippedGridSlots();

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif
}

// ============================================
// 🆕 [Phase 7] EquippedGridSlots 수집 함수 분리
// ============================================
void UInv_SpatialInventory::CollectEquippedGridSlots()
{
	// 이미 수집되었으면 스킵
	if (EquippedGridSlots.Num() > 0)
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("   ⏭️ EquippedGridSlots 이미 수집됨: %d개"), EquippedGridSlots.Num());
#endif
		return;
	}

	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		UInv_EquippedGridSlot* EquippedGridSlot = Cast<UInv_EquippedGridSlot>(Widget);
		if (IsValid(EquippedGridSlot))
		{
			EquippedGridSlots.Add(EquippedGridSlot);

			// 델리게이트 중복 바인딩 방지
			if (!EquippedGridSlot->EquippedGridSlotClicked.IsAlreadyBound(this, &ThisClass::EquippedGridSlotClicked))
			{
				EquippedGridSlot->EquippedGridSlotClicked.AddDynamic(this, &ThisClass::EquippedGridSlotClicked);
			}

#if INV_DEBUG_WIDGET
			UE_LOG(LogTemp, Warning, TEXT("   ✅ EquippedGridSlot 발견: %s (WeaponSlotIndex=%d)"),
				*EquippedGridSlot->GetName(), EquippedGridSlot->GetWeaponSlotIndex());
#endif
		}
	});

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("▶ EquippedGridSlots 수집 완료: 총 %d개"), EquippedGridSlots.Num());
	if (EquippedGridSlots.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("   ❌ [경고] EquippedGridSlots가 비어있음!"));
	}
#endif
}

// ============================================
// 🆕 [Phase 8] 인벤토리 열릴 때 장착 슬롯 레이아웃 갱신
// ============================================
void UInv_SpatialInventory::RefreshEquippedSlotLayouts()
{
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[RefreshEquippedSlotLayouts] 장착 슬롯 레이아웃 갱신 시작 (%d개 슬롯)"), EquippedGridSlots.Num());
#endif

	for (UInv_EquippedGridSlot* EquippedGridSlot : EquippedGridSlots)
	{
		if (IsValid(EquippedGridSlot))
		{
			EquippedGridSlot->RefreshLayout();
		}
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("[RefreshEquippedSlotLayouts] 갱신 완료!"));
#endif
}

// 장착된 그리드 슬롯이 클릭되었을 때 호출되는 함수
void UInv_SpatialInventory::EquippedGridSlotClicked(UInv_EquippedGridSlot* EquippedGridSlot, const FGameplayTag& EquipmentTypeTag) // 콜백함수 
{
	// Check to see if we can equip the Hover Item
	// 호버 아이템을 장착할 수 있는지 확인
	if (!CanEquipHoverItem(EquippedGridSlot, EquipmentTypeTag)) return; // 장착할 수 없으면 반환 (아이템이 없는 경우에 끌어당길 시.)
	
	UInv_HoverItem* HoverItem = GetHoverItem();
	
	// Create an Equipped Slotted Item and add it to the Equipped Grid Slot (call EquippedGridSlot->OnItemEquipped())
	// 장착된 슬롯 아이템을 만들고 장착된 그리드 슬롯에 (EquippedGridSlot->OnItemEquipped()) 추가
	const float TileSize = UInv_InventoryStatics::GetInventoryWidget(GetOwningPlayer())->GetTileSize();
	
	// 장착시킨 그리드 슬롯에 실제 아이템 장착
	UInv_EquippedSlottedItem* EquippedSlottedItem = EquippedGridSlot->OnItemEquipped(
		HoverItem->GetInventoryItem(),
		EquipmentTypeTag,
		TileSize
	);
	EquippedSlottedItem->OnEquippedSlottedItemClicked.AddDynamic(this, &ThisClass::EquippedSlottedItemClicked);
	
	// Inform the server that we've equipped an item (potentially unequipping an item as well)
	// 아이템을 장착했음을 서버에 알리기(잠재적으로 아이템을 해제하기도 함)
	UInv_InventoryComponent* InventoryComponent = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	check(IsValid(InventoryComponent)); 
	
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스 전달
	int32 WeaponSlotIndex = EquippedGridSlot->GetWeaponSlotIndex();
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("⭐ [SpatialInventory] 장착 슬롯 클릭 - WeaponSlotIndex: %d"), WeaponSlotIndex);
#endif
	
	//장착된 곳에 서버RPC를 생성하는 부분
	InventoryComponent->Server_EquipSlotClicked(HoverItem->GetInventoryItem(), nullptr, WeaponSlotIndex);
	
	//데디케이티드 서버 제약 조건 설정 (민우님에게도 알려줄 것.)
	if (GetOwningPlayer()->GetNetMode() != NM_DedicatedServer)
	{
		InventoryComponent->OnItemEquipped.Broadcast(HoverItem->GetInventoryItem(), WeaponSlotIndex); // 아이템 장착 델리게이트 방송
	}
	
	// Clear the Hover item
	// 호버 아이템 지우기
	Grid_Equippables->ClearHoverItem();
}

// 장착된 슬롯 아이템 클릭 시 호출되는 함수
void UInv_SpatialInventory::EquippedSlottedItemClicked(UInv_EquippedSlottedItem* EquippedSlottedItem)
{
	// Remove the Item Description
	// 아이템 설명 제거
	UInv_InventoryStatics::ItemUnhovered(GetOwningPlayer());
	if (IsValid(GetHoverItem()) && GetHoverItem()->IsStackable()) return; // 호버 아이템이 유효하고 스택 가능하면 반환 (수정됨)
	
	//Get Item to Equip
	// 장착할 아이템 가져오기
	UInv_InventoryItem* ItemToEquip = IsValid(GetHoverItem()) ? GetHoverItem()->GetInventoryItem() : nullptr; // 장착할 아이템
	
	//Get item to Unequip
	// 해제할 아이템 가져오기
	UInv_InventoryItem* ItemToUnequip = EquippedSlottedItem->GetInventoryItem(); // 해제할 아이템
	
	// Get the Equipped Grid Slot holding this item
	// 이 아이템을 보유한 장착된 그리드 슬롯 가져오기
	UInv_EquippedGridSlot* EquippedGridSlot = FindSlotWithEquippedItem(ItemToUnequip);
	
	// ⭐ [WeaponBridge] 장착 해제 시 WeaponSlotIndex 가져오기
	int32 WeaponSlotIndex = IsValid(EquippedGridSlot) ? EquippedGridSlot->GetWeaponSlotIndex() : -1;
#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Warning, TEXT("⭐ [SpatialInventory] 장착 슬롯 아이템 클릭 (해제) - WeaponSlotIndex: %d"), WeaponSlotIndex);
#endif
	
	// Clear the equipped slot of this item (set it's inventory item to nullptr)
	// 이 아이템의 슬롯을 지우기
	ClearSlotOfItem(EquippedGridSlot);
	
	// Assign previously equipped item as the hover item
	// 이전에 장착된 아이템을 호버 아이템으로 지정	
	Grid_Equippables->AssignHoverItem(ItemToUnequip);
	
	// Remove of the equipped slotted item from the equipped grid slot (unbind from the OnEquippedSlottedItemClicked)
	// 장착된 그리드 슬롯에서 장착된 슬롯 아이템 제거 (OnEquippedSlottedItemClicked에서 바인딩 해제)
	RemoveEquippedSlottedItem(EquippedSlottedItem);
	
	// Make a new equipped slotted item (for the item we held in HoverItem)
	// 호버 아이템에 들고 있던 아이템을 위한 새로운 장착된 슬롯 아이템 만들기
	MakeEquippedSlottedItem(EquippedSlottedItem, EquippedGridSlot, ItemToEquip);
	
	// Broadcast delegates for OnItemEquipped/OnItemUnequipped (from the IC)
	// IC에서 OnItemEquipped/OnItemUnequipped에 대한 델리게이트 방송
	BroadcastSlotClickedDelegates(ItemToEquip, ItemToUnequip, WeaponSlotIndex);
}

// 마우스 버튼 다운 이벤트 처리 인벤토리 아이템 드롭
FReply UInv_SpatialInventory::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	ActiveGrid->DropItem();
	return FReply::Handled();
}

// 매 프레임마다 호출되는 틱 함수 (마우스 Hover에 사용)
void UInv_SpatialInventory::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	
	if (!IsValid(ItemDescription)) return; // 아이템 설명 위젯이 유효하지 않으면 반환
	SetItemDescriptionSizeAndPosition(ItemDescription, CanvasPanel); // 아이템 설명 크기 및 위치 설정
	SetEquippedItemDescriptionSizeAndPosition(ItemDescription, EquippedItemDescription, CanvasPanel);
}

// 마우스를 올려둘 때 뜨는 아이템 설명 크기 및 위치 설정
void UInv_SpatialInventory::SetItemDescriptionSizeAndPosition(UInv_ItemDescription* Description, UCanvasPanel* Canvas) const
{
	UCanvasPanelSlot* ItemDescriptionCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(Description);
	if (!IsValid(ItemDescriptionCPS)) return;

	const FVector2D ItemDescriptionSize = Description->GetBoxSize();
	ItemDescriptionCPS->SetSize(ItemDescriptionSize);

	FVector2D ClampedPosition = UInv_WidgetUtils::GetClampedWidgetPosition(
		UInv_WidgetUtils::GetWidgetSize(Canvas),
		ItemDescriptionSize,
		UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer()));

	ItemDescriptionCPS->SetPosition(ClampedPosition);
}

void UInv_SpatialInventory::SetEquippedItemDescriptionSizeAndPosition(UInv_ItemDescription* Description, UInv_ItemDescription* EquippedDescription, UCanvasPanel* Canvas) const
{
	UCanvasPanelSlot* ItemDescriptionCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(Description);
	UCanvasPanelSlot* EquippedItemDescriptionCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(EquippedDescription);
	if (!IsValid(ItemDescriptionCPS) || !IsValid(EquippedItemDescriptionCPS)) return;

	const FVector2D ItemDescriptionSize = Description->GetBoxSize();
	const FVector2D EquippedItemDescriptionSize = EquippedDescription->GetBoxSize();

	FVector2D ClampedPosition = UInv_WidgetUtils::GetClampedWidgetPosition(
		UInv_WidgetUtils::GetWidgetSize(Canvas),
		ItemDescriptionSize,
		UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer()));
	ClampedPosition.X -= EquippedItemDescriptionSize.X; 

	EquippedItemDescriptionCPS->SetSize(EquippedItemDescriptionSize);
	EquippedItemDescriptionCPS->SetPosition(ClampedPosition);
}

// 호버 아이템 장착 가능 여부 확인 게임태그도 참조해야 낄 수 있게.
bool UInv_SpatialInventory::CanEquipHoverItem(UInv_EquippedGridSlot* EquippedGridSlot, const FGameplayTag& EquipmentTypeTag) const
{
		if (!IsValid(EquippedGridSlot) || EquippedGridSlot->GetInventoryItem().IsValid()) return false; // 슬롯에 이미 아이템이 있으면 false 반환 (수정됨)

	UInv_HoverItem* HoverItem = GetHoverItem();
	if (!IsValid(HoverItem)) return false; // 호버 아이템이 유효하지 않으면 false 반환
	
	UInv_InventoryItem* HeldItem = HoverItem->GetInventoryItem(); // 호버 아이템에서 인벤토리 아이템 가져오기
	
	// Check if the held item is non-stackable and equippable
	// 들고 있는 아이템이 스택 불가능하고 장착 가능한지 확인
	return HasHoverItem() && IsValid(HeldItem) &&
		!HoverItem->IsStackable() &&
			HeldItem->GetItemManifest().GetItemCategory() == EInv_ItemCategory::Equippable &&
				HeldItem->GetItemManifest().GetItemType().MatchesTag(EquipmentTypeTag);
}

// 캡처한 포인터와 동일한 인벤토리 항목에 있는지 확인하는 것.
UInv_EquippedGridSlot* UInv_SpatialInventory::FindSlotWithEquippedItem(UInv_InventoryItem* EquippedItem) const
{
	auto* FoundEquippedGridSlot = EquippedGridSlots.FindByPredicate([EquippedItem](const UInv_EquippedGridSlot* GridSlot)
	{
		return GridSlot->GetInventoryItem() == EquippedItem; // 장착된 아이템과 슬롯의 아이템이 같은지 비교
	});
	
	return FoundEquippedGridSlot ? *FoundEquippedGridSlot : nullptr;
}

// 장착된 아이템을 그리드 슬롯에서 제거  
void UInv_SpatialInventory::ClearSlotOfItem(UInv_EquippedGridSlot* EquippedGridSlot)
{
	if (IsValid(EquippedGridSlot)) // 슬롯이 유효한 경우
	{
		EquippedGridSlot->SetEquippedSlottedItem(nullptr); // 장착된 슬롯 아이템을 nullptr로 설정하여 제거
		EquippedGridSlot->SetInventoryItem(nullptr); // 슬롯의 인벤토리 아이템을 nullptr로 설정하여 제거
	}
}

void UInv_SpatialInventory::RemoveEquippedSlottedItem(UInv_EquippedSlottedItem* EquippedSlottedItem)
{
	if (!IsValid(EquippedSlottedItem)) return; // 장착된 슬롯 아이템이 유효하지 않으면 반환
	
	if (EquippedSlottedItem->OnEquippedSlottedItemClicked.IsAlreadyBound(this, &ThisClass::EquippedSlottedItemClicked)) // 델리게이트가 이미 바인딩되어 있는지 확인
	{
		EquippedSlottedItem->OnEquippedSlottedItemClicked.RemoveDynamic(this, &ThisClass::EquippedSlottedItemClicked); // 델리게이트 바인딩 해제
	}
	
	EquippedSlottedItem->RemoveFromParent(); // 부모에서 장착된 슬롯 아이템 제거
}

// 장착된 슬롯에 아이템 만들기
void UInv_SpatialInventory::MakeEquippedSlottedItem(UInv_EquippedSlottedItem* EquippedSlottedItem, UInv_EquippedGridSlot* EquippedGridSlot, UInv_InventoryItem* ItemToEquip) 
{
	if (!IsValid(EquippedGridSlot)) return;
	
	UInv_EquippedSlottedItem* SlottedItem = EquippedGridSlot->OnItemEquipped(
		ItemToEquip, 
		EquippedSlottedItem->GetEquipmentTypeTag(), 
		UInv_InventoryStatics::GetInventoryWidget(GetOwningPlayer())->GetTileSize());
	if (IsValid(SlottedItem))SlottedItem->OnEquippedSlottedItemClicked.AddDynamic(this, &ThisClass::EquippedSlottedItemClicked);
	
	//새로 아이템을 장착할 바인딩 되길 바람
	EquippedGridSlot->SetEquippedSlottedItem(SlottedItem);
}

// ============================================
// 🆕 [Phase 6] 장착 아이템 복원 (델리게이트 바인딩 포함)
// ============================================
UInv_EquippedSlottedItem* UInv_SpatialInventory::RestoreEquippedItem(UInv_EquippedGridSlot* EquippedGridSlot, UInv_InventoryItem* ItemToEquip)
{
	if (!IsValid(EquippedGridSlot) || !IsValid(ItemToEquip))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RestoreEquippedItem] 유효하지 않은 인자!"));
#endif
		return nullptr;
	}

	// TileSize 가져오기
	auto* InventoryWidget = UInv_InventoryStatics::GetInventoryWidget(GetOwningPlayer());
	if (!IsValid(InventoryWidget))
	{
#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Error, TEXT("[RestoreEquippedItem] ❌ InventoryWidget이 nullptr!"));
#endif
		return nullptr;
	}
	const float TileSize = InventoryWidget->GetTileSize();

#if INV_DEBUG_WIDGET
	// 🔍 [Phase 8] TileSize 디버깅
	UE_LOG(LogTemp, Warning, TEXT("[RestoreEquippedItem] InventoryWidget: %s, TileSize: %.1f"),
		*InventoryWidget->GetName(), TileSize);

	if (TileSize <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[RestoreEquippedItem] ❌ TileSize가 0 이하! 위젯이 안 보일 수 있음!"));
	}
#endif
	
	// 장착 아이템의 태그 가져오기
	FGameplayTag EquipmentTag = ItemToEquip->GetItemManifest().GetItemType();
	
	// 장착 슬롯에 아이템 배치 (UI 위젯 생성)
	UInv_EquippedSlottedItem* EquippedSlottedItem = EquippedGridSlot->OnItemEquipped(ItemToEquip, EquipmentTag, TileSize);
	
	if (IsValid(EquippedSlottedItem))
	{
		// ⚠️ 핵심: 클릭 델리게이트 바인딩 (드래그&드롭 장착 해제용)
		EquippedSlottedItem->OnEquippedSlottedItemClicked.AddDynamic(this, &ThisClass::EquippedSlottedItemClicked);

#if INV_DEBUG_WIDGET
		UE_LOG(LogTemp, Warning, TEXT("[RestoreEquippedItem] ✅ 델리게이트 바인딩 완료: %s → 슬롯 %d"),
			*EquipmentTag.ToString(), EquippedGridSlot->GetWeaponSlotIndex());
#endif
	}

	return EquippedSlottedItem;
}

void UInv_SpatialInventory::BroadcastSlotClickedDelegates(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex) const
{
	UInv_InventoryComponent* InventoryComponent = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	check(IsValid(InventoryComponent));
	InventoryComponent->Server_EquipSlotClicked(ItemToEquip, ItemToUnequip, WeaponSlotIndex);
	
	// 데디서버일경우는 이런 걸 걱정 할 필요 없다.
	if (GetOwningPlayer()->GetNetMode() != NM_DedicatedServer)
	{
		// ⭐ [WeaponBridge] 유효한 아이템이 있을 때만 브로드캐스트
		if (IsValid(ItemToEquip))
		{
			InventoryComponent->OnItemEquipped.Broadcast(ItemToEquip, WeaponSlotIndex);
		}
		if (IsValid(ItemToUnequip))
		{
			InventoryComponent->OnItemUnequipped.Broadcast(ItemToUnequip, WeaponSlotIndex);
		}
	}
}


FInv_SlotAvailabilityResult UInv_SpatialInventory::HasRoomForItem(UInv_ItemComponent* ItemComponent) const // 아이템 컴포넌트가 있는지 확인
{
	switch (UInv_InventoryStatics::GetItemCategoryFromItemComp(ItemComponent))
	{
	case EInv_ItemCategory::Equippable:
		return Grid_Equippables->HasRoomForItem(ItemComponent);
	case EInv_ItemCategory::Consumable:
		return Grid_Consumables->HasRoomForItem(ItemComponent);
	case EInv_ItemCategory::Craftable:
		return Grid_Craftables->HasRoomForItem(ItemComponent);
	default:
		UE_LOG(LogInventory, Error, TEXT("ItemComponent doesn't have a valid Item Category. (inventory.h)"))
			return FInv_SlotAvailabilityResult(); // 빈 결과 반환
	}
}

// 아이템이 호버되었을 때 호출되는 함수 (설명 칸 보일 때 쓰는 부분들임)
void UInv_SpatialInventory::OnItemHovered(UInv_InventoryItem* Item)
{
	const auto& Manifest = Item->GetItemManifest(); // 아이템 매니페스트 가져오기
	UInv_ItemDescription* DescriptionWidget = GetItemDescription();
	DescriptionWidget->SetVisibility(ESlateVisibility::Collapsed);
	
	GetOwningPlayer()->GetWorldTimerManager().ClearTimer(DescriptionTimer); // 기존 타이머 클리어
	GetOwningPlayer()->GetWorldTimerManager().ClearTimer(EquippedDescriptionTimer); // 두 번째 장비 보이는 것. (장착 장비)
	
	FTimerDelegate DescriptionTimerDelegate;
	DescriptionTimerDelegate.BindLambda([this, Item, &Manifest, DescriptionWidget]()
	{
		// Assimalate the manifest into the Item Description widget.
		// 아이템 설명 위젯에 매니페스트 동화
		GetItemDescription()->SetVisibility(ESlateVisibility::HitTestInvisible); // 설명 위젯 보이기
		Manifest.AssimilateInventoryFragments(DescriptionWidget);
		
		// For the second item description, showing the equipped item of this type.
		// 두 번째 아이템 설명의 경우, 이 유형의 장착된 아이템을 보여줌.
		FTimerDelegate EquippedDescriptionTimerDelegate;
		EquippedDescriptionTimerDelegate.BindUObject(this, &ThisClass::ShowEquippedItemDescription, Item);
		GetOwningPlayer()->GetWorldTimerManager().SetTimer(EquippedDescriptionTimer, EquippedDescriptionTimerDelegate, EquippedDescriptionTimerDelay, false);
	});
	
	// 타이머 설정
	GetOwningPlayer()->GetWorldTimerManager().SetTimer(DescriptionTimer, DescriptionTimerDelegate, DescriptionTimerDelay, false);
}

//아이템에서 마우스에 손을 땔 떄
void UInv_SpatialInventory::OnItemUnHovered()
{
	GetItemDescription()->SetVisibility(ESlateVisibility::Collapsed); // 설명 위젯 숨기기
	GetOwningPlayer()->GetWorldTimerManager().ClearTimer(DescriptionTimer); // 타이머 클리어
	GetEquippedItemDescription()->SetVisibility(ESlateVisibility::Collapsed);
	GetOwningPlayer()->GetWorldTimerManager().ClearTimer(EquippedDescriptionTimer);
}

bool UInv_SpatialInventory::HasHoverItem() const // UI 마우스 호버 부분들
{
	if (Grid_Equippables->HasHoverItem()) return true;
	if (Grid_Consumables->HasHoverItem()) return true;
	if (Grid_Craftables->HasHoverItem()) return true;
	return false;
}

// 활성 그리드가 유효한 경우 호버 아이템 반환.
UInv_HoverItem* UInv_SpatialInventory::GetHoverItem() const
{
	if (!ActiveGrid.IsValid()) return nullptr; // 액터 그리드가 유효하지 않으면 nullptr 반환
	
	return ActiveGrid->GetHoverItem(); // 활성 그리드에서 호버 아이템 반환
}

float UInv_SpatialInventory::GetTileSize() const
{
	return Grid_Equippables->GetTileSize(); // 장비 그리드의 타일 크기 반환
}

void UInv_SpatialInventory::ShowEquippedItemDescription(UInv_InventoryItem* Item)
{
	const auto& Manifest = Item->GetItemManifest();
	const FInv_EquipmentFragment* EquipmentFragment = Manifest.GetFragmentOfType<FInv_EquipmentFragment>();
	if (!EquipmentFragment) return;

	const FGameplayTag HoveredEquipmentType = EquipmentFragment->GetEquipmentType();
	
	auto EquippedGridSlot = EquippedGridSlots.FindByPredicate([Item](const UInv_EquippedGridSlot* GridSlot)
	{
		return GridSlot->GetInventoryItem() == Item;
	});
	if (EquippedGridSlot != nullptr) return; // The hovered item is already equipped, we're already showing its Item Description

	// It's not equipped, so find the equipped item with the same equipment type
	auto FoundEquippedSlot = EquippedGridSlots.FindByPredicate([HoveredEquipmentType](const UInv_EquippedGridSlot* GridSlot)
	{
		UInv_InventoryItem* InventoryItem = GridSlot->GetInventoryItem().Get();
		return IsValid(InventoryItem) ? InventoryItem->GetItemManifest().GetFragmentOfType<FInv_EquipmentFragment>()->GetEquipmentType() == HoveredEquipmentType : false ;
	});
	UInv_EquippedGridSlot* EquippedSlot = FoundEquippedSlot ? *FoundEquippedSlot : nullptr;
	if (!IsValid(EquippedSlot)) return; // No equipped item with the same equipment type

	UInv_InventoryItem* EquippedItem = EquippedSlot->GetInventoryItem().Get();
	if (!IsValid(EquippedItem)) return;

	const auto& EquippedItemManifest = EquippedItem->GetItemManifest();
	UInv_ItemDescription* DescriptionWidget = GetEquippedItemDescription();

	//장비 비교하기 칸을 조절하려면 이 칸을 조절하면 됨. (장비 비교)
	auto EquippedDescriptionWidget = GetEquippedItemDescription();
	
	EquippedDescriptionWidget->Collapse();
	DescriptionWidget->SetVisibility(ESlateVisibility::HitTestInvisible);	
	EquippedItemManifest.AssimilateInventoryFragments(EquippedDescriptionWidget);
	// 여기까지 장비 비교 칸 조절.
}

UInv_ItemDescription* UInv_SpatialInventory::GetItemDescription() // 아이템 설명 위젯 가져오기
{
	if (!IsValid(ItemDescription))
	{
		ItemDescription = CreateWidget<UInv_ItemDescription>(GetOwningPlayer(), ItemDescriptionClass); // 아이템 설명 위젯 생성
		CanvasPanel->AddChild(ItemDescription);
	}
	return ItemDescription;
}

UInv_ItemDescription* UInv_SpatialInventory::GetEquippedItemDescription()
{
	if (!IsValid(EquippedItemDescription))
	{
		EquippedItemDescription = CreateWidget<UInv_ItemDescription>(GetOwningPlayer(), EquippedItemDescriptionClass);
		CanvasPanel->AddChild(EquippedItemDescription);
	}
	return EquippedItemDescription;
}

void UInv_SpatialInventory::ShowEquippables()
{
	SetActiveGrid(Grid_Equippables, Button_Equippables);
}

void UInv_SpatialInventory::ShowConsumables()
{
	SetActiveGrid(Grid_Consumables, Button_Consumables);
}

void UInv_SpatialInventory::ShowCraftables()
{
	SetActiveGrid(Grid_Craftables, Button_Craftables);
}



//리펙토링을 이렇게 하네 신기하다. 일단 버튼 비활성화 부분
void UInv_SpatialInventory::DisableButton(UButton* Button)
{
	Button_Equippables->SetIsEnabled(true);
	Button_Consumables->SetIsEnabled(true);
	Button_Craftables->SetIsEnabled(true);
	Button->SetIsEnabled(false);
}

// 그리드가 활성 되면 등장하는 것들.
void UInv_SpatialInventory::SetActiveGrid(UInv_InventoryGrid* Grid, UButton* Button) 
{
	if (ActiveGrid.IsValid())
	{
		ActiveGrid->HideCursor();
		ActiveGrid->OnHide();
	}
	ActiveGrid = Grid;
	if (ActiveGrid.IsValid()) ActiveGrid->ShowCursor();
	DisableButton(Button);
	Switcher->SetActiveWidget(Grid);
}

// ⭐ UI 기반 재료 개수 세기 (Split된 스택도 정확히 계산!)
int32 UInv_SpatialInventory::GetTotalMaterialCountFromUI(const FGameplayTag& MaterialTag) const
{
	if (!MaterialTag.IsValid()) return 0;

	int32 TotalCount = 0;

	// 모든 그리드 순회 (Craftables가 재료 그리드)
	TArray<UInv_InventoryGrid*> GridsToCheck = { Grid_Craftables, Grid_Consumables };

	for (UInv_InventoryGrid* Grid : GridsToCheck)
	{
		if (!IsValid(Grid)) continue;

		// Grid의 GridSlots를 직접 읽어서 개수 합산
		TotalCount += Grid->GetTotalMaterialCountFromSlots(MaterialTag);
	}

#if INV_DEBUG_WIDGET
	UE_LOG(LogTemp, Log, TEXT("GetTotalMaterialCountFromUI(%s) = %d (모든 그리드 합산)"), *MaterialTag.ToString(), TotalCount);
#endif
	return TotalCount;
}

