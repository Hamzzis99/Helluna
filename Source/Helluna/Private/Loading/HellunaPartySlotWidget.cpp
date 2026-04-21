// ════════════════════════════════════════════════════════════════════════════════
// HellunaPartySlotWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Loading/HellunaPartySlotWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"

void UHellunaPartySlotWidget::InitSlot(const FString& InPlayerId, bool bInIsMe)
{
	PlayerId = InPlayerId;
	bIsMe = bInIsMe;
	bReady = false;
	OnSlotVisualUpdated(PlayerId, bReady, bIsMe);
}

void UHellunaPartySlotWidget::SetReady(bool bInReady)
{
	if (bReady == bInReady)
	{
		return;
	}
	bReady = bInReady;
	OnSlotVisualUpdated(PlayerId, bReady, bIsMe);
}

void UHellunaPartySlotWidget::OnSlotVisualUpdated_Implementation(const FString& InPlayerId, bool bInReady, bool bInIsMe)
{
	if (Text_Name)
	{
		const FString NameStr = bInIsMe
			? FString::Printf(TEXT("%s (YOU)"), *InPlayerId)
			: InPlayerId;
		Text_Name->SetText(FText::FromString(NameStr));
	}
	if (Text_Status)
	{
		Text_Status->SetText(FText::FromString(bInReady ? TEXT("READY") : TEXT("로딩 중...")));
		const FLinearColor StatusColor = bInReady
			? FLinearColor(0.49f, 0.78f, 1.0f, 1.0f)
			: FLinearColor(0.55f, 0.55f, 0.55f, 1.0f);
		Text_Status->SetColorAndOpacity(FSlateColor(StatusColor));
	}
	if (Image_Dot)
	{
		const FLinearColor DotColor = bInReady
			? FLinearColor(0.49f, 0.78f, 1.0f, 1.0f)
			: FLinearColor(0.40f, 0.40f, 0.40f, 1.0f);
		Image_Dot->SetColorAndOpacity(DotColor);
	}
}
