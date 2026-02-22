// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Inv_CompositeBase.generated.h"

/**
 * 
 */
UCLASS()
class INVENTORY_API UInv_CompositeBase : public UUserWidget
{
	GENERATED_BODY()
public:
	FGameplayTag GetFragmentTag() const {return FragmentTag;}
	void SetFragmentTag(const FGameplayTag& Tag) {FragmentTag = Tag;}
	virtual void Collapse();
	void Expand();
	
	using FuncType = TFunction<void(UInv_CompositeBase*)>;
	virtual void ApplyFunction(FuncType Function) {}
private:
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "프래그먼트 태그",
		Tooltip = "이 컴포지트 노드가 대응하는 아이템 프래그먼트의 게임플레이 태그입니다."))
	FGameplayTag FragmentTag;
};
