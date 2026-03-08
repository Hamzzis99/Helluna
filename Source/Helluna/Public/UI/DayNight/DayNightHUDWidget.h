// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "DayNightHUDWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 낮/밤 상태 HUD 위젯
 *
 * [낮 표시]
 *  - DayNightIconImage  : 낮 아이콘
 *  - DayCountText       : "3일차" (낮/밤 항상 표시)
 *  - StatusText         : "밤까지 42초"
 *
 * [밤 표시]
 *  - DayNightIconImage  : 밤 아이콘 (낮 아이콘과 같은 위치, 텍스처만 교체)
 *  - DayCountText       : "3일차" (낮/밤 항상 표시)
 *  - StatusText         : "남은 몬스터: 7 / 15"
 */
UCLASS()
class HELLUNA_API UDayNightHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 낮 아이콘 텍스처 (에디터에서 지정) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DayNight|Icon",
        meta = (DisplayName = "낮 아이콘 텍스처"))
    TObjectPtr<UTexture2D> DayIconTexture = nullptr;

    /** 밤 아이콘 텍스처 (에디터에서 지정) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DayNight|Icon",
        meta = (DisplayName = "밤 아이콘 텍스처"))
    TObjectPtr<UTexture2D> NightIconTexture = nullptr;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    // ── BP에서 이름으로 바인딩되는 위젯들 ────────────────────────────────

    /** 낮/밤 아이콘 이미지 (하나로 통합 — Phase에 따라 텍스처 교체) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UImage> DayNightIconImage = nullptr;

    /** 몇 일차 텍스트 (낮/밤 항상 표시) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> DayCountText = nullptr;

    /** 낮: "밤까지 N초" / 밤: "남은 몬스터: N / M" (하나로 통합) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    TObjectPtr<UTextBlock> StatusText = nullptr;

private:
    EDefensePhase LastPhase   = EDefensePhase::Day;
    int32 LastDay             = -1;
    int32 LastTimeRemaining   = -1;
    int32 LastAliveMonsters   = -1;
    int32 LastTotalMonsters   = -1;

    /** Phase 전환 시 아이콘 텍스처 교체 */
    void ApplyPhaseIcon(EDefensePhase NewPhase);
};
