// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    UI/Vote/VoteResultWidget.cpp
 * @brief   UVoteResultWidget 구현
 *
 * @author  [작성자]
 * @date    2026-02-06
 */

#include "UI/Vote/VoteResultWidget.h"
#include "Components/TextBlock.h"
#include "TimerManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHellunaVote, Log, All);

// ============================================================================
// 라이프사이클
// ============================================================================

void UVoteResultWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteResultWidget] NativeConstruct"));
}

void UVoteResultWidget::NativeDestruct()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteResultWidget] NativeDestruct"));

	// 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimerHandle);
	}

	Super::NativeDestruct();
}

// ============================================================================
// 결과 표시
// ============================================================================

void UVoteResultWidget::ShowResult(bool bPassed, float Duration)
{
	// Duration이 0 이하이면 기본값 사용
	const float ActualDuration = (Duration > 0.0f) ? Duration : DisplayDuration;

	// 메시지 설정
	const FText& Message = bPassed ? VotePassedMessage : VoteFailedMessage;

	if (Text_ResultMessage)
	{
		Text_ResultMessage->SetText(Message);
	}

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteResultWidget] ShowResult - %s: %s (%.1f초 후 숨김)"),
		bPassed ? TEXT("통과") : TEXT("부결"),
		*Message.ToString(),
		ActualDuration);

	// 위젯 표시
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// 일정 시간 후 자동 제거
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			HideTimerHandle,
			this,
			&UVoteResultWidget::OnHideTimerExpired,
			ActualDuration,
			false
		);
	}
}

void UVoteResultWidget::OnHideTimerExpired()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteResultWidget] 표시 시간 종료 - 위젯 제거"));
	RemoveFromParent();
}
