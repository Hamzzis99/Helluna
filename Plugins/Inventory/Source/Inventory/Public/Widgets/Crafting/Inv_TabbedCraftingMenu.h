// Gihyeon's Inventory Project
// @author 김기현

// 탭형 크래프팅 메뉴 위젯
// 장비/소모품/부착물 3개 탭으로 구성된 크래프팅 UI
// DataAsset(UInv_CraftingRecipeDA)에서 레시피를 읽어 엔트리 위젯을 동적 생성

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Crafting/Data/Inv_CraftingRecipeData.h"
#include "Inv_TabbedCraftingMenu.generated.h"

class UWidgetSwitcher;
class UScrollBox;
class UButton;
class UInv_InventoryItem;
class UInv_TabbedCraftingEntry;
struct FInv_SlotAvailabilityResult;

/**
 * 탭형 크래프팅 메뉴 위젯
 * WBP_Inv_TabbedCraftingMenu의 C++ 부모 클래스
 *
 * 위젯 트리:
 *  CanvasPanel (Root)
 *   +- HorizontalBox_Tabs (탭 버튼 컨테이너)
 *   |   +- Button_Tab_Equipment
 *   |   +- Button_Tab_Consumable
 *   |   +- Button_Tab_Attachment
 *   +- WidgetSwitcher_Content
 *   |   +- ScrollBox_Equipment   (Index 0)
 *   |   +- ScrollBox_Consumable  (Index 1)
 *   |   +- ScrollBox_Attachment  (Index 2)
 *   +- Button_Close
 */
UCLASS()
class INVENTORY_API UInv_TabbedCraftingMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	// 외부에서 DataAsset 설정 (CraftingStation에서 호출)
	UFUNCTION(BlueprintCallable, Category = "제작")
	void InitializeWithRecipes(UInv_CraftingRecipeDA* RecipeData);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 엔트리 위젯 Blueprint 클래스 (WBP_Inv_TabbedCraftingEntry)
	UPROPERTY(EditDefaultsOnly, Category = "제작",
		meta = (DisplayName = "엔트리 위젯 클래스"))
	TSubclassOf<UInv_TabbedCraftingEntry> EntryWidgetClass;

private:
	// === 탭 전환 ===
	UFUNCTION()
	void OnTabEquipmentClicked();
	UFUNCTION()
	void OnTabConsumableClicked();
	UFUNCTION()
	void OnTabAttachmentClicked();
	UFUNCTION()
	void OnCloseClicked();

	void SwitchTab(int32 TabIndex);
	void UpdateTabButtonStyles(int32 ActiveIndex);

	// === 레시피 -> 엔트리 위젯 생성 ===
	void PopulateCraftingEntries();
	void PopulateScrollBox(UScrollBox* TargetBox, const TArray<FInv_CraftingRecipe>& Recipes);

	// === 델리게이트 콜백 (인벤토리 변경 시 전체 엔트리 UI 갱신) ===
	void BindInventoryDelegates();
	void UnbindInventoryDelegates();

	UFUNCTION()
	void OnInventoryChanged(UInv_InventoryItem* Item, int32 EntryIndex);
	UFUNCTION()
	void OnStackChanged(const FInv_SlotAvailabilityResult& Result);
	UFUNCTION()
	void OnMaterialChanged(const FGameplayTag& MaterialTag);

	void RefreshAllEntryUI();

	// === 바인딩 위젯 ===
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> WidgetSwitcher_Content;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> ScrollBox_Equipment;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> ScrollBox_Consumable;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> ScrollBox_Attachment;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Tab_Equipment;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Tab_Consumable;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Tab_Attachment;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Close;

	// === 데이터 ===
	UPROPERTY()
	TObjectPtr<UInv_CraftingRecipeDA> CachedRecipeData;

	// 현재 활성 탭 인덱스 (0=장비, 1=소모품, 2=부착물)
	int32 ActiveTabIndex = 0;

	// 생성된 엔트리 위젯 캐시 (갱신용)
	UPROPERTY()
	TArray<TObjectPtr<UInv_TabbedCraftingEntry>> AllEntryWidgets;
};
