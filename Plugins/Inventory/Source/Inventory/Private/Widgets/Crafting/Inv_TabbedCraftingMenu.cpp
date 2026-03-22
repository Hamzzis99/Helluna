// Gihyeon's Inventory Project
// @author 김기현

#include "Widgets/Crafting/Inv_TabbedCraftingMenu.h"
#include "Inventory.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ScrollBox.h"
#include "Components/Button.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "InventoryManagement/Utils/Inv_InventoryStatics.h"
#include "Items/Inv_InventoryItem.h"
#include "Widgets/Crafting/Inv_TabbedCraftingEntry.h"

void UInv_TabbedCraftingMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 탭 버튼 OnClicked 바인딩
	if (IsValid(Button_Tab_Equipment))
	{
		Button_Tab_Equipment->OnClicked.AddUniqueDynamic(this, &ThisClass::OnTabEquipmentClicked);
	}

	if (IsValid(Button_Tab_Consumable))
	{
		Button_Tab_Consumable->OnClicked.AddUniqueDynamic(this, &ThisClass::OnTabConsumableClicked);
	}

	if (IsValid(Button_Tab_Attachment))
	{
		Button_Tab_Attachment->OnClicked.AddUniqueDynamic(this, &ThisClass::OnTabAttachmentClicked);
	}

	// 닫기 버튼 바인딩
	if (IsValid(Button_Close))
	{
		Button_Close->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCloseClicked);
	}
}

void UInv_TabbedCraftingMenu::NativeConstruct()
{
	Super::NativeConstruct();

	// 인벤토리 델리게이트 바인딩
	BindInventoryDelegates();

	// 장비 탭 기본 선택
	SwitchTab(0);

	// DataAsset이 이미 설정되어 있으면 엔트리 생성
	if (IsValid(CachedRecipeData))
	{
		PopulateCraftingEntries();
	}
}

void UInv_TabbedCraftingMenu::NativeDestruct()
{
	Super::NativeDestruct();

	// 델리게이트 언바인딩
	UnbindInventoryDelegates();
}

void UInv_TabbedCraftingMenu::InitializeWithRecipes(UInv_CraftingRecipeDA* RecipeData)
{
	if (!IsValid(RecipeData))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("TabbedCraftingMenu: RecipeData가 nullptr입니다!"));
#endif
		return;
	}

	CachedRecipeData = RecipeData;

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingMenu: InitializeWithRecipes - 총 레시피 %d개"), RecipeData->Recipes.Num());

	const int32 EquipCount = RecipeData->GetRecipesByCategory(EInv_ItemCategory::Equippable).Num();
	const int32 ConsumableCount = RecipeData->GetRecipesByCategory(EInv_ItemCategory::Consumable).Num();
	const int32 AttachmentCount = RecipeData->GetRecipesByCategory(EInv_ItemCategory::Craftable).Num();
	UE_LOG(LogTemp, Warning, TEXT("  장비: %d, 소모품: %d, 부착물: %d"), EquipCount, ConsumableCount, AttachmentCount);
#endif

	PopulateCraftingEntries();
}

void UInv_TabbedCraftingMenu::PopulateCraftingEntries()
{
	// 3개 ScrollBox 모두 초기화
	if (IsValid(ScrollBox_Equipment))
	{
		ScrollBox_Equipment->ClearChildren();
	}
	if (IsValid(ScrollBox_Consumable))
	{
		ScrollBox_Consumable->ClearChildren();
	}
	if (IsValid(ScrollBox_Attachment))
	{
		ScrollBox_Attachment->ClearChildren();
	}

	AllEntryWidgets.Empty();

	if (!IsValid(CachedRecipeData))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingMenu: PopulateCraftingEntries - CachedRecipeData 없음"));
#endif
		return;
	}

	if (!EntryWidgetClass)
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("TabbedCraftingMenu: EntryWidgetClass가 설정되지 않았습니다! Blueprint에서 설정하세요."));
#endif
		return;
	}

	// 카테고리별 레시피 필터 후 ScrollBox에 추가
	const TArray<FInv_CraftingRecipe> EquipRecipes = CachedRecipeData->GetRecipesByCategory(EInv_ItemCategory::Equippable);
	const TArray<FInv_CraftingRecipe> ConsumableRecipes = CachedRecipeData->GetRecipesByCategory(EInv_ItemCategory::Consumable);
	const TArray<FInv_CraftingRecipe> AttachmentRecipes = CachedRecipeData->GetRecipesByCategory(EInv_ItemCategory::Craftable);

	PopulateScrollBox(ScrollBox_Equipment, EquipRecipes);
	PopulateScrollBox(ScrollBox_Consumable, ConsumableRecipes);
	PopulateScrollBox(ScrollBox_Attachment, AttachmentRecipes);

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Warning, TEXT("TabbedCraftingMenu: PopulateCraftingEntries 완료 - 장비:%d, 소모품:%d, 부착물:%d"),
		EquipRecipes.Num(), ConsumableRecipes.Num(), AttachmentRecipes.Num());
#endif
}

void UInv_TabbedCraftingMenu::PopulateScrollBox(UScrollBox* TargetBox, const TArray<FInv_CraftingRecipe>& Recipes)
{
	if (!IsValid(TargetBox)) return;

	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC)) return;

	for (const FInv_CraftingRecipe& Recipe : Recipes)
	{
		UInv_TabbedCraftingEntry* Entry = CreateWidget<UInv_TabbedCraftingEntry>(PC, EntryWidgetClass);
		if (!IsValid(Entry)) continue;

		Entry->InitFromRecipe(Recipe);
		TargetBox->AddChild(Entry);
		AllEntryWidgets.Add(Entry);
	}
}

