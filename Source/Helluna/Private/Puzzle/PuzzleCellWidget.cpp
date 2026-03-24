// Source/Helluna/Private/Puzzle/PuzzleCellWidget.cpp

#include "Puzzle/PuzzleCellWidget.h"
#include "Components/Image.h"
#include "TimerManager.h"

void UPuzzleCellWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SelectBurstTimerHandle);
		World->GetTimerManager().ClearTimer(RotateFlashTimerHandle);
	}
	Super::NativeDestruct();
}

void UPuzzleCellWidget::SetPipeTexture(UTexture2D* Texture, float RotationAngle)
{
	if (!PipeImage)
	{
		return;
	}

	if (Texture)
	{
		PipeImage->SetBrushResourceObject(Texture);
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
	BgImage->SetBrushResourceObject(Texture);
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

	// 플래시 진행 중이면 Tint 스킵 (Flash 끝나면 RefreshGrid에서 재호출)
	if (RotateFlashProgress > 0.f && RotateFlashProgress < 1.0f)
	{
		return;
	}

	static const FLinearColor ConnectedColor(0.0f, 0.8f, 1.0f, 1.0f);
	static const FLinearColor DefaultColor(1.0f, 1.0f, 1.0f, 1.0f);
	PipeImage->SetColorAndOpacity(bConnected ? ConnectedColor : DefaultColor);
}

// ============================================================================
// 이동 이펙트 — 링이 퍼지며 페이드아웃
// ============================================================================

void UPuzzleCellWidget::PlaySelectBurst(UTexture2D* BurstTexture)
{
	if (!EffectImage)
	{
		return;
	}

	SelectBurstProgress = 0.f;

	if (BurstTexture)
	{
		EffectImage->SetBrushResourceObject(BurstTexture);
	}
	EffectImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	EffectImage->SetRenderOpacity(0.8f);
	EffectImage->SetRenderScale(FVector2D(0.3f, 0.3f));
	EffectImage->SetColorAndOpacity(FLinearColor(0.0f, 0.9f, 1.0f, 1.0f));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SelectBurstTimerHandle);
		World->GetTimerManager().SetTimer(
			SelectBurstTimerHandle, this,
			&UPuzzleCellWidget::TickSelectBurst,
			0.016f, true);
	}
}

void UPuzzleCellWidget::TickSelectBurst()
{
	SelectBurstProgress += 0.016f / 0.2f; // 0.2초 동안

	if (SelectBurstProgress >= 1.0f)
	{
		if (EffectImage)
		{
			EffectImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SelectBurstTimerHandle);
		}
		return;
	}

	if (!EffectImage)
	{
		return;
	}

	// 스케일: 0.3 → 1.2 (팍 퍼짐)
	const float Scale = FMath::Lerp(0.3f, 1.2f, SelectBurstProgress);
	EffectImage->SetRenderScale(FVector2D(Scale, Scale));

	// 투명도: 0.8 → 0.0 (빠르게 사라짐)
	const float Opacity = FMath::Lerp(0.8f, 0.0f, SelectBurstProgress);
	EffectImage->SetRenderOpacity(Opacity);
}

// ============================================================================
// 회전 이펙트 — 더 크고 강한 플래시 버스트
// ============================================================================

void UPuzzleCellWidget::PlayRotateFlash(UTexture2D* FlashTexture)
{
	if (!EffectImage)
	{
		return;
	}

	RotateFlashProgress = 0.f;

	if (FlashTexture)
	{
		EffectImage->SetBrushResourceObject(FlashTexture);
	}
	EffectImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	EffectImage->SetRenderOpacity(1.0f);
	EffectImage->SetRenderScale(FVector2D(0.5f, 0.5f));
	EffectImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

	// 파이프 이미지에도 순간 밝기 부스트
	if (PipeImage)
	{
		PipeImage->SetColorAndOpacity(FLinearColor(1.5f, 1.5f, 1.5f, 1.0f));
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RotateFlashTimerHandle);
		World->GetTimerManager().SetTimer(
			RotateFlashTimerHandle, this,
			&UPuzzleCellWidget::TickRotateFlash,
			0.016f, true);
	}
}

void UPuzzleCellWidget::TickRotateFlash()
{
	RotateFlashProgress += 0.016f / 0.3f; // 0.3초 동안

	if (RotateFlashProgress >= 1.0f)
	{
		if (EffectImage)
		{
			EffectImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RotateFlashTimerHandle);
		}
		return;
	}

	if (EffectImage)
	{
		// 스케일: 0.5 → 1.8 (크게 퍼짐!)
		const float Scale = FMath::Lerp(0.5f, 1.8f, RotateFlashProgress);
		EffectImage->SetRenderScale(FVector2D(Scale, Scale));

		// 투명도: 1.0 → 0.0
		const float Opacity = FMath::Lerp(1.0f, 0.0f, RotateFlashProgress);
		EffectImage->SetRenderOpacity(Opacity);
	}

	// 파이프 밝기도 서서히 복원
	if (PipeImage)
	{
		const float PipeBoost = FMath::Lerp(0.5f, 0.0f, RotateFlashProgress);
		PipeImage->SetColorAndOpacity(FLinearColor(
			1.0f + PipeBoost, 1.0f + PipeBoost, 1.0f + PipeBoost, 1.0f));
	}
}
