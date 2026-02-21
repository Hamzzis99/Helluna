// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Inv_GridSlot.h"
#include "GameplayTagContainer.h"
#include "Inv_EquippedGridSlot.generated.h"

// 델리게이트 선언: 장착된 그리드 슬롯이 클릭되었을 때 호출되는 델리게이트
class UInv_EquippedSlottedItem;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEquippedGridSlotClicked, UInv_EquippedGridSlot*, GridSlot, const FGameplayTag&, EquipmentTypeTag);

class UImage;
class UOverlay;
UCLASS()
class INVENTORY_API UInv_EquippedGridSlot : public UInv_GridSlot
{
	GENERATED_BODY()
public:
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	
	UInv_EquippedSlottedItem* OnItemEquipped(UInv_InventoryItem* Item, const FGameplayTag& EquipmentTag, float TileSize); // 아이템 장착 시 호출되는 함수
	void SetEquippedSlottedItem(UInv_EquippedSlottedItem* Item) {EquippedSlottedItem = Item; } // 장착된 슬롯 아이템 설정

	FEquippedGridSlotClicked EquippedGridSlotClicked; // 장착된 그리드 슬롯 클릭 이벤트 델리게이트

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스 Getter
	// ⭐ 0 = 주무기, 1 = 보조무기
	// ============================================
	int32 GetWeaponSlotIndex() const { return WeaponSlotIndex; }

	// ============================================
	// 🆕 [Phase 6] 장착된 아이템 가져오기
	// ============================================
	UInv_InventoryItem* GetEquippedInventoryItem() const;

	// ============================================
	// 🆕 [Phase 8] 레이아웃 갱신 (인벤토리 열릴 때 호출)
	// ============================================
	void RefreshLayout();

private:
	UPROPERTY(EditAnywhere, Category = "인벤토리|장비", meta = (DisplayName = "장비 타입 태그", Tooltip = "이 슬롯에 장착 가능한 장비 유형을 나타내는 게임플레이 태그입니다.", Categories = "GameItem.Equipment"))
	FGameplayTag EquipmentTypeTag; // 장착된 아이템의 타입을 나타내는 게임플레이 태그

	// ============================================
	// ⭐ [WeaponBridge] 무기 슬롯 인덱스
	// ⭐ 블루프린트에서 설정 (0 = 주무기, 1 = 보조무기)
	// ⭐ 무기 슬롯이 아닌 경우 -1
	// ============================================
	UPROPERTY(EditAnywhere, Category = "인벤토리|무기", meta = (DisplayName = "무기 슬롯 인덱스", Tooltip = "무기 슬롯 번호입니다. 0 = 주무기, 1 = 보조무기, -1 = 무기 슬롯 아님."))
	int32 WeaponSlotIndex = -1;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_GrayedOutIcon; // 호버했을 때 상황
	
	UPROPERTY(EditAnywhere, Category = "인벤토리|장비", meta = (DisplayName = "장착 슬롯 아이템 클래스", Tooltip = "장착 슬롯에 사용할 위젯 블루프린트 클래스입니다."))
	TSubclassOf<UInv_EquippedSlottedItem> EquippedSlottedItemClass; // 장착된 슬롯 아이템의 클래스 타입
	
	UPROPERTY()
	TObjectPtr<UInv_EquippedSlottedItem> EquippedSlottedItem; // 장착된 슬롯 아이템 인스턴스

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UOverlay> Overlay_Root; // 오버레이 루트 위젯

	// 🆕 [Phase 8] 레이아웃 갱신용 캐시
	FVector2D CachedDrawSize = FVector2D::ZeroVector;
};