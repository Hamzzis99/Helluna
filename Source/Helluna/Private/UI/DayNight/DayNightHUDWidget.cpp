// File: Source/Helluna/Private/UI/DayNight/DayNightHUDWidget.cpp

#include "UI/DayNight/DayNightHUDWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Controller/HellunaHeroController.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Player/HellunaPlayerState.h"

// ============================================================================
// NativeConstruct
// ============================================================================
void UDayNightHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 초기 상태: 낮 타이머 표시, 웨이브 정보 흐리게
    if (DayTimerPanel)
    {
        DayTimerPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if (WaveInfoPanel)
    {
        WaveInfoPanel->SetRenderOpacity(0.35f);
    }
    if (WaveLabelText)
    {
        WaveLabelText->SetText(FText::FromString(TEXT("STANDBY")));
    }
    if (WaveCountText)
    {
        WaveCountText->SetText(FText::FromString(TEXT("--")));
    }
    if (WaveSubText)
    {
        WaveSubText->SetText(FText::FromString(TEXT("대기")));
    }
    if (DayTimeUnitText)
    {
        DayTimeUnitText->SetText(FText::FromString(TEXT("밤까지")));
    }

    // 미션 알림 초기 숨김
    if (NotifyPanel)
    {
        NotifyPanel->SetVisibility(ESlateVisibility::Collapsed);
        NotifyPanel->SetRenderOpacity(0.f);
        NotifyPanel->SetRenderTranslation(FVector2D::ZeroVector);
    }
    bWarningShown = false;
    NotifyAnimState = 0;
    NotifyAnimTime = 0.f;

    // 미니맵 초기화
    InitializeMinimap();
}

// ============================================================================
// NativeDestruct
// ============================================================================
void UDayNightHUDWidget::NativeDestruct()
{
    UWorld* World = GetWorld();
    if (World)
    {
        World->GetTimerManager().ClearTimer(NotifyHideTimer);
    }

    CleanupPlayerIcons();
    Super::NativeDestruct();
}

