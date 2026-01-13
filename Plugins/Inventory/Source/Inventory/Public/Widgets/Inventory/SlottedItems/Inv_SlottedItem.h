// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_SlottedItem.generated.h"

class UInv_InventoryItem;
class UImage;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSlottedItemClicked, int32, GridIndex, const FPointerEvent&, MouseEvent); // 슬롯 아이템 클릭 델리게이트 선언

UCLASS()
class INVENTORY_API UInv_SlottedItem : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override; //마우스 클릭 상호작용 만들기
	virtual void NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override; // 마우스 엔터 상호작용 만들기
	virtual void NativeOnMouseLeave(const FPointerEvent& MouseEvent) override;
	
	bool IsStackable() const { return bIsStackable; } // 스택 가능 여부 가져오기
	void SetIsStackable(bool bStackable) { bIsStackable = bStackable; } // 스택 가능 여부 설정
	UImage* GetImageIcon() const {return Image_Icon;} // 아이콘 이미지 가져오기
	void SetGridIndex(int32 Index) { GridIndex = Index; } // 그리드 인덱스 설정
	int32 GetGridIndex() const { return GridIndex; } // 그리드 인덱스 가져오기
	void SetEntryIndex(int32 Index) { EntryIndex = Index; } // ⭐ Entry 인덱스 설정
	int32 GetEntryIndex() const { return EntryIndex; } // ⭐ Entry 인덱스 가져오기
	void SetGridDimensions(const FIntPoint& Dimensions) { GridDimensions = Dimensions; } // 그리드 크기 설정
	FIntPoint GetGridDimensions() const { return GridDimensions; } // 그리드 크기 가져오기
	void SetInventoryItem(UInv_InventoryItem* Item); // 인벤토리 아이템 설정
	UInv_InventoryItem* GetInventoryItem() const { return InventoryItem.Get(); } // 인벤토리 아이템 가져오기
	void SetImageBrush(const FSlateBrush& Brush) const; // 이미지 브러시 설정
	void UpdateStackCount(int32 StackCount); // 아이템 스택 수량 업데이트

	FSlottedItemClicked OnSlottedItemClicked; // 마우스로 슬롯 아이템 클릭 델리게이트

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Icon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_StackCount;

	int32 GridIndex{ -1 }; // 그리드 인덱스 (-1 = 미설정)
	int32 EntryIndex{ -1 }; // ⭐ Entry Index 저장 (-1 = 미설정)
	FIntPoint GridDimensions;
	TWeakObjectPtr<UInv_InventoryItem> InventoryItem;
	bool bIsStackable{ false };
};



