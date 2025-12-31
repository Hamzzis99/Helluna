// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Inv_CompositeBase.h"
#include "Inv_Composite.generated.h"

/**
 * 
 */
UCLASS()
class INVENTORY_API UInv_Composite : public UInv_CompositeBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void ApplyFunction(FuncType Function) override;
	virtual void Collapse() override;
	TArray<UInv_CompositeBase*> GetChildren() {return Children;} // 자식 컴포지트 배열 반환 
	
private:
	UPROPERTY()
	TArray<TObjectPtr<UInv_CompositeBase>> Children;
};
