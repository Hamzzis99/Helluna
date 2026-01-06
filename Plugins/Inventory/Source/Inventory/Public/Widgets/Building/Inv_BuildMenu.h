// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_BuildMenu.generated.h"

class UHorizontalBox;
class UInv_BuildingButton;

/**
 * 빌드 메뉴 메인 위젯
 * 블루프린트에서 UI 디자인 후 이 클래스를 상속받아 사용
 */
UCLASS()
class INVENTORY_API UInv_BuildMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

private:
	// === 블루프린트에서 바인딩할 위젯들 ===
	
	// 건물 버튼들을 담을 컨테이너 (Horizontal Box 또는 Wrap Box)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_Buildings;
};

