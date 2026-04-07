// File: Source/Helluna/Private/UI/WorldMap/HellunaWorldMapWidget.cpp

#include "UI/WorldMap/HellunaWorldMapWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Controller/HellunaHeroController.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

// ============================================================================
// NativeConstruct
// ============================================================================
void UHellunaWorldMapWidget::NativeConstruct()
{
    Super::NativeConstruct();

    SetVisibility(ESlateVisibility::Collapsed);
    SetRenderOpacity(0.f);
    SetRenderTranslation(FVector2D(0.f, SlideOffset));
    AnimState = 0;
    bIsOpen = false;
}

// ============================================================================
// NativeDestruct
// ============================================================================
void UHellunaWorldMapWidget::NativeDestruct()
{
    Super::NativeDestruct();
}

// ============================================================================
// OpenMap
// ============================================================================
void UHellunaWorldMapWidget::OpenMap()
{
    UE_LOG(LogTemp, Warning, TEXT("[WorldMap] OpenMap() 호출됨 bIsOpen=%d"), bIsOpen ? 1 : 0);
    if (bIsOpen)
    {
        return;
    }

    SetVisibility(ESlateVisibility::Visible);
    SetRenderOpacity(0.f);
    SetRenderTranslation(FVector2D(0.f, SlideOffset));
    AnimState = 1;
    AnimTime = 0.f;
    bIsOpen = true;

    MapZoom = 1.f;
    if (ZoomContent)
    {
        ZoomContent->SetRenderScale(FVector2D(1.f, 1.f));
    }
    UE_LOG(LogTemp, Warning, TEXT("[WorldMap] Visibility=Visible, AnimState=1, bIsOpen=true"));

    APlayerController* PC = GetOwningPlayer();
    if (PC)
    {
        FInputModeGameAndUI Mode;
        Mode.SetWidgetToFocus(TakeWidget());
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
        PC->bShowMouseCursor = true;
        UE_LOG(LogTemp, Warning, TEXT("[WorldMap] InputMode=GameAndUI, MouseCursor=true"));
    }
}

// ============================================================================
// CloseMap
// ============================================================================
void UHellunaWorldMapWidget::CloseMap()
{
    if (!bIsOpen)
    {
        return;
    }

    AnimState = 3;
    AnimTime = 0.f;
    bIsOpen = false;

    APlayerController* PC = GetOwningPlayer();
    if (PC)
    {
        FInputModeGameOnly Mode;
        PC->SetInputMode(Mode);
        PC->bShowMouseCursor = false;
    }
}

// ============================================================================
// NativeTick
// ============================================================================
void UHellunaWorldMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // ── 애니메이션 (NotifyPanel과 동일 패턴) ──
    if (AnimState != 0)
    {
        AnimTime += InDeltaTime;

        if (AnimState == 1) // 페이드인
        {
            const float Alpha = FMath::Clamp(AnimTime / FadeInDuration, 0.f, 1.f);
            const float Eased = 1.f - FMath::Pow(1.f - Alpha, 3.f); // EaseOutCubic
            SetRenderOpacity(Eased);
            SetRenderTranslation(FVector2D(0.f, SlideOffset * (1.f - Eased)));
            if (Alpha >= 1.f)
            {
                AnimState = 2;
                AnimTime = 0.f;
            }
        }
        else if (AnimState == 3) // 페이드아웃
        {
            const float Alpha = FMath::Clamp(AnimTime / FadeOutDuration, 0.f, 1.f);
            const float Eased = FMath::Pow(Alpha, 2.f); // EaseInQuad
            SetRenderOpacity(1.f - Eased);
            SetRenderTranslation(FVector2D(0.f, -SlideOffset * Eased));
            if (Alpha >= 1.f)
            {
                AnimState = 0;
                SetVisibility(ESlateVisibility::Collapsed);
                SetRenderOpacity(0.f);
                SetRenderTranslation(FVector2D::ZeroVector);
            }
        }
    }

    // ── 맵 열려있을 때만 마커 갱신 ──
    if (bIsOpen && AnimState != 3)
    {
        UpdatePlayerMarkers();
        UpdatePingMarker();
    }
}

// ============================================================================
// NativeOnMouseButtonDown — 클릭 시 핑 생성/삭제
// ============================================================================
FReply UHellunaWorldMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (!bIsOpen) return FReply::Unhandled();

    const float Delta = InMouseEvent.GetWheelDelta();
    MapZoom = FMath::Clamp(MapZoom + Delta * ZoomStep, MinZoom, MaxZoom);

    if (ZoomContent)
    {
        ZoomContent->SetRenderScale(FVector2D(MapZoom, MapZoom));
    }

    UE_LOG(LogTemp, Verbose, TEXT("[WorldMap] Zoom=%.2f"), MapZoom);
    return FReply::Handled();
}

