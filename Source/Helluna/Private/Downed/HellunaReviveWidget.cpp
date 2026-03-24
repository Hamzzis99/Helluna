// Source/Helluna/Private/Downed/HellunaReviveWidget.cpp

#include "Downed/HellunaReviveWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UHellunaReviveWidget::SetReviveProgress(float InProgress)
{
	InProgress = FMath::Clamp(InProgress, 0.f, 1.f);
	UpdateWaterFill(InProgress);
	UpdateBarFill(InProgress);

	// 부활 중일 때 액션 텍스트 변경
	if (ActionText)
	{
		if (InProgress > 0.01f)
		{
			ActionText->SetText(FText::FromString(TEXT("Reviving...")));
		}
		else
		{
			ActionText->SetText(FText::FromString(TEXT("Revive")));
		}
	}

	// 프로그레스 중 키 아이콘 밝기 증가
	if (KeyBgImage)
	{
		const float Brightness = 1.0f + InProgress * 0.5f;
		KeyBgImage->SetColorAndOpacity(FLinearColor(Brightness, Brightness, Brightness, 1.f));
	}
}

void UHellunaReviveWidget::SetBleedoutTime(float InTimeRemaining)
{
	if (!BleedoutTimerText) return;

	const int32 Seconds = FMath::CeilToInt(FMath::Max(0.f, InTimeRemaining));
	BleedoutTimerText->SetText(FText::FromString(FString::Printf(TEXT("%ds"), Seconds)));

	// 10초 이하면 붉은색 경고
	if (InTimeRemaining <= 10.f)
	{
		BleedoutTimerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.3f, 0.3f, 1.f)));
	}
	else
	{
		BleedoutTimerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.9f)));
	}
}

void UHellunaReviveWidget::ShowCompleted()
{
	const FLinearColor GreenColor(0.0f, 1.0f, 0.6f, 1.0f);

	if (KeyBgImage)
	{
		KeyBgImage->SetColorAndOpacity(GreenColor);
	}
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(FSlateColor(GreenColor));
	}
	if (WaterFillImage)
	{
		WaterFillImage->SetColorAndOpacity(FLinearColor(0.0f, 1.0f, 0.5f, 0.3f));
	}
	if (BarFillImage)
	{
		BarFillImage->SetColorAndOpacity(FLinearColor(0.0f, 1.0f, 0.5f, 1.0f));
	}
	if (ActionText)
	{
		ActionText->SetText(FText::FromString(TEXT("Revived!")));
		ActionText->SetColorAndOpacity(FSlateColor(GreenColor));
	}
	if (BleedoutTimerText)
	{
		BleedoutTimerText->SetText(FText::GetEmpty());
	}
}

void UHellunaReviveWidget::ResetState()
{
	SetReviveProgress(0.f);

	const FLinearColor CyanColor(0.0f, 0.85f, 1.0f, 1.0f);

	if (KeyBgImage)
	{
		KeyBgImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f));
	}
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(FSlateColor(CyanColor));
	}
	if (WaterFillImage)
	{
		WaterFillImage->SetColorAndOpacity(FLinearColor(0.0f, 0.8f, 1.0f, 0.25f));
	}
	if (BarFillImage)
	{
		BarFillImage->SetColorAndOpacity(FLinearColor(0.0f, 0.85f, 1.0f, 1.0f));
	}
	if (ActionText)
	{
		ActionText->SetText(FText::FromString(TEXT("Revive")));
		ActionText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 0.8f, 1.0f, 0.8f)));
	}
	if (BleedoutTimerText)
	{
		BleedoutTimerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.9f)));
	}
}

void UHellunaReviveWidget::UpdateWaterFill(float InProgress)
{
	if (!WaterFillImage) return;

	const float MaxOffset = 36.f; // 키 아이콘 높이
	FWidgetTransform Tr;
	Tr.Translation = FVector2D(0.f, MaxOffset * (1.f - InProgress));
	WaterFillImage->SetRenderTransform(Tr);
	WaterFillImage->SetRenderOpacity(InProgress > 0.01f ? 1.0f : 0.0f);
}

void UHellunaReviveWidget::UpdateBarFill(float InProgress)
{
	if (!BarFillImage) return;

	FWidgetTransform Tr;
	Tr.Scale = FVector2D(InProgress, 1.f);
	BarFillImage->SetRenderTransform(Tr);
	BarFillImage->SetRenderOpacity(InProgress > 0.01f ? 1.0f : 0.0f);
}
