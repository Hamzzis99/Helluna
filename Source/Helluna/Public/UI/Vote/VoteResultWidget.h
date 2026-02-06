// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    UI/Vote/VoteResultWidget.h
 * @brief   투표 결과 메시지 위젯
 *
 * @details 투표 종료 후 통과/부결 메시지를 표시합니다.
 *          BP에서 메시지 텍스트를 자유롭게 설정할 수 있습니다.
 *          일정 시간 후 자동으로 사라집니다.
 *
 * @usage   1. 이 클래스를 상속받는 WBP_VoteResultWidget 생성
 *          2. BP Details에서 VotePassedMessage / VoteFailedMessage 설정
 *          3. 디자이너에서 Text_ResultMessage (TextBlock) 배치
 *          4. VoteWidget의 Vote Result Widget Class에 지정
 *
 * @author  [작성자]
 * @date    2026-02-06
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VoteResultWidget.generated.h"

class UTextBlock;

/**
 * @brief   투표 결과 메시지 위젯
 * @details 투표 통과/부결 시 메시지를 표시하고, 일정 시간 후 자동 숨김.
 *          BP에서 메시지 텍스트와 표시 시간을 설정할 수 있습니다.
 */
UCLASS()
class HELLUNA_API UVoteResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ========================================================================
	// BP에서 설정할 메시지
	// ========================================================================

	/** 투표 통과 시 표시할 메시지 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote|Result", meta = (DisplayName = "Vote Passed Message (투표 통과 메시지)"))
	FText VotePassedMessage = FText::FromString(TEXT("잠시 후 다음 맵으로 넘어갑니다!"));

	/** 투표 부결 시 표시할 메시지 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote|Result", meta = (DisplayName = "Vote Failed Message (투표 부결 메시지)"))
	FText VoteFailedMessage = FText::FromString(TEXT("투표가 부결되었습니다."));

	/** 결과 메시지 표시 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote|Result", meta = (DisplayName = "Display Duration (표시 시간)", ClampMin = "1.0", ClampMax = "10.0"))
	float DisplayDuration = 3.0f;

	// ========================================================================
	// 외부 호출 함수
	// ========================================================================

	/**
	 * @brief   결과 메시지 표시
	 * @param   bPassed - true: 통과 메시지, false: 부결 메시지
	 */
	UFUNCTION(BlueprintCallable, Category = "Vote|Result")
	void ShowResult(bool bPassed);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ========================================================================
	// UI 요소 (BP에서 바인딩)
	// ========================================================================

	/** 결과 메시지 텍스트 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Result Message Text (결과 메시지)"))
	TObjectPtr<UTextBlock> Text_ResultMessage;

private:
	/** 자동 숨김 타이머 */
	FTimerHandle HideTimerHandle;

	/** 타이머 콜백 - 위젯 제거 */
	void OnHideTimerExpired();
};
