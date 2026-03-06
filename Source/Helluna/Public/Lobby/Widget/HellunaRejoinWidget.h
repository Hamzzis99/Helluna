// ════════════════════════════════════════════════════════════════════════════════
// HellunaRejoinWidget.h — [Phase 14e] 게임 재참가 위젯
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 복귀 시 진행 중인 게임이 감지되면 표시
// 재참가 / 포기 선택지를 제공
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaRejoinWidget.generated.h"

class UTextBlock;
class UButton;

UCLASS(Blueprintable)
class HELLUNA_API UHellunaRejoinWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 재참가 정보 설정 (서버 포트) */
	void SetRejoinInfo(int32 Port);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	// ════════════════════════════════════════════════════════════════
	// BindWidget
	// ════════════════════════════════════════════════════════════════

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Message;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Rejoin;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Abandon;

private:
	UFUNCTION()
	void OnRejoinClicked();

	UFUNCTION()
	void OnAbandonClicked();

	int32 CachedPort = 0;
};
