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
FReply UHellunaWorldMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (!bIsOpen)
    {
        return FReply::Unhandled();
    }

    const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());

    // 맵 영역 안인지 체크
    if (LocalPos.X < MapWidgetTopLeft.X || LocalPos.X > MapWidgetTopLeft.X + MapWidgetSize.X ||
        LocalPos.Y < MapWidgetTopLeft.Y || LocalPos.Y > MapWidgetTopLeft.Y + MapWidgetSize.Y)
    {
        return FReply::Unhandled();
    }

    AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(GetOwningPlayer());
    if (!HeroPC)
    {
        return FReply::Unhandled();
    }

    // 우클릭 -> 핑 삭제
    if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        HeroPC->ClearLocalPing();
        return FReply::Handled();
    }

    // 좌클릭 -> 핑 생성/이동
    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FVector WorldLoc = MapPixelToWorld(LocalPos);
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
    return FVector2D(
        MapWidgetTopLeft.X + U * MapWidgetSize.X,
        MapWidgetTopLeft.Y + V * MapWidgetSize.Y
    );
}

// ============================================================================
// MapPixelToWorld — 풀맵 위젯 픽셀 -> 월드 좌표
// ============================================================================
FVector UHellunaWorldMapWidget::MapPixelToWorld(const FVector2D& MapPixel) const
{
    const float U = (MapPixel.X - MapWidgetTopLeft.X) / MapWidgetSize.X;
    const float V = (MapPixel.Y - MapWidgetTopLeft.Y) / MapWidgetSize.Y;
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