void UInv_TabbedCraftingMenu::OnTabEquipmentClicked()
{
	SwitchTab(0);
}

void UInv_TabbedCraftingMenu::OnTabConsumableClicked()
{
	SwitchTab(1);
}

void UInv_TabbedCraftingMenu::OnTabAttachmentClicked()
{
	SwitchTab(2);
}

void UInv_TabbedCraftingMenu::OnCloseClicked()
{
	// 입력 모드 복구
	APlayerController* PC = GetOwningPlayer();
	if (IsValid(PC))
	{
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
		PC->SetShowMouseCursor(false);
	}

	RemoveFromParent();
}

void UInv_TabbedCraftingMenu::SwitchTab(int32 TabIndex)
{
#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Log, TEXT("TabbedCraftingMenu: SwitchTab %d -> %d"), ActiveTabIndex, TabIndex);
#endif

	if (IsValid(WidgetSwitcher_Content))
	{
		WidgetSwitcher_Content->SetActiveWidgetIndex(TabIndex);
	}

	ActiveTabIndex = TabIndex;
	UpdateTabButtonStyles(TabIndex);
}

void UInv_TabbedCraftingMenu::UpdateTabButtonStyles(int32 ActiveIndex)
{
	// 활성 탭: IsEnabled=false (Blueprint에서 Disabled 스타일을 "선택된 탭"으로 디자인)
	// 비활성 탭: IsEnabled=true
	TObjectPtr<UButton> TabButtons[] = { Button_Tab_Equipment, Button_Tab_Consumable, Button_Tab_Attachment };

	for (int32 i = 0; i < 3; ++i)
	{
		if (IsValid(TabButtons[i]))
		{
			TabButtons[i]->SetIsEnabled(i != ActiveIndex);
		}
	}
}

void UInv_TabbedCraftingMenu::BindInventoryDelegates()
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InvComp)) return;

	if (!InvComp->OnItemAdded.IsAlreadyBound(this, &ThisClass::OnInventoryChanged))
	{
		InvComp->OnItemAdded.AddUniqueDynamic(this, &ThisClass::OnInventoryChanged);
	}

	if (!InvComp->OnItemRemoved.IsAlreadyBound(this, &ThisClass::OnInventoryChanged))
	{
		InvComp->OnItemRemoved.AddUniqueDynamic(this, &ThisClass::OnInventoryChanged);
	}

	if (!InvComp->OnStackChange.IsAlreadyBound(this, &ThisClass::OnStackChanged))
	{
		InvComp->OnStackChange.AddUniqueDynamic(this, &ThisClass::OnStackChanged);
	}

	if (!InvComp->OnMaterialStacksChanged.IsAlreadyBound(this, &ThisClass::OnMaterialChanged))
	{
		InvComp->OnMaterialStacksChanged.AddUniqueDynamic(this, &ThisClass::OnMaterialChanged);
	}

#if INV_DEBUG_CRAFT
	UE_LOG(LogTemp, Log, TEXT("TabbedCraftingMenu: 인벤토리 델리게이트 바인딩 완료"));
#endif
}

void UInv_TabbedCraftingMenu::UnbindInventoryDelegates()
{
	UInv_InventoryComponent* InvComp = UInv_InventoryStatics::GetInventoryComponent(GetOwningPlayer());
	if (!IsValid(InvComp)) return;

	InvComp->OnItemAdded.RemoveDynamic(this, &ThisClass::OnInventoryChanged);
	InvComp->OnItemRemoved.RemoveDynamic(this, &ThisClass::OnInventoryChanged);
	InvComp->OnStackChange.RemoveDynamic(this, &ThisClass::OnStackChanged);
	InvComp->OnMaterialStacksChanged.RemoveDynamic(this, &ThisClass::OnMaterialChanged);
}

void UInv_TabbedCraftingMenu::OnInventoryChanged(UInv_InventoryItem* Item, int32 EntryIndex)
{
	// [최적화] 변경된 아이템 타입을 재료로 사용하는 엔트리만 갱신
	if (!IsValid(Item))
	{
		RefreshAllEntryUI();
		return;
	}

	const FGameplayTag ItemType = Item->GetItemManifest().GetItemType();
	for (const TObjectPtr<UInv_TabbedCraftingEntry>& Entry : AllEntryWidgets)
	{
		if (IsValid(Entry) && Entry->UsesMaterial(ItemType))
		{
			Entry->RefreshMaterialUI();
		}
	}
}

void UInv_TabbedCraftingMenu::OnStackChanged(const FInv_SlotAvailabilityResult& Result)
{
	RefreshAllEntryUI();
}

void UInv_TabbedCraftingMenu::OnMaterialChanged(const FGameplayTag& MaterialTag)
{
	// [최적화] 해당 재료 태그를 사용하는 엔트리만 갱신
	if (!MaterialTag.IsValid())
	{
		RefreshAllEntryUI();
		return;
	}

	for (const TObjectPtr<UInv_TabbedCraftingEntry>& Entry : AllEntryWidgets)
	{
		if (IsValid(Entry) && Entry->UsesMaterial(MaterialTag))
		{
			Entry->RefreshMaterialUI();
		}
	}
}

void UInv_TabbedCraftingMenu::RefreshAllEntryUI()
{
	for (const TObjectPtr<UInv_TabbedCraftingEntry>& Entry : AllEntryWidgets)
	{
		if (IsValid(Entry))
		{
			Entry->RefreshMaterialUI();
		}
	}
}
