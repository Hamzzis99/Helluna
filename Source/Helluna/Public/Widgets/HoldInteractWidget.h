// Source/Helluna/Public/Widgets/HoldInteractWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HoldInteractWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 3D 홀드 상호작용 위젯 베이스 클래스
 * WidgetComponent에 부착되어 3D 공간에 Screen Space로 표시됨
 *
 * F키 홀드 프로그레스(물 차오르기 + 바 채움) + 완료 시 녹색 전환.
 * DefaultActionText / CompletedActionText를 BP 디테일에서 Override하여
 * 퍼즐, 보스 조우 등 다양한 상호작용에 재사용.
 */
UCLASS()
class HELLUNA_API UHoldInteractWidget : public UUserWidget
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

	/** 액션 텍스트 ("HOLD to hack" 등) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	/** 하단 프로그레스 바 배경 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BarBgImage;

	/** 하단 프로그레스 바 채움 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BarFillImage;

	// ---- BP에서 수정 가능한 텍스트 ----

	/** 기본 액션 텍스트 (ResetState에서 복원됨). BP 디테일에서 Override 가능. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HoldInteract",
		meta = (DisplayName = "Default Action Text (기본 액션 텍스트)"))
	FText DefaultActionText = FText::FromString(TEXT("HOLD to interact"));

	/** 완료 시 액션 텍스트. BP 디테일에서 Override 가능. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HoldInteract",
		meta = (DisplayName = "Completed Action Text (완료 텍스트)"))
	FText CompletedActionText = FText::FromString(TEXT("ACTIVATED"));

	// ---- 프로그레스 제어 ----

	/** 프로그레스 설정 (0.0 ~ 1.0) */
	UFUNCTION(BlueprintCallable, Category = "HoldInteract")
	void SetProgress(float Progress);

	/** 완료 상태 표시 (녹색 전환 + 텍스트 변경) */
	UFUNCTION(BlueprintCallable, Category = "HoldInteract")
	void ShowCompleted();

	/** 리셋 (기본 상태로 복원) */
	UFUNCTION(BlueprintCallable, Category = "HoldInteract")
	void ResetState();

private:
	/** 물 차오르기: WaterFillImage의 RenderTransform Y 오프셋으로 구현 */
	void UpdateWaterFill(float InProgress);

	/** 프로그레스 바 채움 */
	void UpdateBarFill(float InProgress);
};
