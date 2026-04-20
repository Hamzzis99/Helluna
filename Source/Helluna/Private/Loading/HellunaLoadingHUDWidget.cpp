// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingHUDWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Loading/HellunaLoadingHUDWidget.h"
#include "Loading/HellunaPartySlotWidget.h"
#include "Helluna.h"

void UHellunaLoadingHUDWidget::InitializeHUD(const TArray<FString>& InExpectedIds,
                                             const FString& InLocalPlayerId,
                                             int32 InPartyId)
{
	ExpectedPlayerIds = InExpectedIds;
	LocalPlayerId = InLocalPlayerId;
	PartyId = InPartyId;
	bInPhase2 = false;
	MyProgress = 0.f;

	PlayerIdToSlot.Empty(ExpectedPlayerIds.Num());
	for (const FString& Id : ExpectedPlayerIds)
	{
		const bool bIsMe = (!LocalPlayerId.IsEmpty() && Id == LocalPlayerId);
		UHellunaPartySlotWidget* PartySlot = SpawnPartySlot(Id, bIsMe);
		if (PartySlot)
		{
			PartySlot->InitSlot(Id, bIsMe);
			PlayerIdToSlot.Add(Id, PartySlot);
		}
	}
}

void UHellunaLoadingHUDWidget::UpdateMyProgress(float Normalized01)
{
	MyProgress = FMath::Clamp(Normalized01, 0.f, 1.f);
}

void UHellunaLoadingHUDWidget::TransitionToPhase2()
{
	if (bInPhase2)
	{
		return;
	}
	bInPhase2 = true;
	PlayTransitionAnimation();
}

void UHellunaLoadingHUDWidget::UpdateSlot(const FString& PlayerId, bool bReady)
{
	if (TObjectPtr<UHellunaPartySlotWidget>* SlotPtr = PlayerIdToSlot.Find(PlayerId))
	{
		if (UHellunaPartySlotWidget* PartySlot = SlotPtr->Get())
		{
			PartySlot->SetReady(bReady);
		}
	}
}
