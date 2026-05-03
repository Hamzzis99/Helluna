// Capstone Project Helluna — 하늘 무드 데이터 타입

#pragma once

#include "CoreMinimal.h"
#include "HellunaSkyMoodTypes.generated.h"

/**
 * 하늘 무드 프리셋 — Day 또는 Night 한 페이즈의 모든 룩 값.
 *
 * 사용처:
 *  - AHellunaSkyPreviewActor::DayMood / NightMood (per-map 설정)
 *  - AHellunaDefenseGameState::ApplyVisualPhaseAlpha 가 NightVisualAlpha 기반 Lerp
 *
 * 디폴트는 Helluna 원본 밤 룩 (붉은달 + 오로라). DayMood는 SkyPreview 디테일
 * 패널에서 디자이너가 흰달/별 5.5 등으로 오버라이드.
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FHellunaSkyMoodPreset
{
    GENERATED_BODY()

    // ── 시간/달 위상 ──────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "시간",
        meta = (DisplayName = "Time of Day", ClampMin = "0", ClampMax = "2400"))
    float TimeOfDay = 2200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "시간",
        meta = (DisplayName = "달 위상 (0=신월 / 0.5=반달 / 1=보름)",
                ClampMin = "0.0", ClampMax = "1.0"))
    float MoonPhase = 0.0f;

    // ── UDS BP 라이팅 ─────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "라이팅",
        meta = (DisplayName = "달빛 색상 (UDS Moon Light Color)"))
    FLinearColor MoonLightColor = FLinearColor(0.35f, 0.12f, 0.14f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "라이팅",
        meta = (DisplayName = "달빛 인텐시티", ClampMin = "0.0"))
    float MoonLightIntensity = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "라이팅",
        meta = (DisplayName = "Sky Light 인텐시티", ClampMin = "0.0"))
    float SkyLightIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "라이팅",
        meta = (DisplayName = "Night Brightness", ClampMin = "0.0"))
    float NightBrightness = 0.7f;

    // ── 별/오로라 ─────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "별과오로라",
        meta = (DisplayName = "별 강도", ClampMin = "0.0"))
    float StarsIntensity = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "별과오로라",
        meta = (DisplayName = "오로라 인텐시티", ClampMin = "0.0"))
    float AuroraIntensity = 0.9f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "별과오로라",
        meta = (DisplayName = "Daytime Aurora 인텐시티", ClampMin = "0.0"))
    float DaytimeAuroraIntensity = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "별과오로라",
        meta = (DisplayName = "Render Nebula"))
    bool bRenderNebula = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "별과오로라",
        meta = (DisplayName = "달 텍스처 인텐시티 (Night)", ClampMin = "0.0"))
    float MoonTextureIntensityNight = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "별과오로라",
        meta = (DisplayName = "달 글로우 인텐시티", ClampMin = "0.0"))
    float MoonGlowIntensity = 0.03f;

    // ── Sky_Sphere MID 파라미터 ───────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MID",
        meta = (DisplayName = "달 디스크 색 (MID Moon Color)"))
    FLinearColor MoonDiscColor = FLinearColor(0.9f, 0.18f, 0.12f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MID",
        meta = (DisplayName = "Aurora_Color_1"))
    FLinearColor AuroraColor1 = FLinearColor(0.7f, 0.06f, 0.2f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MID",
        meta = (DisplayName = "Aurora_Color_2"))
    FLinearColor AuroraColor2 = FLinearColor(0.02f, 0.55f, 0.65f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MID",
        meta = (DisplayName = "Aurora_Color_3"))
    FLinearColor AuroraColor3 = FLinearColor(0.15f, 0.06f, 0.4f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MID",
        meta = (DisplayName = "Twinkle Strength", ClampMin = "0.0"))
    float TwinkleStrength = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MID",
        meta = (DisplayName = "Twinkle Floor", ClampMin = "0.0", ClampMax = "1.0"))
    float TwinkleFloor = 0.0f;
};
