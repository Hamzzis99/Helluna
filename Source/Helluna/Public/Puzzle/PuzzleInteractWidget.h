// Source/Helluna/Public/Puzzle/PuzzleInteractWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PuzzleInteractWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 퍼즐 큐브/보스 3D 상호작용 위젯
 * WidgetComponent에 부착되어 3D 공간에 표시됨
 *
 * [보스전 로드맵]
 * 보스 버전에서도 이 위젯을 그대로 사용.
 * PuzzleShieldComponent의 WidgetComponent에 부착하면 됨.
 * "HOLD to hack" → "HOLD to disable shield" 텍스트만 변경.
 */
UCLASS()
class HELLUNA_API UPuzzleInteractWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---- BindWidgetOptional (WBP에서 배치) ----

	/** F키 아이콘 배경 이미지 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeyBgImage;

	/** F키 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	/** 원형 프로그레스 이미지 (Material Instance로 Fill Amount 제어) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ProgressCircleImage;

	/** 물 차오르기 이미지 (하단부터 올라오는 오버레이) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> WaterFillImage;

	/** 액션 텍스트 ("HOLD to hack") */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	/** 하단 프로그레스 바 배경 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BarBgImage;

	/** 하단 프로그레스 바 채움 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BarFillImage;

	// ---- 프로그레스 제어 ----

	/** 프로그레스 설정 (0.0 ~ 1.0) */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Interact")
	void SetProgress(float Progress);

	/** 완료 상태 표시 (녹색 전환 + 플래시) */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Interact")
	void ShowCompleted();

	/** 리셋 (기본 상태) */
	UFUNCTION(BlueprintCallable, Category = "Puzzle|Interact")
	void ResetState();

private:
	/** 물 차오르기: WaterFillImage의 RenderTransform Y 오프셋으로 구현 */
	void UpdateWaterFill(float Progress);

	/** 프로그레스 바 채움 */
	void UpdateBarFill(float Progress);
};
