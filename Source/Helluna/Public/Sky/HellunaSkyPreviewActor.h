// Capstone Project Helluna — 에디터 전용 밤/낮 + 날씨 미리보기 액터

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaSkyPreviewActor.generated.h"

UENUM(BlueprintType)
enum class ESkyPreviewMode : uint8
{
	Day   UMETA(DisplayName = "낮 (Day)"),
	Night UMETA(DisplayName = "밤 (Night)")
};

UENUM(BlueprintType)
enum class ESkyWeatherPreset : uint8
{
	None          UMETA(DisplayName = "없음 (None)"),
	ClearSkies    UMETA(DisplayName = "맑음 (Clear Skies)"),
	PartlyCloudy  UMETA(DisplayName = "구름 조금 (Partly Cloudy)"),
	Cloudy        UMETA(DisplayName = "흐림 (Cloudy)"),
	Overcast      UMETA(DisplayName = "잔뜩 흐림 (Overcast)"),
	Foggy         UMETA(DisplayName = "안개 (Foggy)"),
	RainLight     UMETA(DisplayName = "이슬비 (Rain Light)"),
	Rain          UMETA(DisplayName = "비 (Rain)"),
	Thunderstorm  UMETA(DisplayName = "뇌우 (Thunderstorm)"),
	SnowLight     UMETA(DisplayName = "눈 조금 (Snow Light)"),
	Snow          UMETA(DisplayName = "눈 (Snow)"),
	Blizzard      UMETA(DisplayName = "눈보라 (Blizzard)")
};

/**
 * 에디터 전용 — 밤/낮 + 날씨 미리보기 전환 액터
 *
 * 사용법:
 *  1. MainMap에 이 액터를 배치 (위치 무관)
 *  2. 디테일 패널에서 "밤/낮 전환" 변경 → 시간 + 날씨가 동시에 전환
 *  3. 밤 날씨 / 낮 날씨를 각각 개별 설정 가능
 *  4. 뷰포트가 즉시 전환됨
 *
 * 런타임에는 아무 동작도 하지 않음 (bIsEditorOnlyActor = true)
 */
UCLASS(meta = (DisplayName = "Sky Preview (하늘 미리보기)"))
class HELLUNA_API AHellunaSkyPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AHellunaSkyPreviewActor();

	// ─────────────────────────────────────────────────────────
	// 밤/낮 전환
	// ─────────────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "하늘 미리보기|시간",
		meta = (DisplayName = "밤/낮 전환 (Sky Preview)"))
	ESkyPreviewMode PreviewMode = ESkyPreviewMode::Day;

	/** 밤 미리보기 시 UDS Time of Day (0=자정, 2200=밤10시) */
	UPROPERTY(EditAnywhere, Category = "하늘 미리보기|시간",
		meta = (DisplayName = "밤 시간 (Night Time)", ClampMin = "1800", ClampMax = "2400",
			EditCondition = "PreviewMode == ESkyPreviewMode::Night"))
	float NightPreviewTime = 2200.f;

	/** 낮 미리보기 시 UDS Time of Day (800=아침, 1200=정오, 1600=오후) */
	UPROPERTY(EditAnywhere, Category = "하늘 미리보기|시간",
		meta = (DisplayName = "낮 시간 (Day Time)", ClampMin = "600", ClampMax = "1800",
			EditCondition = "PreviewMode == ESkyPreviewMode::Day"))
	float DayPreviewTime = 1200.f;

	// ─────────────────────────────────────────────────────────
	// 날씨 프리셋 (밤/낮 개별)
	// ─────────────────────────────────────────────────────────

	/** 낮 날씨 프리셋 — Day 모드에서 적용 */
	UPROPERTY(EditAnywhere, Category = "하늘 미리보기|날씨",
		meta = (DisplayName = "낮 날씨 (Day Weather)"))
	ESkyWeatherPreset DayWeatherPreset = ESkyWeatherPreset::ClearSkies;

	/** 밤 날씨 프리셋 — Night 모드에서 적용 */
	UPROPERTY(EditAnywhere, Category = "하늘 미리보기|날씨",
		meta = (DisplayName = "밤 날씨 (Night Weather)"))
	ESkyWeatherPreset NightWeatherPreset = ESkyWeatherPreset::None;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	AActor* FindUDSActor() const;
	AActor* FindUDWActor() const;

	/** 시간 + 구름 토글 적용 */
	void ApplyTimePreview();

	/** 현재 모드에 맞는 날씨 적용 */
	void ApplyWeatherPreview();

	/** 전체 미리보기 적용 (시간 + 날씨) */
	void ApplyFullPreview();

	/** UDS 프로퍼티 리플렉션 헬퍼 */
	static void SetUDSFloat(AActor* UDS, const TCHAR* PropName, float Value);
	static void SetUDSBool(AActor* UDS, const TCHAR* PropName, bool Value);

	/** 프리셋 enum → UDW Weather Preset 에셋 경로 매핑 */
	static FString GetWeatherPresetPath(ESkyWeatherPreset Preset);
};
