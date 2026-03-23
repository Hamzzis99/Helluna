// Source/Helluna/Public/Puzzle/PuzzleCellWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PuzzleCellWidget.generated.h"

class UImage;

/**
 * 퍼즐 단일 셀 위젯
 * C++은 로직만 담당, 비주얼(크기/브러시/레이아웃)은 WBP_PuzzleCell에서 설정
 */
UCLASS()
class HELLUNA_API UPuzzleCellWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// BindWidgetOptional — WBP Designer에서 배치
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BgImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> PipeImage;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SelectionImage;

	/** 파이프 텍스처 + 회전 각도 설정 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle")
	void SetPipeTexture(UTexture2D* Texture, float RotationAngle);

	/** 배경 텍스처 설정 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle")
	void SetBgTexture(UTexture2D* Texture);

	/** 선택 하이라이트 표시/숨김 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle")
	void SetSelected(bool bSelected);

	/** 연결 상태에 따른 Tint 색상 */
	UFUNCTION(BlueprintCallable, Category = "Puzzle")
	void SetConnectedTint(bool bConnected);
};
