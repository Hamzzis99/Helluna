// Capstone Project Helluna — 에디터 전용 밤/낮 미리보기 액터

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

/**
 * 에디터 전용 — 밤/낮 하늘 미리보기 전환 액터
 *
 * 사용법:
 *  1. MainMap에 이 액터를 배치 (위치 무관)
 *  2. 디테일 패널에서 "밤/낮 전환" 드롭다운 변경
 *  3. 뷰포트가 즉시 밤 또는 낮으로 전환됨
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
	// 미리보기 전환
	// ─────────────────────────────────────────────────────────

	UPROPERTY(EditAnywhere, Category = "하늘 미리보기",
		meta = (DisplayName = "밤/낮 전환 (Sky Preview)"))
	ESkyPreviewMode PreviewMode = ESkyPreviewMode::Day;

	// ─────────────────────────────────────────────────────────
	// 밤 시간 미세 조절 (Time of Day 0~2400)
	// ─────────────────────────────────────────────────────────

	/** 밤 미리보기 시 UDS Time of Day (0=자정, 2200=밤10시) */
	UPROPERTY(EditAnywhere, Category = "하늘 미리보기",
		meta = (DisplayName = "밤 시간 (Night Time)", ClampMin = "1800", ClampMax = "2400",
			EditCondition = "PreviewMode == ESkyPreviewMode::Night"))
	float NightPreviewTime = 2200.f;

	/** 낮 미리보기 시 UDS Time of Day (800=아침, 1200=정오, 1600=오후) */
	UPROPERTY(EditAnywhere, Category = "하늘 미리보기",
		meta = (DisplayName = "낮 시간 (Day Time)", ClampMin = "600", ClampMax = "1800",
			EditCondition = "PreviewMode == ESkyPreviewMode::Day"))
	float DayPreviewTime = 1200.f;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	AActor* FindUDSActor() const;
	void ApplyPreview();
};
