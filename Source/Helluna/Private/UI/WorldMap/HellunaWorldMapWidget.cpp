// File: Source/Helluna/Private/UI/WorldMap/HellunaWorldMapWidget.cpp

#include "UI/WorldMap/HellunaWorldMapWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Controller/HellunaHeroController.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Character/HellunaHeroCharacter.h"
#include "Player/HellunaPlayerState.h"
#include "Kismet/GameplayStatics.h"

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
    PanOffset = FVector2D::ZeroVector;
    bIsPanning = false;
    if (ZoomContent)
    {
        ZoomContent->SetRenderScale(FVector2D(1.f, 1.f));
        ZoomContent->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        ZoomContent->SetRenderTranslation(FVector2D::ZeroVector);
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

    MapZoom = 1.f;
    PanOffset = FVector2D::ZeroVector;
    bIsPanning = false;
    if (ZoomContent)
    {
        ZoomContent->SetRenderScale(FVector2D(1.f, 1.f));
        ZoomContent->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        ZoomContent->SetRenderTranslation(FVector2D::ZeroVector);
    }

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
FReply UHellunaWorldMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton && bIsPanning)
    {
        bIsPanning = false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Unhandled();
}

FReply UHellunaWorldMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (!bIsPanning || !ZoomContent) return FReply::Unhandled();

    const FVector2D CurMouse = InMouseEvent.GetScreenSpacePosition();
    const FVector2D Delta = CurMouse - PanLastMouse;
    PanLastMouse = CurMouse;

    PanOffset += Delta;
    ZoomContent->SetRenderTranslation(PanOffset);

    return FReply::Handled();
}

