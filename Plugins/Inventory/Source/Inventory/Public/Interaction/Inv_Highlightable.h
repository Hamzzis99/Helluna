

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "Inv_Highlightable.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UInv_Highlightable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class INVENTORY_API IInv_Highlightable
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	UFUNCTION(BlueprintNativeEvent, Category = "상호작용|하이라이트", meta = (DisplayName = "하이라이트 켜기"))
	void Highlight();
	UFUNCTION(BlueprintNativeEvent, Category = "상호작용|하이라이트", meta = (DisplayName = "하이라이트 끄기"))
	void UnHighlight();
	
};