// ============================================================================
// NativeTick
// ============================================================================
void UDayNightHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    AHellunaDefenseGameState* GS = Cast<AHellunaDefenseGameState>(
        UGameplayStatics::GetGameState(this));
    if (!GS)
    {
        return;
    }

    const EDefensePhase CurrentPhase = GS->GetPhase();

    // ── Phase 전환 감지 ─────────────────────────────────────────────────────
    if (CurrentPhase != LastPhase)
    {
        LastPhase = CurrentPhase;
        LastDay = -1;
        LastTimeRemaining = -1;
        LastAliveMonsters = -1;
        LastTotalMonsters = -1;

        if (CurrentPhase == EDefensePhase::Day)
        {
            if (DayTimerPanel)
            {
                DayTimerPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
            if (WaveInfoPanel)
            {
                WaveInfoPanel->SetRenderOpacity(0.35f);
            }
            if (WaveLabelText)
            {
                WaveLabelText->SetText(FText::FromString(TEXT("STANDBY")));
            }
            if (WaveCountText)
            {
                WaveCountText->SetText(FText::FromString(TEXT("--")));
            }
            if (WaveSubText)
            {
                WaveSubText->SetText(FText::FromString(TEXT("대기")));
            }
            CachedTotalDayTime = GS->DayTimeRemaining;

            // 미션 알림: 낮 시작
            ShowNotification(
                TEXT("DAY PHASE"),
                TEXT("자원을 수집하십시오"),
                TEXT("우주선 수리 자원을 탐색하십시오"),
                FLinearColor(0.96f, 0.62f, 0.04f, 1.f)  // 앰버
            );
            bWarningShown = false;
        }
        else // Night
        {
            if (DayTimerPanel)
            {
                DayTimerPanel->SetVisibility(ESlateVisibility::Collapsed);
            }
            if (WaveInfoPanel)
            {
                WaveInfoPanel->SetRenderOpacity(1.0f);
            }
            if (WaveLabelText)
            {
                WaveLabelText->SetText(FText::FromString(TEXT("NIGHT")));
            }
            if (WaveCountText)
            {
                WaveCountText->SetText(FText::FromString(TEXT("0")));
            }
            if (WaveSubText)
            {
                WaveSubText->SetText(FText::FromString(TEXT("/0")));
            }

            // 미션 알림: 밤 시작
            ShowNotification(
                TEXT("NIGHT PHASE"),
                TEXT("적의 공격을 막으십시오"),
                TEXT("새벽까지 생존하십시오"),
                FLinearColor(0.39f, 0.40f, 0.94f, 1.f)  // 인디고
            );
        }
    }

    // ── 모드별 갱신 ─────────────────────────────────────────────────────────
    if (CurrentPhase == EDefensePhase::Day)
    {
        UpdateDayMode(GS);
    }
    else
    {
        UpdateNightMode(GS);
    }

    // ── 미니맵 갱신 ─────────────────────────────────────────────────────────
    UpdateMinimap();

    // ── 미션 알림 애니메이션 ─────────────────────────────────────────────────
    if (NotifyAnimState != 0 && NotifyPanel)
    {
        NotifyAnimTime += InDeltaTime;

        if (NotifyAnimState == 1)
        {
            const float Alpha = FMath::Clamp(NotifyAnimTime / NotifyFadeInDuration, 0.f, 1.f);
            const float Eased = 1.f - FMath::Pow(1.f - Alpha, 3.f);
            NotifyPanel->SetRenderOpacity(Eased);
            NotifyPanel->SetRenderTranslation(FVector2D(0.f, NotifySlideOffset * (1.f - Eased)));
            if (Alpha >= 1.f)
            {
                NotifyAnimState = 2;
                NotifyAnimTime = 0.f;
            }
        }
        else if (NotifyAnimState == 3)
        {
            const float Alpha = FMath::Clamp(NotifyAnimTime / NotifyFadeOutDuration, 0.f, 1.f);
            const float Eased = FMath::Pow(Alpha, 2.f);
            NotifyPanel->SetRenderOpacity(1.f - Eased);
            NotifyPanel->SetRenderTranslation(FVector2D(0.f, -NotifySlideOffset * Eased));
            if (Alpha >= 1.f)
            {
                NotifyAnimState = 0;
                NotifyPanel->SetVisibility(ESlateVisibility::Collapsed);
                NotifyPanel->SetRenderOpacity(0.f);
                NotifyPanel->SetRenderTranslation(FVector2D::ZeroVector);
            }
        }
    }
}

// ============================================================================
// UpdateDayMode
// ============================================================================
void UDayNightHUDWidget::UpdateDayMode(AHellunaDefenseGameState* GS)
{
    if (CachedTotalDayTime <= 0.f && GS->DayTimeRemaining > 0.f)
    {
        CachedTotalDayTime = GS->DayTimeRemaining;
    }

    const int32 TimeRemaining = FMath::CeilToInt(GS->DayTimeRemaining);
    const int32 CurrentDay = GS->CurrentDayForUI;

    if (TimeRemaining != LastTimeRemaining)
    {
        LastTimeRemaining = TimeRemaining;

        const int32 Minutes = TimeRemaining / 60;
        const int32 Seconds = TimeRemaining % 60;

        if (DayTimeText)
        {
            DayTimeText->SetText(FText::FromString(
                FString::Printf(TEXT("%d:%02d"), Minutes, Seconds)));
        }
        if (DayTimerBar && CachedTotalDayTime > 0.f)
        {
            DayTimerBar->SetPercent(GS->DayTimeRemaining / CachedTotalDayTime);
        }

        if (TimeRemaining < 15)
        {
            if (DayLabelText)
            {
                DayLabelText->SetText(FText::FromString(TEXT("WARNING")));
            }
            if (DayTimerBar)
            {
                DayTimerBar->SetFillColorAndOpacity(FLinearColor(1.f, 0.f, 0.f, 1.f));
            }
            if (DayTimeUnitText)
            {
                DayTimeUnitText->SetText(FText::FromString(TEXT("밤이 온다")));
            }

            // 밤 임박 알림 (1회만)
            if (!bWarningShown)
            {
                bWarningShown = true;
                ShowNotification(
                    TEXT("WARNING"),
                    TEXT("밤이 다가오고 있습니다"),
                    TEXT("거점으로 돌아가십시오"),
                    FLinearColor(0.94f, 0.27f, 0.27f, 1.f)  // 레드
                );
            }
        }
        else
        {
            if (DayTimerBar)
            {
                DayTimerBar->SetFillColorAndOpacity(FLinearColor(1.f, 0.75f, 0.f, 1.f));
            }
            if (DayTimeUnitText)
            {
                DayTimeUnitText->SetText(FText::FromString(TEXT("밤까지")));
            }
        }
    }

    if (CurrentDay != LastDay)
    {
        LastDay = CurrentDay;
        if (DayLabelText && TimeRemaining >= 15)
        {
            DayLabelText->SetText(FText::FromString(
                FString::Printf(TEXT("DAY %d"), CurrentDay)));
        }
    }
}

