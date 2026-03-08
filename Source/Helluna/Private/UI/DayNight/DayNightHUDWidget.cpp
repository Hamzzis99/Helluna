// Capstone Project Helluna

#include "UI/DayNight/DayNightHUDWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/HellunaDefenseGameState.h"

// ============================================================================
// NativeConstruct
// ============================================================================
void UDayNightHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 초기 상태: 낮 아이콘 적용
    ApplyPhaseIcon(EDefensePhase::Day);
}

// ============================================================================
// NativeTick — GameState 폴링, 값이 바뀔 때만 텍스트/아이콘 갱신
// ============================================================================
void UDayNightHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    AHellunaDefenseGameState* GS = Cast<AHellunaDefenseGameState>(
        UGameplayStatics::GetGameState(this));
    if (!GS) return;

    const EDefensePhase CurrentPhase = GS->GetPhase();
    const int32 CurrentDay           = GS->CurrentDayForUI;
    const int32 TimeRemaining        = FMath::CeilToInt(GS->DayTimeRemaining);
    const int32 AliveMonsters        = GS->GetAliveMonsterCount();
    const int32 TotalMonsters        = GS->TotalMonstersThisNight;

    const bool bPhaseChanged = (CurrentPhase != LastPhase);

    // ── Phase 전환 ──────────────────────────────────────────────────────────
    if (bPhaseChanged)
    {
        LastPhase = CurrentPhase;           // 여기서 갱신
        ApplyPhaseIcon(CurrentPhase);

        // 캐시 초기화 → 아래 StatusText/DayCount 즉시 갱신 유도
        LastDay           = -1;
        LastTimeRemaining = -1;
        LastAliveMonsters = -1;
        LastTotalMonsters = -1;
    }

    // ── 일차 텍스트 (낮/밤 항상 표시, Phase 전환 시도 갱신) ─────────────────
    if (CurrentDay != LastDay)
    {
        LastDay = CurrentDay;
        // \n으로 2줄: "1일차\n낮" / "1일차\n밤"
        const FString PhaseLabel = (CurrentPhase == EDefensePhase::Day)
            ? TEXT(" 낮") : TEXT(" 밤");
        const FString DayStr = FString::Printf(TEXT("%d일차\n%s"), CurrentDay, *PhaseLabel);
        if (DayCountText)
            DayCountText->SetText(FText::FromString(DayStr));
    }

    // ── StatusText ──────────────────────────────────────────────────────────
    if (CurrentPhase == EDefensePhase::Day)
    {
        if (TimeRemaining != LastTimeRemaining)
        {
            LastTimeRemaining = TimeRemaining;
            if (StatusText)
                StatusText->SetText(
                    FText::FromString(FString::Printf(TEXT("밤까지 %d초"), TimeRemaining)));
        }
    }
    else
    {
        // 0/0 상태(소환 전)이면 표시하지 않음
        if (TotalMonsters > 0 &&
            (AliveMonsters != LastAliveMonsters || TotalMonsters != LastTotalMonsters))
        {
            LastAliveMonsters = AliveMonsters;
            LastTotalMonsters = TotalMonsters;
            if (StatusText)
            {
                const FString Text = GS->bIsBossNight
                    ? FString::Printf(TEXT("보스몬스터: %d / %d"), AliveMonsters, TotalMonsters)
                    : FString::Printf(TEXT("남은 몬스터: %d / %d"), AliveMonsters, TotalMonsters);
                StatusText->SetText(FText::FromString(Text));
            }
        }
    }
}

// ============================================================================
// ApplyPhaseIcon — Phase에 따라 DayNightIconImage 텍스처 교체
// ============================================================================
void UDayNightHUDWidget::ApplyPhaseIcon(EDefensePhase NewPhase)
{
    if (!DayNightIconImage) return;

    UTexture2D* TargetTexture = (NewPhase == EDefensePhase::Day)
        ? DayIconTexture
        : NightIconTexture;

    if (TargetTexture)
    {
        DayNightIconImage->SetBrushFromTexture(TargetTexture);
        DayNightIconImage->SetBrushTintColor(FLinearColor::White);
        DayNightIconImage->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f));
        DayNightIconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    else
    {
        DayNightIconImage->SetVisibility(ESlateVisibility::Collapsed);
    }
}
