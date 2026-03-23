// Source/Helluna/Private/Puzzle/PuzzleCellWidget.cpp

#include "Puzzle/PuzzleCellWidget.h"
#include "Components/Image.h"

void UPuzzleCellWidget::SetPipeTexture(UTexture2D* Texture, float RotationAngle)
{
	if (!PipeImage)
	{
		return;
	}

	if (Texture)
	{
		PipeImage->SetBrushFromTexture(Texture);
		PipeImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	else
	{
		PipeImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	PipeImage->SetRenderTransformAngle(RotationAngle);
}

void UPuzzleCellWidget::SetBgTexture(UTexture2D* Texture)
{
	if (!BgImage || !Texture)
	{
		return;
	}
	BgImage->SetBrushFromTexture(Texture);
}

void UPuzzleCellWidget::SetSelected(bool bSelected)
{
	if (!SelectionImage)
	{
		return;
	}
	SelectionImage->SetVisibility(
		bSelected ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed
	);
}

void UPuzzleCellWidget::SetConnectedTint(bool bConnected)
{
	if (!PipeImage)
	{
		return;
	}
	static const FLinearColor ConnectedColor(0.0f, 0.8f, 1.0f, 1.0f);
	static const FLinearColor DefaultColor(1.0f, 1.0f, 1.0f, 1.0f);
	PipeImage->SetColorAndOpacity(bConnected ? ConnectedColor : DefaultColor);
}
