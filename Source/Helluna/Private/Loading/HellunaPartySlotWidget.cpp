// ════════════════════════════════════════════════════════════════════════════════
// HellunaPartySlotWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Loading/HellunaPartySlotWidget.h"

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
