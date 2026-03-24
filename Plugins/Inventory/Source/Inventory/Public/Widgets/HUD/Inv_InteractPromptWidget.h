// Plugins/Inventory/Source/Inventory/Public/Widgets/HUD/Inv_InteractPromptWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inv_InteractPromptWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 3D 공간 상호작용 프롬프트 위젯
 * WidgetComponent에 부착되어 아이템 위에 표시됨
 * WBP에서 BindWidgetOptional로 바인딩
 */
UCLASS()
class INVENTORY_API UInv_InteractPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- BindWidgetOptional (WBP에서 배치) ----

	/** 키 아이콘 배경 이미지 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeyBgImage;

	/** 키 텍스트 ("E") */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	/** 아이템 이름 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemNameText;

	/** 액션 텍스트 ("줍기" 등) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	// ---- API ----

	/** 아이템 이름 설정 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Interact",
		meta = (DisplayName = "Set Item Name (아이템 이름 설정)"))
	void SetItemName(const FString& InName);

	/** 액션 텍스트 설정 ("줍기", "열기" 등) */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Interact",
		meta = (DisplayName = "Set Action Text (액션 텍스트 설정)"))
	void SetActionText(const FString& InAction);

	/** 키 텍스트 설정 ("E", "F" 등 — Enhanced Input에서 읽은 실제 키) */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Interact",
		meta = (DisplayName = "Set Key Text (키 텍스트 설정)"))
	void SetKeyText(const FString& InKey);

	/** 기본 시안 색상으로 리셋 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Interact",
		meta = (DisplayName = "Reset State (상태 리셋)"))
	void ResetState();
};
