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
	// 인자로 받은 텍스처를 멤버로 보관 → AddReferencedObjects 에서 GC 참조 유지.
	SnapshotTexture = InArgs._SnapshotTexture;

	if (SnapshotTexture)
	{
		SnapshotBrush.SetResourceObject(SnapshotTexture);
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

void SLoadingSnapshotWidget::AddReferencedObjects(FReferenceCollector& Collector)
{
	// 위젯이 살아있는 동안 스냅샷 텍스처를 GC 로부터 보호.
	// 위젯이 파괴되면 더 이상 페인트되지 않으므로 자연스럽게 참조 해제 → 텍스처 수거 허용.
	Collector.AddReferencedObject(SnapshotTexture);
}
