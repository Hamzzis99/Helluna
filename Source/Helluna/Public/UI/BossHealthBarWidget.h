// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BossHealthBarWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * [BossHealthBarV1] 보스 전용 화면 상단 HP 바.
 *
 * WBP_BossHealthBar는 이 클래스를 부모로 reparent. 다음 위젯 이름을 디자이너에서 맞춰야 함:
 *   - UProgressBar "ProgressBar_Health" (주 바 — 빨리 따라감)
 *   - UProgressBar "ProgressBar_Delayed" (잔상 바 — 천천히 따라감, 엘든링 스타일, 선택)
 *   - UTextBlock "Text_BossName" (보스 이름, 선택)
 *
 * BossActor는 CreateWidget 직후 외부에서 주입 (ExposeOnSpawn).
 */
UCLASS()
class HELLUNA_API UBossHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 타겟 보스 액터 — HellunaHealthComponent를 찾아 HP% 읽음. Create Widget에서 자동 주입. */
	UPROPERTY(BlueprintReadWrite, Category = "Boss", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<AActor> BossActor = nullptr;

	/** 주 HP 바가 타겟을 따라가는 속도 (높을수록 즉시 반응). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Animation",
		meta = (DisplayName = "주 HP 바 보간 속도", ClampMin = "1.0", ClampMax = "40.0"))
	float MainInterpSpeed = 14.f;

	/** 잔상(Delayed) 바가 타겟을 따라가는 속도 (낮을수록 천천히 줄어듦). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Animation",
		meta = (DisplayName = "잔상 바 보간 속도", ClampMin = "0.3", ClampMax = "10.0"))
	float DelayedInterpSpeed = 2.2f;

	/** 피격 시 잔상 바가 잠깐 멈췄다 따라오도록 하는 지연 시간 (초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Animation",
		meta = (DisplayName = "잔상 바 지연 시작 (초)", ClampMin = "0.0", ClampMax = "2.0"))
	float DelayedHoldDuration = 0.6f;

	// =========================================================
	// [HPBarPhaseColor] 페이즈별 색상 + 맥박 애니메이션
	// =========================================================

	/** 1페이즈 주 바 색 — 크림슨 레드. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "1페이즈 주 바 색"))
	FLinearColor Phase1MainColor = FLinearColor(0.88f, 0.10f, 0.18f, 1.f);

	/** 1페이즈 잔상 바 색 — 주황-레드. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "1페이즈 잔상 바 색"))
	FLinearColor Phase1DelayedColor = FLinearColor(0.95f, 0.55f, 0.12f, 1.f);

	/** 2페이즈 주 바 색 — 딥 퍼플 블러드. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "2페이즈 주 바 색"))
	FLinearColor Phase2MainColor = FLinearColor(0.72f, 0.10f, 0.92f, 1.f);

	/** 2페이즈 잔상 바 색 — 핑크-마젠타. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "2페이즈 잔상 바 색"))
	FLinearColor Phase2DelayedColor = FLinearColor(1.f, 0.45f, 0.85f, 1.f);

	/** 데미지 받을 때 플래시 지속 시간 (초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Flash",
		meta = (DisplayName = "피격 플래시 지속 시간 (초)", ClampMin = "0.05", ClampMax = "1.5"))
	float DamageFlashDuration = 0.35f;

	/** 플래시 동안 바가 살짝 커지는 스케일 진폭. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Flash",
		meta = (DisplayName = "플래시 스케일 진폭", ClampMin = "0.0", ClampMax = "0.3"))
	float FlashScaleAmplitude = 0.08f;

	/** 플래시 색 — 기존 바 색에 섞일 하이라이트 색 (기본 흰색). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Flash",
		meta = (DisplayName = "플래시 하이라이트 색"))
	FLinearColor FlashHighlightColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** 플래시 시 하이라이트가 기존 색에 섞이는 최대 비율 (0~1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Flash",
		meta = (DisplayName = "플래시 색 섞임 최대", ClampMin = "0.0", ClampMax = "1.0"))
	float FlashColorMixMax = 0.55f;

	/** 2페이즈 진입 시 FillColor가 크림슨→퍼플로 보간되는 시간 (초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "BossHP|Color",
		meta = (DisplayName = "페이즈 색 전환 시간 (초)", ClampMin = "0.1", ClampMax = "3.0"))
	float PhaseColorBlendDuration = 0.8f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// =========================================================
	// BindWidgetOptional — 디자이너에서 이름이 일치하면 자동 바인딩
	// =========================================================

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_Health = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar_Delayed = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_BossName = nullptr;

private:
	/** 화면에 현재 그려지고 있는 주 HP%. 매 Tick에서 Target에 보간. */
	float DisplayMainPercent = 1.f;

	/** 화면에 현재 그려지고 있는 잔상 HP%. 주 바보다 천천히 따라감. */
	float DisplayDelayedPercent = 1.f;

	/** 마지막으로 HP 감소가 있었던 순간 — 잔상 바 hold 타이밍 기준. */
	double LastDamageTime = 0.0;

	/** 이전 프레임에서 읽었던 타겟 HP%. 감소 감지용. */
	float PreviousTargetPercent = 1.f;

	/** 현재 표시 중인 페이즈(1 or 2). 2페이즈 진입 델리게이트 수신 시 2로 설정. */
	int32 DisplayedPhase = 1;

	/** 페이즈 색 전환 보간 진행도 (0→1, Phase1→Phase2). */
	float PhaseColorBlendAlpha = 0.f;

	/** 플래시 남은 시간 — 피격 순간 DamageFlashDuration으로 세팅, 매 Tick 감소. */
	float FlashTimeRemaining = 0.f;

	/** 플래시 sin 위상 누적 시간 (짧은 한 펄스 표현용). */
	float FlashElapsed = 0.f;

	/** 보스 OnBossEnterPhase2 수신용 핸들러 (BossActor 바인딩). */
	UFUNCTION()
	void HandleBossEnterPhase2();

	/** 보스에서 현재 HP% (0~1) 읽기. 보스가 없으면 0. */
	float GetBossHealthNormalized() const;

	/** 보스의 bInPhase2 상태 조회 (서버/클라 둘 다 복제된 값 참조). */
	bool IsBossInPhase2() const;
};
