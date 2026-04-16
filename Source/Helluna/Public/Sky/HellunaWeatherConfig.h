// Capstone Project Helluna — 날씨 구성 DataAsset (Day/Night Pool + Forced, Enum 기반)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sky/HellunaSkyPreviewActor.h"  // ESkyWeatherPreset 재사용
#include "HellunaWeatherConfig.generated.h"

/**
 * 맵별 날씨 프리셋 구성.
 *
 * 동작:
 *  - *Forced 가 None 외의 값이면 해당 Pool 무시하고 항상 그 프리셋 사용.
 *  - *Forced 가 None이면 *Pool 배열에서 랜덤 선택.
 *  - Pool도 비어있으면 날씨 전환 생략.
 *
 * 설계 선택: 클래스 피커 대신 `ESkyWeatherPreset` enum 드롭다운 사용.
 *  - SkyPreview 액터와 동일한 패턴 (한글 이름, 에디터 UX 단순)
 *  - Texture/잘못된 BP 실수 선택 원천 차단
 *  - 런타임에서 enum → UDS Weather Preset 경로 매핑 → StaticLoadObject
 *
 * 참조: GameState::WeatherConfig (TSoftObjectPtr) → 이 DataAsset.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Weather Config (날씨 설정)"))
class HELLUNA_API UHellunaWeatherConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 낮 랜덤 풀. DayForced가 None일 때 사용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Day",
        meta = (DisplayName = "Day Pool (낮 랜덤 풀)"))
    TArray<ESkyWeatherPreset> DayPool;

    /** 낮 고정 날씨. None이 아니면 DayPool 무시. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Day",
        meta = (DisplayName = "Day Forced (낮 고정 — None이면 Pool 사용)"))
    ESkyWeatherPreset DayForced = ESkyWeatherPreset::None;

    /** 밤 랜덤 풀. NightForced가 None일 때 사용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Night",
        meta = (DisplayName = "Night Pool (밤 랜덤 풀)"))
    TArray<ESkyWeatherPreset> NightPool;

    /** 밤 고정 날씨. None이 아니면 NightPool 무시. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather|Night",
        meta = (DisplayName = "Night Forced (밤 고정 — None이면 Pool 사용)"))
    ESkyWeatherPreset NightForced = ESkyWeatherPreset::None;

    /** 날씨 전환 시간 (초). 0이면 GameState의 기본값 사용. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather",
        meta = (DisplayName = "Transition Time (전환 시간)", ClampMin = "0.0"))
    float TransitionTime = 0.f;
};
