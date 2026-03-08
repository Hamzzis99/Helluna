// File: Source/Helluna/Public/Login/Widget/HellunaLoadingWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLoadingWidget.generated.h"

class UTextBlock;

/**
 * ============================================
 * HellunaLoadingWidget
 * ============================================
 *
 * 전체 화면 로딩 위젯
 * 검은 배경 + 중앙 텍스트로 전환 구간에서 표시
 *
 * ============================================
 * 사용 위치:
 * ============================================
 * - MDF_GameInstance::ShowLoadingScreen() 에서 생성/표시
 * - MDF_GameInstance::HideLoadingScreen() 에서 제거
 *
 * ============================================
 * BP 설정 (WBP_HellunaLoadingWidget):
 * ============================================
 * [Root] CanvasPanel
 *   └ Border (검은 배경, Anchors 0,0~1,1, Stretch)
 *       └ Text_LoadingMessage (중앙 정렬, 흰색)
 *
 * 작성자: Gihyeon
 */
UCLASS()
class HELLUNA_API UHellunaLoadingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 로딩 메시지 변경
	 * @param Message - 표시할 메시지 (예: "서버 접속 중...", "로그인 중...")
	 */
	UFUNCTION(BlueprintCallable, Category = "Loading")
	void SetLoadingMessage(const FString& Message);

protected:
	/** 로딩 메시지 텍스트 (BP에서 동일한 이름으로 바인딩 필수) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_LoadingMessage;
};
