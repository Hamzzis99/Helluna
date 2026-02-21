// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Inv_Leaf.h"
#include "Inv_Leaf_Text.generated.h"

/**
 * 
 */

class UTextBlock;

UCLASS()
class INVENTORY_API UInv_Leaf_Text : public UInv_Leaf
{
	GENERATED_BODY()
public:
	void SetText(const FText& Text) const;
	virtual void NativePreConstruct() override;
	
private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_LeafText;
	
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "글꼴 크기",
		Tooltip = "리프 텍스트의 글꼴 크기입니다."))
	int32 FontSize{12};
};
