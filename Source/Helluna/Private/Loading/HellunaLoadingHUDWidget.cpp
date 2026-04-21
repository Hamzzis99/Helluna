// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingHUDWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Loading/HellunaLoadingHUDWidget.h"
#include "Loading/HellunaPartySlotWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/PanelWidget.h"
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

	if (ProgressBar_MyProgress)
	{
		ProgressBar_MyProgress->SetPercent(MyProgress);
	}
	if (Text_MyProgressText)
	{
		const FString PctStr = (MyProgress >= 0.999f)
			? FString(TEXT("READY"))
			: FString::Printf(TEXT("%d%%"), FMath::FloorToInt(MyProgress * 100.f));
		Text_MyProgressText->SetText(FText::FromString(PctStr));
	}
}

void UHellunaLoadingHUDWidget::TransitionToPhase2()
{
	if (bInPhase2)
	{
		return;
	}
	bInPhase2 = true;
	if (Text_Subtitle)
	{
		Text_Subtitle->SetText(FText::FromString(TEXT("파티원 대기 중")));
	}
	PlayTransitionAnimation();
}

UHellunaPartySlotWidget* UHellunaLoadingHUDWidget::SpawnPartySlot_Implementation(const FString& InPlayerId, bool bIsMe)
{
	if (!PartySlotWidgetClass)
	{
		UE_LOG(LogHelluna, Warning, TEXT("[LoadingHUD] PartySlotWidgetClass 미설정 — SpawnPartySlot 폴백 실패 PlayerId=%s"), *InPlayerId);
		return nullptr;
	}
	UHellunaPartySlotWidget* NewSlot = CreateWidget<UHellunaPartySlotWidget>(GetOwningPlayer(), PartySlotWidgetClass);
	if (NewSlot && VBox_PartyList)
	{
		VBox_PartyList->AddChild(NewSlot);
	}
	return NewSlot;
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
