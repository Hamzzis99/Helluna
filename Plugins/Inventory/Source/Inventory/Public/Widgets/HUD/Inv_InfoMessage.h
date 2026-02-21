// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_InfoMessage.generated.h"

class UTextBlock;
UCLASS()
class INVENTORY_API UInv_InfoMessage : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "인벤토리", meta = (DisplayName = "메시지 표시"))
	void MessageShow();

	UFUNCTION(BlueprintImplementableEvent, Category = "인벤토리", meta = (DisplayName = "메시지 숨기기"))
	void MessageHide();

	void SetMessage(const FText& Message);

private:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_Message;

	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "메시지 표시 시간",
		Tooltip = "정보 메시지가 화면에 표시되는 시간(초)입니다."))
	float MessageLifetime{ 3.f };

	FTimerHandle MessageTimer;
	bool bIsMessageActive{ false };
};