FReply UHellunaWorldMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (!bIsOpen)
    {
        return FReply::Unhandled();
    }

    if (!MapClipPanel)
    {
        return FReply::Unhandled();
    }

    const FGeometry& ClipGeo = MapClipPanel->GetCachedGeometry();
    const FVector2D ScreenPos = InMouseEvent.GetScreenSpacePosition();
    const FVector2D ClipLocal = ClipGeo.AbsoluteToLocal(ScreenPos);
    const FVector2D ClipSize = ClipGeo.GetLocalSize();

    if (ClipLocal.X < 0.f || ClipLocal.X > ClipSize.X || ClipLocal.Y < 0.f || ClipLocal.Y > ClipSize.Y)
    {
        return FReply::Unhandled();
    }

    AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(GetOwningPlayer());
    if (!HeroPC)
    {
        return FReply::Unhandled();
    }

    if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        HeroPC->ClearLocalPing();
        return FReply::Handled();
    }

    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FVector2D Center = ClipSize * 0.5f;
        const FVector2D ZoomLocal = (ClipLocal - Center) / FMath::Max(MapZoom, 0.0001f) + Center;

        const float U = ZoomLocal.X / ClipSize.X;
        const float V = ZoomLocal.Y / ClipSize.Y;

        const FVector WorldLoc(
            MapCenterX + (U - 0.5f) * MapHalfSize * 2.f,
            MapCenterY + (V - 0.5f) * MapHalfSize * 2.f,
            0.f
        );

        UE_LOG(LogTemp, Warning, TEXT("[WorldMap] Click ClipLocal=(%.0f,%.0f) UV=(%.2f,%.2f) World=(%.0f,%.0f) Zoom=%.2f"),
            ClipLocal.X, ClipLocal.Y, U, V, WorldLoc.X, WorldLoc.Y, MapZoom);

        HeroPC->SetLocalPing(WorldLoc);
        return FReply::Handled();
    }

    return FReply::Unhandled();
}

// ============================================================================
// WorldToMapPixel — 월드 좌표 -> 풀맵 위젯 픽셀
// ============================================================================
FVector2D UHellunaWorldMapWidget::WorldToMapPixel(const FVector& WorldLocation) const
{
    const float U = (WorldLocation.X - (MapCenterX - MapHalfSize)) / (MapHalfSize * 2.f);
    const float V = (WorldLocation.Y - (MapCenterY - MapHalfSize)) / (MapHalfSize * 2.f);

    FVector2D Size = MapWidgetSize;
    if (MapClipPanel)
    {
        const FVector2D Cached = MapClipPanel->GetCachedGeometry().GetLocalSize();
        if (Cached.X > 0.f && Cached.Y > 0.f) Size = Cached;
    }
    return FVector2D(U * Size.X, V * Size.Y);
}

// ============================================================================
// MapPixelToWorld — ZoomContent 로컬 픽셀 -> 월드 좌표
// ============================================================================
FVector UHellunaWorldMapWidget::MapPixelToWorld(const FVector2D& MapPixel) const
{
    FVector2D Size = MapWidgetSize;
    if (MapClipPanel)
    {
        const FVector2D Cached = MapClipPanel->GetCachedGeometry().GetLocalSize();
        if (Cached.X > 0.f && Cached.Y > 0.f) Size = Cached;
    }
    const float U = MapPixel.X / Size.X;
    const float V = MapPixel.Y / Size.Y;
    return FVector(
        (MapCenterX - MapHalfSize) + U * MapHalfSize * 2.f,
        (MapCenterY - MapHalfSize) + V * MapHalfSize * 2.f,
        0.f
    );
}

// ============================================================================
// UpdatePlayerMarkers
// ============================================================================
void UHellunaWorldMapWidget::UpdatePlayerMarkers()
{
    APawn* MyPawn = GetOwningPlayerPawn();
    if (!MyPawn || !LocalPlayerMarker)
    {
        return;
    }

    const FVector2D Pixel = WorldToMapPixel(MyPawn->GetActorLocation());
    UCanvasPanelSlot* PlayerSlot = Cast<UCanvasPanelSlot>(LocalPlayerMarker->Slot);
    if (PlayerSlot)
    {
        PlayerSlot->SetPosition(Pixel - FVector2D(8.f, 8.f)); // 아이콘 크기 절반 오프셋
    }

    // TODO: 팀원 마커 갱신 (TeamMarkerCanvas 사용)
}

// ============================================================================
// UpdatePingMarker
// ============================================================================
void UHellunaWorldMapWidget::UpdatePingMarker()
{
    AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(GetOwningPlayer());
    if (!HeroPC || !PingMarker)
    {
        return;
    }

    if (HeroPC->HasLocalPing())
    {
        const FVector2D Pixel = WorldToMapPixel(HeroPC->GetLocalPingLocation());
        UCanvasPanelSlot* PingSlot = Cast<UCanvasPanelSlot>(PingMarker->Slot);
        if (PingSlot)
        {
            PingSlot->SetPosition(Pixel - FVector2D(12.f, 12.f)); // 핑 아이콘 크기 절반
        }
        PingMarker->SetVisibility(ESlateVisibility::HitTestInvisible);

        // 핑까지 거리 표시
        if (PingDistanceText)
        {
            APawn* MyPawn = GetOwningPlayerPawn();
            if (MyPawn)
            {
                const float DistCM = FVector::Dist2D(MyPawn->GetActorLocation(), HeroPC->GetLocalPingLocation());
                const int32 DistM = FMath::RoundToInt(DistCM / 100.f);
                PingDistanceText->SetText(FText::FromString(FString::Printf(TEXT("%dm"), DistM)));
                PingDistanceText->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
        }
    }
    else
    {
        PingMarker->SetVisibility(ESlateVisibility::Collapsed);
        if (PingDistanceText)
        {
            PingDistanceText->SetText(FText::GetEmpty());
            PingDistanceText->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}
