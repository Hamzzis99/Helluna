// ════════════════════════════════════════════════════════════════════════════════
// SLoadingSnapshotWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Loading/SLoadingSnapshotWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture2D.h"

void SLoadingSnapshotWidget::Construct(const FArguments& InArgs)
{
	if (InArgs._SnapshotTexture)
	{
		SnapshotBrush.SetResourceObject(InArgs._SnapshotTexture);
		SnapshotBrush.DrawAs = ESlateBrushDrawType::Image;
		SnapshotBrush.ImageSize = FVector2D(
			InArgs._SnapshotTexture->GetSizeX(),
			InArgs._SnapshotTexture->GetSizeY());
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SImage).Image(&SnapshotBrush)
		]
	];
}
