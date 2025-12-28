// Gihyeon's Inventory Project
// HUD의 잎사귀들을 만드는 건데. 트리 구조 기억하지 원소를 Leaf라고 부르잖아. 그거 만드는거임.

#pragma once

#include "CoreMinimal.h"
#include "Inv_CompositeBase.h"
#include "Inv_Leaf.generated.h"

/**
 * 
 */
UCLASS()
class INVENTORY_API UInv_Leaf : public UInv_CompositeBase
{
	GENERATED_BODY()
public:
	virtual void ApplyFunction(FuncType Function) override; // 함수 적용 메서드
};
