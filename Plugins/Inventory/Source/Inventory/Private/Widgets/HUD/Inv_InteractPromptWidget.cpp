// Plugins/Inventory/Source/Inventory/Private/Widgets/HUD/Inv_InteractPromptWidget.cpp

#include "Widgets/HUD/Inv_InteractPromptWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UInv_InteractPromptWidget::SetItemName(const FString& InName)
{
	if (ItemNameText)
	{
		ItemNameText->SetText(FText::FromString(InName));
	}
}

void UInv_InteractPromptWidget::SetActionText(const FString& InAction)
{
	if (ActionText)
	{
		ActionText->SetText(FText::FromString(InAction));
	}
}

void UInv_InteractPromptWidget::SetKeyText(const FString& InKey)
{
	if (KeyText)
	{
		KeyText->SetText(FText::FromString(InKey));
	}
}

void UInv_InteractPromptWidget::ResetState()
{
	const FLinearColor CyanColor(0.0f, 0.85f, 1.0f, 1.0f);

	if (KeyBgImage)
	{
		KeyBgImage->SetColorAndOpacity(FLinearColor::White);
	}
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(FSlateColor(CyanColor));
	}
	if (ItemNameText)
	{
		ItemNameText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	}
	if (ActionText)
	{
		ActionText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 0.8f, 1.0f, 0.8f)));
	}
}
