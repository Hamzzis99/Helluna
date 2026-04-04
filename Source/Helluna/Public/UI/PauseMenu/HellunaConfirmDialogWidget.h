// ════════════════════════════════════════════════════════════════════════════════
// File: Source/Helluna/Public/UI/PauseMenu/HellunaConfirmDialogWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 범용 확인 다이얼로그 위젯 — 일시정지 메뉴에서 사용
//
// 기능:
//   - 아이콘 + 제목 + 설명(경고 시 빨간색) + 취소/확인 버튼
//   - 확인 버튼 빨간색 (#FF4060)
//   - 등장 애니메이션: Scale 0.9→1.0 + Opacity 0→1 (0.3초)
//   - OnConfirmed / OnCancelled 델리게이트
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaConfirmDialogWidget.generated.h"

class UButton;
class UTextBlock;
class UImage;
class UWidgetAnimation;

/** 확인/취소 결과 델리게이트 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDialogConfirmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDialogCancelled);

UCLASS()
class HELLUNA_API UHellunaConfirmDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	/**
	 * 다이얼로그 내용 설정
	 * @param InIcon       아이콘 텍스트 (⚠ 또는 ✕)
	 * @param InTitle      제목 텍스트
	 * @param InDescription 설명 텍스트
	 * @param bIsWarning   true면 설명 텍스트를 빨간색으로 표시
	 */
	UFUNCTION(BlueprintCallable, Category = "ConfirmDialog (확인 다이얼로그)")
	void SetDialogContent(const FText& InIcon, const FText& InTitle,
		const FText& InDescription, bool bIsWarning);

	/** 등장 애니메이션 재생 */
	void PlayShowAnimation();

	/** 결과 델리게이트 */
	UPROPERTY(BlueprintAssignable, Category = "ConfirmDialog (확인 다이얼로그)")
	FOnDialogConfirmed OnConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "ConfirmDialog (확인 다이얼로그)")
	FOnDialogCancelled OnCancelled;

protected:
	// ════════════════════════════════════════════════════════════
	// BindWidgetOptional
	// ════════════════════════════════════════════════════════════

	/** 배경 오버레이 (어두운 반투명) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_DialogOverlay;

	/** 아이콘 텍스트 (⚠ 또는 ✕) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Icon;

	/** 제목 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_DialogTitle;

	/** 설명 (경고 시 빨간색) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_DialogDescription;

	/** 취소 버튼 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Cancel;

	/** 확인 버튼 (빨간색) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Confirm;

	/** 취소 버튼 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Cancel;

	/** 확인 버튼 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Confirm;

private:
	UFUNCTION() void OnCancelClicked();
	UFUNCTION() void OnConfirmClicked();

	/** DialogShow 애니메이션 — WBP의 Anim_DialogShow에 자동 바인딩 */
	UPROPERTY(meta = (BindWidgetAnimOptional), Transient)
	TObjectPtr<UWidgetAnimation> Anim_DialogShow;

	static const FLinearColor RedColor; // #FF4060
};
