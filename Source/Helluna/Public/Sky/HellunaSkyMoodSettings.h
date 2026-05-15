// Capstone Project Helluna — 하늘 무드 데이터 액터 (per-map, runtime)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sky/HellunaSkyMoodTypes.h"
#include "HellunaSkyMoodSettings.generated.h"

/**
 * Sky Mood Settings — Day/Night 무드 데이터 보관 전용 액터.
 *
 * 사용법:
 *  1. 맵에 이 액터를 1개 배치 (위치 무관, hidden)
 *  2. 디테일 패널에서 DayMood / NightMood 값 조정
 *  3. 런타임에서 GameState 가 BeginPlay 시 캐싱하여 ApplyVisualPhaseAlpha 가 참조
 *
 * 디자인 의도:
 *  - SkyPreview (에디터 전용 미리보기)와 분리
 *  - per-map 무드 차별화 (각 맵 다른 액터/값)
 *  - 데이터만 보관, 시각/물리 영향 0 (hidden + tick off)
 */
UCLASS(meta = (DisplayName = "Sky Mood Settings (하늘 무드 설정)"))
class HELLUNA_API AHellunaSkyMoodSettings : public AActor
{
    GENERATED_BODY()

public:
    AHellunaSkyMoodSettings();

    /** 낮 무드 — NightVisualAlpha=0.0일 때 적용될 모든 룩 값 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Mood",
        meta = (DisplayName = "낮 무드 (Day Mood)"))
    FHellunaSkyMoodPreset DayMood;

    /** 밤 무드 — NightVisualAlpha=1.0일 때 적용될 모든 룩 값 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Mood",
        meta = (DisplayName = "밤 무드 (Night Mood)"))
    FHellunaSkyMoodPreset NightMood;
};
