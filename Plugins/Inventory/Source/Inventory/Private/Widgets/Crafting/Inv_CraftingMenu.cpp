// Gihyeon's Inventory Project

#include "Widgets/Crafting/Inv_CraftingMenu.h"
#include "Inventory.h"
#include "Components/VerticalBox.h"
#include "Widgets/Crafting/Inv_CraftingButton.h"

void UInv_CraftingMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!IsValid(VerticalBox_CraftingButtons))
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Error, TEXT("Inv_CraftingMenu: VerticalBox_CraftingButtons is not bound! Make sure the widget has a Vertical Box named 'VerticalBox_CraftingButtons'."));
#endif
	}
	else
	{
#if INV_DEBUG_CRAFT
		UE_LOG(LogTemp, Warning, TEXT("Inv_CraftingMenu: Initialized successfully!"));
#endif
	}
}

