// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Blueprint/UserWidget.h"
#include "Inv_HoverItem.generated.h"

/**
 * GridSlot 내에서 아이템을 마우스로 쭈욱 끌어당겨 테트리스를 할 수 있어요!
 */
class UInv_InventoryItem;
class UImage;
class UTextBlock;

UCLASS()
class INVENTORY_API UInv_HoverItem : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetImageBrush(const FSlateBrush& Brush) const; //슬레이트 브러시 설정 함수
	void UpdateStackCount(const int32 Count);

	//참조를 하기 위한 그리드에서 기존 얻던 것들. (오로지 이동하기 위해)
	FGameplayTag GetItemType() const;
	int32 GetStackCount() const { return StackCount; }
	bool IsStackable() const { return bIsStackable; }
	void SetIsStackable(bool bStacks);
	int32 GetPreviousGridIndex() const { return PreviousGridIndex; }
	void SetPreviousGridIndex(int32 Index) { PreviousGridIndex = Index; }
	FIntPoint GetGridDimensions() const { return GridDimensions; }
	void SetGridDimensions(const FIntPoint& Dimensions) { GridDimensions = Dimensions; }
	UInv_InventoryItem* GetInventoryItem() const;
	void SetInventoryItem(UInv_InventoryItem* Item);

	// ⭐ FastArray Entry Index (서버-클라이언트 포인터 불일치 해결용)
	int32 GetEntryIndex() const { return EntryIndex; }
	void SetEntryIndex(int32 Index) { EntryIndex = Index; }

	// ⭐ Phase 8: Split 아이템 플래그 (서버에 새 Entry 생성 필요 여부)
	bool IsSplitItem() const { return bIsSplitItem; }
	void SetIsSplitItem(bool bValue) { bIsSplitItem = bValue; }
	
	// ⭐ Phase 8: Split 시 원본 아이템 (서버 RPC용)
	UInv_InventoryItem* GetOriginalSplitItem() const { return OriginalSplitItem.Get(); }
	void SetOriginalSplitItem(UInv_InventoryItem* Item);

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_Icon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_StackCount;

	//이전 그리드 인덱스를 알아내는 변수 우리가 Grid에서 알아내야 할 변수들
	int32 PreviousGridIndex;
	int32 EntryIndex{ INDEX_NONE }; // ⭐ FastArray Entry Index
	FIntPoint GridDimensions;
	TWeakObjectPtr<UInv_InventoryItem> InventoryItem;
	bool bIsStackable{ false };
	int32 StackCount{ 0 };
	
	// ⭐ Phase 8: Split 관련 변수
	bool bIsSplitItem{ false }; // Split으로 생성된 HoverItem인지 플래그
	TWeakObjectPtr<UInv_InventoryItem> OriginalSplitItem; // Split 시 원본 아이템 (서버 RPC용)

};
