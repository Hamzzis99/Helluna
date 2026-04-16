// Capstone Project Helluna — 날씨 구성 DataAsset (Day/Night Pool + Forced)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HellunaWeatherConfig.generated.h"

/**
 * 맵별 날씨 프리셋 구성.
 *
 * 동작:
 *  - *Forced 가 설정되면 해당 Pool 무시하고 항상 그 프리셋 사용.
 *  - *Forced 가 비어있으면 *Pool 배열에서 랜덤 선택.
 *  - Pool도 비어있으면 날씨 전환 생략 (기본값 유지).
 *
 * 참조 구조: GameState::WeatherConfig (TSoftObjectPtr) → 이 DataAsset.
 * SkyPreview 에디터 미리보기와 독립 — 런타임 동작만 담당.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Weather Config (날씨 설정)"))
class HELLUNA_API UHellunaWeatherConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 낮 랜덤 풀 (UDS Weather Preset 에셋들). DayForced가 비어있을 때 사용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Day",
        meta = (DisplayName = "Day Pool (낮 랜덤 풀)"))
    TArray<TSoftObjectPtr<UObject>> DayPool;

    /** 낮 고정 날씨. 설정되면 DayPool 무시. 디버깅/이벤트용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Day",
        meta = (DisplayName = "Day Forced (낮 고정 — 비어있으면 Pool 사용)"))
    TSoftObjectPtr<UObject> DayForced;

    /** 밤 랜덤 풀. NightForced가 비어있을 때 사용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Night",
        meta = (DisplayName = "Night Pool (밤 랜덤 풀)"))
    TArray<TSoftObjectPtr<UObject>> NightPool;

    /** 밤 고정 날씨. 설정되면 NightPool 무시. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Night",
        meta = (DisplayName = "Night Forced (밤 고정 — 비어있으면 Pool 사용)"))
    TSoftObjectPtr<UObject> NightForced;

    /** 날씨 전환 시간 (초). 0이면 GameState의 기본값 사용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather",
        meta = (DisplayName = "Transition Time (전환 시간)", ClampMin = "0.0"))
    float TransitionTime = 0.f;
};
