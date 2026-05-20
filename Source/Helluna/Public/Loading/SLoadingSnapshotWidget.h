// ════════════════════════════════════════════════════════════════════════════════
// SLoadingSnapshotWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// §13 §3.2.4 — B구간 MoviePlayer 배경 Slate 위젯.
// LobbyController가 캡처한 LoadingSnapshotTexture(UTexture2D)를 풀스크린으로 표시.
//
// 📌 작성자: Gihyeon (§13 v2.1)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateBrush.h"
#include "UObject/GCObject.h"

class UTexture2D;

// FGCObject 상속: FSlateBrush 는 ResourceObject 를 GC 로부터 보호하지 않으므로,
// 위젯이 살아있는 동안 스냅샷 텍스처가 수거되지 않도록 직접 참조를 잡아줘야 한다.
// (그렇지 않으면 로딩 HUD 페이드아웃 도중 텍스처가 GC 되어 SImage::OnPaint 에서 댕글링 크래시)
class HELLUNA_API SLoadingSnapshotWidget : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SLoadingSnapshotWidget)
		: _SnapshotTexture(nullptr)
	{}
		SLATE_ARGUMENT(UTexture2D*, SnapshotTexture)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SLoadingSnapshotWidget"); }
	//~ End FGCObject interface

private:
	FSlateBrush SnapshotBrush;

	/** SnapshotBrush 가 가리키는 텍스처. FSlateBrush 는 GC 참조를 잡지 않으므로 여기서 직접 보유. */
	TObjectPtr<UTexture2D> SnapshotTexture = nullptr;
};
