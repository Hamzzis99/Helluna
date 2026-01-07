// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_CraftingMenu.generated.h"

class UVerticalBox;
class UInv_CraftingButton;

/**
 * 크래프팅 메뉴 메인 위젯
 * 블루프린트에서 UI 디자인 후 이 클래스를 상속받아 사용
 */
UCLASS()
class INVENTORY_API UInv_CraftingMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

private:
	// === 블루프린트에서 바인딩할 위젯들 ===
	
	// 제작 버튼들을 담을 컨테이너 (Vertical Box 또는 Wrap Box)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> VerticalBox_CraftingButtons;
};