FReply UHellunaWorldMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (!bIsOpen || !ZoomContent || !MapClipPanel) return FReply::Unhandled();

    const FGeometry& ClipGeo = MapClipPanel->GetCachedGeometry();
    const FVector2D ClipSize = ClipGeo.GetLocalSize();
    if (ClipSize.X <= 0.f || ClipSize.Y <= 0.f) return FReply::Unhandled();

    const FVector2D MouseLocal = ClipGeo.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
    if (MouseLocal.X < 0.f || MouseLocal.X > ClipSize.X || MouseLocal.Y < 0.f || MouseLocal.Y > ClipSize.Y)
    {
        return FReply::Unhandled();
    }

    const FVector2D Pivot(MouseLocal.X / ClipSize.X, MouseLocal.Y / ClipSize.Y);
    ZoomContent->SetRenderTransformPivot(Pivot);

    const float Delta = InMouseEvent.GetWheelDelta();
    const float OldZoom = MapZoom;
    MapZoom = FMath::Clamp(MapZoom + Delta * ZoomStep, MinZoom, MaxZoom);

    if (MapZoom <= 1.0001f)
    {
        MapZoom = 1.f;
        PanOffset = FVector2D::ZeroVector;
        bIsPanning = false;
        ZoomContent->SetRenderScale(FVector2D(1.f, 1.f));
        ZoomContent->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        ZoomContent->SetRenderTranslation(FVector2D::ZeroVector);
    }
    else
    {
        ZoomContent->SetRenderScale(FVector2D(MapZoom, MapZoom));
    }

    UE_LOG(LogTemp, Verbose, TEXT("[WorldMap] Zoom=%.2f Pivot=(%.2f,%.2f)"), MapZoom, Pivot.X, Pivot.Y);
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
        HeroPC->Server_ClearWorldPing();
        return FReply::Handled();
    }

    if (InMouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
    {
        if (MapZoom <= 1.0001f) return FReply::Handled();
        bIsPanning = true;
        PanLastMouse = InMouseEvent.GetScreenSpacePosition();
        return FReply::Handled().CaptureMouse(TakeWidget());
    }

    if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        FVector2D Pivot(0.5f, 0.5f);
        if (ZoomContent)
        {
            Pivot = ZoomContent->GetRenderTransformPivot();
        }
        const FVector2D PivotPx(Pivot.X * ClipSize.X, Pivot.Y * ClipSize.Y);
        const FVector2D AdjustedLocal = ClipLocal - PanOffset;
        const FVector2D ZoomLocal = (AdjustedLocal - PivotPx) / FMath::Max(MapZoom, 0.0001f) + PivotPx;

        const float U = ZoomLocal.X / ClipSize.X;
        const float V = ZoomLocal.Y / ClipSize.Y;

        const FVector WorldLoc(
            MapCenterX + (U - 0.5f) * MapHalfSize * 2.f,
            MapCenterY + (V - 0.5f) * MapHalfSize * 2.f,
            0.f
        );

        UE_LOG(LogTemp, Warning, TEXT("[WorldMap] Click ClipLocal=(%.0f,%.0f) UV=(%.2f,%.2f) World=(%.0f,%.0f) Zoom=%.2f"),
            ClipLocal.X, ClipLocal.Y, U, V, WorldLoc.X, WorldLoc.Y, MapZoom);

        HeroPC->Server_SetWorldPing(WorldLoc);
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

    // ── 본인 마커 ──
    if (MyPawn && LocalPlayerMarker)
    {
        const FVector2D Px = WorldToMapPixel(MyPawn->GetActorLocation());
        if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(LocalPlayerMarker->Slot))
        {
            const FVector2D Sz = S->GetSize();
            S->SetPosition(Px - Sz * 0.5f);
        }
        LocalPlayerMarker->SetRenderTransformAngle(MyPawn->GetActorRotation().Yaw + 90.f);
    }

    UWorld* World = GetWorld();
    if (!World) return;

    // ── [Phase 1] 우주선 (BASE) 마커 ──
    if (AHellunaDefenseGameState* DefGS = World->GetGameState<AHellunaDefenseGameState>())
    {
        if (AActor* Ship = DefGS->GetSpaceShip())
        {
            const FVector2D ShipPx = WorldToMapPixel(Ship->GetActorLocation());

            if (BaseMarker)
            {
                if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(BaseMarker->Slot))
                {
                    const FVector2D Sz = S->GetSize();
                    S->SetPosition(ShipPx - Sz * 0.5f);
                }
                BaseMarker->SetVisibility(ESlateVisibility::HitTestInvisible);
            }

            if (BaseLabel)
            {
                if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(BaseLabel->Slot))
                {
                    S->SetPosition(ShipPx + FVector2D(-30.f, 18.f));
                }
                BaseLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
        }
        else
        {
            if (BaseMarker) BaseMarker->SetVisibility(ESlateVisibility::Collapsed);
            if (BaseLabel) BaseLabel->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    // ── [Phase 2] 다른 플레이어 마커 ──
    UImage* TeamMarkers[2] = { TeamMarker1, TeamMarker2 };
    int32 TeamIdx = 0;

    if (AGameStateBase* GSBase = World->GetGameState())
    {
        for (APlayerState* PS : GSBase->PlayerArray)
        {
            if (!PS) continue;
            APawn* Pawn = PS->GetPawn();

            // As-A-Client fallback: GetPawn()이 nullptr이면 HeroCharacter 직접 검색
            if (!Pawn)
            {
                TArray<AActor*> Heroes;
                UGameplayStatics::GetAllActorsOfClass(World, AHellunaHeroCharacter::StaticClass(), Heroes);
                for (AActor* H : Heroes)
                {
                    APawn* HeroPawn = Cast<APawn>(H);
                    if (HeroPawn && HeroPawn->GetPlayerState() == PS)
                    {
                        Pawn = HeroPawn;
                        break;
                    }
                }
            }

            if (!Pawn || Pawn == MyPawn) continue;
            if (TeamIdx >= 2) break;

            UImage* Marker = TeamMarkers[TeamIdx++];
            if (!Marker) continue;

            const FVector2D Px = WorldToMapPixel(Pawn->GetActorLocation());
            if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Marker->Slot))
            {
                const FVector2D Sz = S->GetSize();
                S->SetPosition(Px - Sz * 0.5f);
            }
            Marker->SetRenderTransformAngle(Pawn->GetActorRotation().Yaw + 90.f);
            Marker->SetVisibility(ESlateVisibility::HitTestInvisible);

            // [Phase 4] 캐릭터 타입별 색상
            FLinearColor MarkerColor = FLinearColor::White;
            if (AHellunaPlayerState* HPS = Cast<AHellunaPlayerState>(PS))
            {
                switch (HPS->GetSelectedHeroType())
                {
                case EHellunaHeroType::Liam: MarkerColor = FLinearColor(0.38f, 0.65f, 0.98f); break;
                case EHellunaHeroType::Luna: MarkerColor = FLinearColor(0.96f, 0.45f, 0.71f); break;
                case EHellunaHeroType::Lui:  MarkerColor = FLinearColor(0.31f, 1.0f, 0.56f);  break;
                default: break;
                }
            }
            Marker->SetColorAndOpacity(MarkerColor);
        }
    }

    // 안 채워진 팀 마커 숨김
    for (int32 i = TeamIdx; i < 2; ++i)
    {
        if (TeamMarkers[i]) TeamMarkers[i]->SetVisibility(ESlateVisibility::Collapsed);
    }
}

