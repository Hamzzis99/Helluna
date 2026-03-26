// Source/Helluna/Public/Downed/HellunaReviveWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaReviveWidget.generated.h"

class UImage;
class UTextBlock;
class UProgressBar;

/**
 * 다운된 플레이어 머리 위 3D 부활 위젯
 * WidgetComponent(Screen Space)에 부착되어 모든 클라이언트에게 표시
 *
 * 표시 상태:
 *   - 대기: "F: 부활" 프롬프트 + 출혈 잔여시간
 *   - 부활 중: 프로그레스 바 채움 + "부활 중..." 텍스트
 *   - 완료: 녹색 전환 → 숨김
 *
 * PuzzleInteractWidget 패턴을 Revive 전용으로 특화
 */
UCLASS()
class HELLUNA_API UHellunaReviveWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- BindWidgetOptional (WBP에서 배치) ----

	/** F키 아이콘 배경 이미지 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeyBgImage;

	/** F키 텍스트 ("F") */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	/** 액션 텍스트 ("부활" / "부활 중...") */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	/** 출혈 잔여시간 텍스트 ("42s") */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BleedoutTimerText;

	/** 프로그레스 바 배경 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BarBgImage;

	/** 프로그레스 바 채움 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BarFillImage;

	/** 물 차오르기 이미지 (F키 아이콘 내 오버레이) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> WaterFillImage;

	// ---- API ----

	/** 부활 프로그레스 설정 (0.0 ~ 1.0) */
	UFUNCTION(BlueprintCallable, Category = "Downed|Revive")
	void SetReviveProgress(float InProgress);

	/** 출혈 잔여시간 표시 (초) */
	UFUNCTION(BlueprintCallable, Category = "Downed|Revive")
	void SetBleedoutTime(float InTimeRemaining);

	/** 부활 완료 표시 (녹색 전환) */
	UFUNCTION(BlueprintCallable, Category = "Downed|Revive")
	void ShowCompleted();

	/** 기본 상태로 리셋 */
	UFUNCTION(BlueprintCallable, Category = "Downed|Revive")
	void ResetState();

private:
	/** 물 차오르기: WaterFillImage의 RenderTransform Y 오프셋으로 구현 */
	void UpdateWaterFill(float InProgress);

	/** 프로그레스 바 채움: BarFillImage ScaleX */
	void UpdateBarFill(float InProgress);
};
