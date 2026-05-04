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

	// [Fix stretch] SImage가 brush의 ImageSize(1280x720)를 desired size로 가져
	// viewport 일부만 가리는 버그 → DesiredSizeOverride(0,0)로 slot Fill에 따라 풀스크린 stretch.
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SImage)
			.Image(&SnapshotBrush)
			.DesiredSizeOverride(FVector2D::ZeroVector)
		]
	];
}
