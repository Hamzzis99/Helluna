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

class UTexture2D;

class HELLUNA_API SLoadingSnapshotWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLoadingSnapshotWidget)
		: _SnapshotTexture(nullptr)
	{}
		SLATE_ARGUMENT(UTexture2D*, SnapshotTexture)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FSlateBrush SnapshotBrush;
};