// ============================================================================
// UpdateNightMode
// ============================================================================
void UDayNightHUDWidget::UpdateNightMode(AHellunaDefenseGameState* GS)
{
    const int32 AliveMonsters = GS->GetAliveMonsterCount();
    const int32 TotalMonsters = GS->TotalMonstersThisNight;

    if (AliveMonsters != LastAliveMonsters || TotalMonsters != LastTotalMonsters)
    {
        LastAliveMonsters = AliveMonsters;
        LastTotalMonsters = TotalMonsters;

        if (WaveCountText)
        {
            WaveCountText->SetText(FText::FromString(
                FString::Printf(TEXT("%d"), AliveMonsters)));
        }
        if (WaveSubText)
        {
            WaveSubText->SetText(FText::FromString(
                FString::Printf(TEXT("/%d"), TotalMonsters)));
        }
        if (WaveProgressBar && TotalMonsters > 0)
        {
            WaveProgressBar->SetPercent(
                static_cast<float>(TotalMonsters - AliveMonsters) / static_cast<float>(TotalMonsters));
        }

        if (AliveMonsters == 0 && TotalMonsters > 0)
        {
            if (WaveLabelText)
            {
                WaveLabelText->SetText(FText::FromString(TEXT("CLEAR")));
            }
            if (WaveProgressBar)
            {
                WaveProgressBar->SetPercent(1.0f);
                WaveProgressBar->SetFillColorAndOpacity(FLinearColor(0.f, 1.f, 0.f, 1.f));
            }

            // 생존 알림
            ShowNotification(
                TEXT("SURVIVED"),
                TEXT("밤을 생존했습니다"),
                TEXT("새벽이 밝아옵니다"),
                FLinearColor(0.29f, 0.87f, 0.50f, 1.f)  // 그린
            );
        }
        else if (TotalMonsters > 0)
        {
            if (WaveLabelText)
            {
                WaveLabelText->SetText(FText::FromString(
                    GS->bIsBossNight ? TEXT("BOSS") : TEXT("NIGHT")));
            }
        }
    }
}

// ============================================================================
// 미니맵 — 초기화
// ============================================================================
void UDayNightHUDWidget::InitializeMinimap()
{
    if (!MinimapImage || !MinimapMaterial)
    {
        return;
    }

    // Dynamic Material Instance 생성
    MinimapMID = UMaterialInstanceDynamic::Create(MinimapMaterial, this);
    if (!MinimapMID)
    {
        return;
    }

    // 초기 파라미터 설정
    MinimapMID->SetScalarParameterValue(FName("ZoomScale"), MinimapZoomScale);
    MinimapMID->SetScalarParameterValue(FName("OffsetU"), 0.5f);
    MinimapMID->SetScalarParameterValue(FName("OffsetV"), 0.5f);

    // MinimapImage에 머티리얼 적용
    MinimapImage->SetBrushFromMaterial(MinimapMID);
}

