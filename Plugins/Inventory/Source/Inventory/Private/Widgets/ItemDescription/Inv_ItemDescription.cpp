// Gihyeon's Inventory Project


#include "Widgets/ItemDescription/Inv_ItemDescription.h"

#include "Components/SizeBox.h"

FVector2D UInv_ItemDescription::GetBoxSize() const
{
	return SizeBox->GetDesiredSize();
}

void UInv_ItemDescription::SetVisibility(ESlateVisibility InVisibility)
{
	for (UWidget* Child : GetChildren())
	{
		if (Child) Child->SetVisibility(ESlateVisibility::Collapsed);
	}
	
	Super::SetVisibility(InVisibility);
}
