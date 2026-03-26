// Source/Helluna/Private/Puzzle/PuzzleInteractWidget.cpp

#include "Puzzle/PuzzleInteractWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UPuzzleInteractWidget::SetProgress(float InProgress)
{
	InProgress = FMath::Clamp(InProgress, 0.f, 1.f);
	UpdateWaterFill(InProgress);
	UpdateBarFill(InProgress);

	// 프로그레스 중일 때 키 아이콘 밝게
	if (KeyBgImage)
	{
		const float Brightness = 1.0f + InProgress * 0.5f;
		KeyBgImage->SetColorAndOpacity(FLinearColor(Brightness, Brightness, Brightness, 1.0f));
	}
}

void UPuzzleInteractWidget::ShowCompleted()
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
		ActionText->SetText(FText::FromString(TEXT("ACTIVATED")));
		ActionText->SetColorAndOpacity(FSlateColor(GreenColor));
	}
}

void UPuzzleInteractWidget::ResetState()
{
	SetProgress(0.f);

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
		ActionText->SetText(FText::FromString(TEXT("HOLD to hack")));
		ActionText->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 0.8f, 1.0f, 0.8f)));
	}
}

void UPuzzleInteractWidget::UpdateWaterFill(float InProgress)
{
	if (!WaterFillImage) { return; }

	// 물 차오르기: 이미지의 RenderTranslation Y를 조절
	// Progress=0 → Y=+전체높이 (안 보임), Progress=1 → Y=0 (꽉 참)
	const float MaxOffset = 36.f; // F키 아이콘 높이
	FWidgetTransform Tr;
	Tr.Translation = FVector2D(0.f, MaxOffset * (1.f - InProgress));
	WaterFillImage->SetRenderTransform(Tr);

	// 투명도도 프로그레스에 따라
	WaterFillImage->SetRenderOpacity(InProgress > 0.01f ? 1.0f : 0.0f);
}

void UPuzzleInteractWidget::UpdateBarFill(float InProgress)
{
	if (!BarFillImage) { return; }

	// 바 채움: ScaleX로 구현
	FWidgetTransform Tr;
	Tr.Scale = FVector2D(InProgress, 1.f);
	BarFillImage->SetRenderTransform(Tr);
	BarFillImage->SetRenderOpacity(InProgress > 0.01f ? 1.0f : 0.0f);
}
