// Gihyeon's Inventory Project
// @author 김기현

// 탭형 크래프팅 메뉴의 개별 레시피 엔트리 위젯
// 기존 Inv_CraftingButton과 동일한 로직을 FInv_CraftingRecipe 기반으로 구현
// Server_CraftItemWithMaterials RPC를 통해 서버에서 재료 차감 + 아이템 생성

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/Data/Inv_CraftingRecipeData.h"
#include "Inv_TabbedCraftingEntry.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UHorizontalBox;

/**
 * 탭형 크래프팅 엔트리 위젯
 * WBP_Inv_TabbedCraftingEntry의 C++ 부모 클래스
 *
 * 위젯 트리:
 *  HorizontalBox (Root)
 *   +- Image_ItemIcon
 *   +- VerticalBox
 *   |   +- Text_ItemName
 *   |   +- HorizontalBox_Materials
 *   |       +- HorizontalBox_Material1 (Image_Material1 + Text_Material1Amount)
 *   |       +- HorizontalBox_Material2 (Image_Material2 + Text_Material2Amount)
 *   |       +- HorizontalBox_Material3 (Image_Material3 + Text_Material3Amount)
 *   +- Button_Craft
 */
UCLASS()
class INVENTORY_API UInv_TabbedCraftingEntry : public UUserWidget
{
	GENERATED_BODY()

public:
	// 레시피 데이터로 초기화 (TabbedCraftingMenu가 호출)
	UFUNCTION(BlueprintCallable, Category = "제작")
	void InitFromRecipe(const FInv_CraftingRecipe& Recipe);

	// 재료 UI 갱신 (외부에서 호출 가능)
	UFUNCTION(BlueprintCallable, Category = "제작")
	void RefreshMaterialUI();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void OnCraftButtonClicked();

	// === 기존 CraftingButton에서 가져온 핵심 로직 ===
	bool HasRequiredMaterials() const;
	void UpdateButtonState();
	void AddCraftedItemToInventory();

	// === BindWidget ===
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Craft;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Image_ItemIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_ItemName;

	// 재료 컨테이너 (BindWidgetOptional - 재료가 없으면 숨김)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material1;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Material1;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Material1Amount;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material2;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Material2;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Material2Amount;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HorizontalBox_Material3;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_Material3;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Material3Amount;

	// === 캐시된 레시피 데이터 ===
	FInv_CraftingRecipe CachedRecipe;

	// 쿨다운
	float LastCraftTime = 0.f;
};
