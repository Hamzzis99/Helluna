// Phase 21: Revive Progress HUD Widget
#include "Downed/HellunaReviveProgressWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Helluna.h"

void UHellunaReviveProgressWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitWidget();
}

void UHellunaReviveProgressWidget::InitWidget()
{
	if (RingImage && RingMaterial && !RingMID)
	{
		RingMID = UMaterialInstanceDynamic::Create(RingMaterial, this);
		if (RingMID)
		{
			RingImage->SetBrushFromMaterial(RingMID);
			RingMID->SetScalarParameterValue(TEXT("Progress"), 0.f);
			UE_LOG(LogHelluna, Log, TEXT("[ReviveProgress] MID 생성 완료"));
		}
	}

	if (TimerText)
	{
		TimerText->SetText(FText::FromString(TEXT("0.0")));
	}

	if (ActionText)
	{
		ActionText->SetText(FText::FromString(TEXT("부활 중...")));
	}

	if (TargetNameText)
	{
		TargetNameText->SetText(FText::GetEmpty());
	}
}

void UHellunaReviveProgressWidget::SetProgress(float InProgress)
{
	if (RingMID)
	{
		RingMID->SetScalarParameterValue(TEXT("Progress"), FMath::Clamp(InProgress, 0.f, 1.f));
	}
}

void UHellunaReviveProgressWidget::SetRemainingTime(float Seconds)
{
	if (TimerText)
	{
		FString TimeStr = FString::Printf(TEXT("%.1f"), FMath::Max(Seconds, 0.f));
		TimerText->SetText(FText::FromString(TimeStr));
	}
}

void UHellunaReviveProgressWidget::SetTargetName(const FString& Name)
{
	if (TargetNameText)
	{
		TargetNameText->SetText(FText::FromString(Name));
	}
}
