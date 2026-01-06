// Gihyeon's Inventory Project

#include "Widgets/Building/Inv_BuildMenu.h"
#include "Components/HorizontalBox.h"
#include "Widgets/Building/Inv_BuildingButton.h"

void UInv_BuildMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!IsValid(HorizontalBox_Buildings))
	{
		UE_LOG(LogTemp, Error, TEXT("Inv_BuildMenu: HorizontalBox_Buildings is not bound! Make sure the widget has a Horizontal Box named 'HorizontalBox_Buildings'."));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Inv_BuildMenu: Initialized successfully!"));
	}
}