// ============================================================================
// 미니맵 — 매 틱 갱신
// ============================================================================
void UDayNightHUDWidget::UpdateMinimap()
{
    if (!MinimapMID || !MinimapIconCanvas)
    {
        return;
    }

    // ── 로컬 플레이어 위치 기준으로 UV 오프셋 갱신 ──
    APawn* LocalPawn = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!LocalPawn)
    {
        return;
    }

    const FVector2D LocalUV = WorldToMinimapUV(LocalPawn->GetActorLocation());

    // UV 오프셋 = 플레이어 UV - ZoomScale의 절반 (플레이어가 중앙에 오도록)
    const float OffsetU = LocalUV.X - MinimapZoomScale * 0.5f;
    const float OffsetV = LocalUV.Y - MinimapZoomScale * 0.5f;

    MinimapMID->SetScalarParameterValue(FName("OffsetU"), OffsetU);
    MinimapMID->SetScalarParameterValue(FName("OffsetV"), OffsetV);

    // ── 미니맵 위젯 크기 (SizeBox 기준) ──
    float MinimapWidgetSize = 76.f;
    if (MinimapSizeBox)
    {
        const float OverrideW = MinimapSizeBox->GetWidthOverride();
        if (OverrideW > 0.f)
        {
            MinimapWidgetSize = OverrideW;
        }
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    auto UpdateMarker = [&](UImage* Marker, const FVector& WorldLoc, float YawDeg, bool bUseRotation) -> bool
    {
        if (!Marker) return false;
        const FVector2D UV = WorldToMinimapUV(WorldLoc);
        const float LocalX = (UV.X - OffsetU) / MinimapZoomScale;
        const float LocalY = (UV.Y - OffsetV) / MinimapZoomScale;
        if (LocalX < 0.f || LocalX > 1.f || LocalY < 0.f || LocalY > 1.f)
        {
            Marker->SetVisibility(ESlateVisibility::Collapsed);
            return false;
        }
        if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Marker->Slot))
        {
            const FVector2D Sz = S->GetSize();
            const float PosX = LocalX * MinimapWidgetSize - Sz.X * 0.5f;
            const float PosY = LocalY * MinimapWidgetSize - Sz.Y * 0.5f;
            S->SetPosition(FVector2D(PosX, PosY));
        }
        if (bUseRotation)
        {
            Marker->SetRenderTransformAngle(YawDeg + 90.f);
        }
        Marker->SetVisibility(ESlateVisibility::HitTestInvisible);
        return true;
    };

    // ── 본인 마커 (회전 적용) ──
    UpdateMarker(MinimapLocalPlayerMarker, LocalPawn->GetActorLocation(), LocalPawn->GetActorRotation().Yaw, true);

    // ── 다른 플레이어 (PlayerArray 순회) ──
    UImage* TeamMarkers[2] = { MinimapTeamMarker1, MinimapTeamMarker2 };
    int32 TeamIdx = 0;

    if (AGameStateBase* GSBase = World->GetGameState())
    {
        const APlayerController* LocalPCRef = UGameplayStatics::GetPlayerController(this, 0);
        const APlayerState* LocalPS = LocalPCRef ? LocalPCRef->PlayerState : nullptr;

        for (APlayerState* PS : GSBase->PlayerArray)
        {
            if (!PS || PS == LocalPS) continue;
            if (PS->IsSpectator()) continue;

            APawn* Pawn = PS->GetPawn();
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
            if (!Pawn) continue;
            if (TeamIdx >= 2) break;

            UImage* Marker = TeamMarkers[TeamIdx++];
            if (!Marker) continue;

            UpdateMarker(Marker, Pawn->GetActorLocation(), Pawn->GetActorRotation().Yaw, true);

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

    for (int32 i = TeamIdx; i < 2; ++i)
    {
        if (TeamMarkers[i]) TeamMarkers[i]->SetVisibility(ESlateVisibility::Collapsed);
    }

    // ── [Phase 1] 우주선 (BASE) 마커 ──
    bool bBaseVisible = false;
    if (AHellunaDefenseGameState* DefGS = World->GetGameState<AHellunaDefenseGameState>())
    {
        if (AActor* Ship = DefGS->GetSpaceShip())
        {
            bBaseVisible = UpdateMarker(MinimapBaseMarker, Ship->GetActorLocation(), 0.f, false);
        }
    }
    if (!bBaseVisible && MinimapBaseMarker)
    {
        MinimapBaseMarker->SetVisibility(ESlateVisibility::Collapsed);
    }

    // ── 핑 마커 갱신 (PC에서 폴링) ──
    if (MinimapPingMarker)
    {
        AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(UGameplayStatics::GetPlayerController(this, 0));
        if (HeroPC && HeroPC->HasLocalPing())
        {
            UpdateMarker(MinimapPingMarker, HeroPC->GetLocalPingLocation(), 0.f, false);
        }
        else
        {
            MinimapPingMarker->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

// ============================================================================
// 월드 좌표 → 미니맵 UV 변환
// ============================================================================
FVector2D UDayNightHUDWidget::WorldToMinimapUV(const FVector& WorldLocation) const
{
    // 직교 캡처 기준: 맵 중심(MapCenterX, MapCenterY), 반경 MapHalfSize
    const float U = (WorldLocation.X - (MapCenterX - MapHalfSize)) / (MapHalfSize * 2.f);
    const float V = (WorldLocation.Y - (MapCenterY - MapHalfSize)) / (MapHalfSize * 2.f);

    return FVector2D(
        FMath::Clamp(U, 0.f, 1.f),
        FMath::Clamp(V, 0.f, 1.f)
    );
}

// ============================================================================
// 플레이어 아이콘 생성
// ============================================================================
UImage* UDayNightHUDWidget::CreatePlayerIcon(const FLinearColor& Color)
{
    if (!MinimapIconCanvas)
    {
        return nullptr;
    }

    UImage* Icon = NewObject<UImage>(this);
    if (!Icon)
    {
        return nullptr;
    }

    // 단색 사각 아이콘
    Icon->SetColorAndOpacity(Color);
    Icon->SetVisibility(ESlateVisibility::HitTestInvisible);

    MinimapIconCanvas->AddChild(Icon);

    // CanvasPanelSlot 크기 설정
    UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(Icon->Slot);
    if (IconSlot)
    {
        IconSlot->SetSize(FVector2D(PlayerIconSize, PlayerIconSize));
        IconSlot->SetAutoSize(false);
    }

    return Icon;
}

// ============================================================================
// 미션 알림 — 표시
// ============================================================================
void UDayNightHUDWidget::ShowNotification(const FString& PhaseLabel, const FString& Title, const FString& Desc, const FLinearColor& Color)
{
    if (!NotifyPanel)
    {
        return;
    }

    if (NotifyPhaseText)
    {
        NotifyPhaseText->SetText(FText::FromString(PhaseLabel));
        // Alpha 보존하여 RGB만 어둡게
        NotifyPhaseText->SetColorAndOpacity(FSlateColor(FLinearColor(Color.R * 0.7f, Color.G * 0.7f, Color.B * 0.7f, Color.A)));
    }
    if (NotifyTitleText)
    {
        NotifyTitleText->SetText(FText::FromString(Title));
    }
    if (NotifyDescText)
    {
        NotifyDescText->SetText(FText::FromString(Desc));
        // Alpha 보존하여 RGB만 더 어둡게
        NotifyDescText->SetColorAndOpacity(FSlateColor(FLinearColor(Color.R * 0.4f, Color.G * 0.4f, Color.B * 0.4f, Color.A)));
    }

    NotifyPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
    NotifyPanel->SetRenderOpacity(0.f);
    NotifyPanel->SetRenderTranslation(FVector2D(0.f, NotifySlideOffset));
    NotifyAnimState = 1;
    NotifyAnimTime = 0.f;

    // 3초 후 자동 숨김 (페이드인 시간 포함하여 페이드아웃 시작)
    UWorld* World = GetWorld();
    if (World)
    {
        World->GetTimerManager().ClearTimer(NotifyHideTimer);
        World->GetTimerManager().SetTimer(NotifyHideTimer, this,
            &UDayNightHUDWidget::HideNotification, 3.f, false);
    }
}

// ============================================================================
// 미션 알림 — 숨기기
// ============================================================================
void UDayNightHUDWidget::HideNotification()
{
    if (NotifyPanel && NotifyAnimState != 0)
    {
        NotifyAnimState = 3;
        NotifyAnimTime = 0.f;
    }
}

// ============================================================================
// 아이콘 정리
// ============================================================================
void UDayNightHUDWidget::CleanupPlayerIcons()
{
    for (auto& Pair : PlayerIconMap)
    {
        if (Pair.Value)
        {
            Pair.Value->RemoveFromParent();
        }
    }
    PlayerIconMap.Empty();
}