// ============================================================================
// UpdatePingMarker — 본인 + 팀원 (최대 3명) 서버 복제 핑 렌더
// ============================================================================
void UHellunaWorldMapWidget::UpdatePingMarker()
{
    APlayerController* PC = GetOwningPlayer();
    AHellunaPlayerState* LocalPS = PC ? PC->GetPlayerState<AHellunaPlayerState>() : nullptr;

    // ── 본인 핑 (기존 비주얼: PingMarker + 거리 라벨) ──
    if (PingMarker)
    {
        if (LocalPS && LocalPS->HasPing())
        {
            const FVector2D Pixel = WorldToMapPixel(LocalPS->GetPingLocation());
            if (UCanvasPanelSlot* PingSlot = Cast<UCanvasPanelSlot>(PingMarker->Slot))
            {
                const FVector2D PingSize = PingSlot->GetSize();
                PingSlot->SetPosition(Pixel - PingSize * 0.5f);
            }
            PingMarker->SetVisibility(ESlateVisibility::HitTestInvisible);

            if (PingDistanceText)
            {
                if (APawn* MyPawn = GetOwningPlayerPawn())
                {
                    const float DistCM = FVector::Dist2D(MyPawn->GetActorLocation(), LocalPS->GetPingLocation());
                    const int32 DistM = FMath::RoundToInt(DistCM / 100.f);
                    PingDistanceText->SetText(FText::FromString(FString::Printf(TEXT("%dm"), DistM)));
                    PingDistanceText->SetVisibility(ESlateVisibility::HitTestInvisible);

                    if (UCanvasPanelSlot* TextSlot = Cast<UCanvasPanelSlot>(PingDistanceText->Slot))
                    {
                        TextSlot->SetPosition(Pixel + FVector2D(20.f, -10.f));
                    }
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

    // ── 팀원 핑 (PlayerArray 순회, 거리 라벨 없음, HeroType 색상) ──
    UImage* TeamPings[2] = { TeamPing1, TeamPing2 };
    int32 TeamIdx = 0;

    UWorld* World = GetWorld();
    AGameStateBase* GSBase = World ? World->GetGameState() : nullptr;
    if (GSBase)
    {
        for (APlayerState* PS : GSBase->PlayerArray)
        {
            if (!PS || PS == LocalPS) continue;
            AHellunaPlayerState* HPS = Cast<AHellunaPlayerState>(PS);
            if (!HPS || !HPS->HasPing()) continue;
            if (TeamIdx >= 2) break;

            UImage* Marker = TeamPings[TeamIdx++];
            if (!Marker) continue;

            const FVector2D Px = WorldToMapPixel(HPS->GetPingLocation());
            if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Marker->Slot))
            {
                const FVector2D Sz = S->GetSize();
                S->SetPosition(Px - Sz * 0.5f);
            }

            FLinearColor C = FLinearColor::White;
            switch (HPS->GetSelectedHeroType())
            {
            case EHellunaHeroType::Liam: C = FLinearColor(0.38f, 0.65f, 0.98f); break;
            case EHellunaHeroType::Luna: C = FLinearColor(0.96f, 0.45f, 0.71f); break;
            case EHellunaHeroType::Lui:  C = FLinearColor(0.31f, 1.0f, 0.56f);  break;
            default: break;
            }
            Marker->SetColorAndOpacity(C);
            Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
    }

    for (int32 i = TeamIdx; i < 2; ++i)
    {
        if (TeamPings[i]) TeamPings[i]->SetVisibility(ESlateVisibility::Collapsed);
    }
}
