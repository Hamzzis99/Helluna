// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inv_SlottedItem.h"

#include "Inv_EquippedSlottedItem.generated.h"

// UInv_EquippeSlootedItem <- 델리게이트 안에서 전방선언 한 것.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEquippedSlottedItemClicked, class UInv_EquippedSlottedItem*, SlottedItem);

/**
 * 
 */
UCLASS()
class INVENTORY_API UInv_EquippedSlottedItem : public UInv_SlottedItem
{
	GENERATED_BODY()
	
public:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	
	// 장착된 장비 유형 태그 설정 및 가져오기
	void SetEquipmentTypeTag(const FGameplayTag& Tag) { EquipmentTypeTag = Tag;}
	FGameplayTag GetEquipmentTypeTag() const { return EquipmentTypeTag; }
	
	FEquippedSlottedItemClicked OnEquippedSlottedItemClicked; // 장착된 슬롯 아이템 클릭 델리게이트
private:
	UPROPERTY()
	FGameplayTag EquipmentTypeTag;
};
