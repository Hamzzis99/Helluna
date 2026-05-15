// ════════════════════════════════════════════════════════════════════════════════
// SLoadingSnapshotWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Loading/SLoadingSnapshotWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Engine/Texture2D.h"

void SLoadingSnapshotWidget::Construct(const FArguments& InArgs)
{
	if (InArgs._SnapshotTexture)
	{
		SnapshotBrush.SetResourceObject(InArgs._SnapshotTexture);
		SnapshotBrush.DrawAs = ESlateBrushDrawType::Image;
		// [Fix stretch v2] ImageSize를 매우 크게 강제 → SImage desired size가 viewport보다 크게
		// → 부모 SOverlay slot(Fill)이 viewport 크기로 clip → 풀스크린 stretch.
		// (DesiredSizeOverride(ZeroVector)는 SCompoundWidget chain에서 layout 0,0이 될 수 있어 폐기)
		SnapshotBrush.ImageSize = FVector2D(99999.f, 99999.f);
	}

	// [Fix robust] 검정 배경(layer 0) + 우주선 SImage(layer 1) 둘 다 풀스크린.
	// 우주선 stretch가 어떤 이유로 실패해도 검정이 가려서 MainMap 노출 절대 방지.
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SColorBlock).Color(FLinearColor::Black)
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SImage).Image(&SnapshotBrush)
		]
	];
}
