// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Layout/Margin.h"
#include "BossHealthBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UBorder;

/**
 * 보스 전용 화면 상단 HP 바.
 *
 * WBP 에서 다음 이름으로 위젯이 존재해야 자동 바인딩됨 (BindWidgetOptional):
 *   - UProgressBar "ProgressBar_Health"   주 HP 바
 *   - UProgressBar "ProgressBar_Delayed"  잔상 (엘든링 스타일)
 *   - UTextBlock   "Text_BossName"        보스 이름
 *   - UBorder      "Border_Frame"         프레임 (페이즈2 우측 확장의 negative-padding 대상)
 *
 * 페이즈2 진입 흐름 — HandleBossEnterPhase2():
 *   1) HP fill 단계 (Phase2FillDuration, 선형) — Display% 0→풀
 *   2) 가로 확장 단계 (Phase2WidthExpandDuration, 선형) — Border padding.Right 를 음수로
 *      lerp 하여 content area (= 바) 만 우측으로 확장. Border 프레임 시각 크기는 그대로.
 *
 * BossActor 는 ExposeOnSpawn 으로 외부에서 주입.
 */
UCLASS()
class HELLUNA_API UBossHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 타겟 보스 액터 — HellunaHealthComponent 를 찾아 HP% 읽음. CreateWidget 에서 자동 주입. */
	UPROPERTY(BlueprintReadWrite, Category = "Boss", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<AActor> BossActor = nullptr;

	// =========================================================
	// 보간 / 잔상 바
	// =========================================================

	/** 주 HP 바가 타겟을 따라가는 속도. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Animation",
		meta = (DisplayName = "주 HP 바 보간 속도", ClampMin = "1.0", ClampMax = "40.0"))
	float MainInterpSpeed = 14.f;

	/** 잔상(Delayed) 바가 타겟을 따라가는 속도 — 낮을수록 천천히. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Animation",
		meta = (DisplayName = "잔상 바 보간 속도", ClampMin = "0.3", ClampMax = "10.0"))
	float DelayedInterpSpeed = 2.2f;

	/** 피격 직후 잔상 바가 잠깐 멈추는 시간. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Animation",
		meta = (DisplayName = "잔상 바 지연 시작 (초)", ClampMin = "0.0", ClampMax = "2.0"))
	float DelayedHoldDuration = 0.6f;

	// =========================================================
	// 페이즈별 색
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "1페이즈 주 바 색"))
	FLinearColor Phase1MainColor = FLinearColor(0.88f, 0.10f, 0.18f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "1페이즈 잔상 바 색"))
	FLinearColor Phase1DelayedColor = FLinearColor(0.95f, 0.55f, 0.12f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "2페이즈 주 바 색"))
	FLinearColor Phase2MainColor = FLinearColor(0.72f, 0.10f, 0.92f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "2페이즈 잔상 바 색"))
	FLinearColor Phase2DelayedColor = FLinearColor(1.f, 0.45f, 0.85f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "페이즈 색 전환 시간 (초)", ClampMin = "0.1", ClampMax = "3.0"))
	float PhaseColorBlendDuration = 0.8f;

	// =========================================================
	// 피격 색 플래시 (스케일 펄스 없음 — 바 가로 흔들림 방지)
	// =========================================================

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Flash",
		meta = (DisplayName = "피격 플래시 지속 시간 (초)", ClampMin = "0.05", ClampMax = "1.5"))
	float DamageFlashDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Flash",
		meta = (DisplayName = "플래시 하이라이트 색"))
	FLinearColor FlashHighlightColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Flash",
		meta = (DisplayName = "플래시 색 섞임 최대", ClampMin = "0.0", ClampMax = "1.0"))
	float FlashColorMixMax = 0.55f;

	// =========================================================
	// 페이즈2 fill + 가로 확장
	// =========================================================

	/** 페이즈2 진입 시 HP 0→풀 채우는 시간 (선형). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Phase2",
		meta = (DisplayName = "1단계: HP fill 시간 (초)", ClampMin = "0.3", ClampMax = "8.0"))
	float Phase2FillDuration = 1.6f;

	/** fill 완료 후 가로 확장 시간 (선형). fill+expand ≤ 보스 무적 시간(2.5s) 권장. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Phase2",
		meta = (DisplayName = "2단계: 가로 확장 시간 (초)", ClampMin = "0.0", ClampMax = "3.0"))
	float Phase2WidthExpandDuration = 0.5f;

	/** Border content area 가 페이즈2 풀 확장 시 우측으로 추가될 디자인 픽셀 양.
	 *  Border 의 layout 폭(=SizeBox 폭)은 그대로 유지되고, padding.Right 를 음수로 보내서 content 만 확장. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Phase2",
		meta = (DisplayName = "가로 확장량 (디자인 px)", ClampMin = "0.0", ClampMax = "3000.0"))
	float Phase2BarWidthAddDesignPx = 700.f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// =========================================================
	// BindWidgetOptional — WBP 의 위젯 이름과 일치하면 자동 바인딩
	// =========================================================

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_Health = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_Delayed = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_BossName = nullptr;

	/** 페이즈2 우측 확장의 핵심 — Padding.Right 를 음수로 lerp. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Border_Frame = nullptr;

private:
	// --- HP 표시 상태 ---
	float DisplayMainPercent = 1.f;
	float DisplayDelayedPercent = 1.f;
	double LastDamageTime = 0.0;
	float PreviousTargetPercent = 1.f;

	// --- 페이즈 / 색 전환 ---
	int32 DisplayedPhase = 1;
	float PhaseColorBlendAlpha = 0.f;

	// --- 플래시 (색만, 스케일 X) ---
	float FlashTimeRemaining = 0.f;

	// --- 페이즈2 fill + 확장 시퀀스 ---
	bool bPhase2FillAnimationActive = false;
	double Phase2FillStartTime = 0.0;
	/** 0=확장 없음, 1=Phase2BarWidthAddDesignPx 만큼 확장. */
	float Phase2WidthAlpha = 0.f;

	// --- Border padding 캐시 ---
	FMargin CachedBorderPadding = FMargin(0.f);
	bool bCachedBorderPadding = false;

	// --- 콜백 / 헬퍼 ---
	UFUNCTION()
	void HandleBossEnterPhase2();

	float GetBossHealthNormalized() const;
	bool IsBossInPhase2() const;
};
