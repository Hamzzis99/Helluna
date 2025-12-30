// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Inv_GridTypes.h"
#include "Inv_InventoryBase.generated.h"

class UInv_ItemComponent;
class UInv_InventoryItem;
class UInv_HoverItem;
UCLASS()
class INVENTORY_API UInv_InventoryBase : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual FInv_SlotAvailabilityResult HasRoomForItem(UInv_ItemComponent* ItemComponent) const { return FInv_SlotAvailabilityResult(); }
	
	virtual void OnItemHovered(UInv_InventoryItem* Item){} // 아이템 호버 시 호출되는 가상 함수
	virtual void OnItemUnHovered() {} // 아이템 언호버 시 호출되는 가상 함수
	virtual bool HasHoverItem() const {return false;} // 호버된 아이템이 있는지 확인하는 가상 함수
	virtual UInv_HoverItem* GetHoverItem() const {return nullptr;} // 호버된 아이템을 반환하는 가상 함수
	virtual float GetTileSize() const {return 0.f;} // 타일 크기를 반환하는 가상 함수
};