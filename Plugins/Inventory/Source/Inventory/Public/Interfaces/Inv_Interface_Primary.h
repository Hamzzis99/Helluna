// Gihyeon's Inventory Project
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Inv_Interface_Primary.generated.h"

// 언리얼 엔진 리플렉션용 (수정 불필요)
UINTERFACE(MinimalAPI)
class UInv_Interface_Primary : public UInterface
{
	GENERATED_BODY()
};

// 실제 사용할 인터페이스 클래스
class INVENTORY_API IInv_Interface_Primary
{
	GENERATED_BODY()

public:
	// 상호작용 함수 
	// 리턴값 bool: "내가 처리했으니(true) 다른 건 하지 마" 라는 신호용
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "상호작용",
		meta = (DisplayName = "상호작용 실행"))
	bool ExecuteInteract(APlayerController* Controller);
};