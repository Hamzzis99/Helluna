// Fill out your copyright notice in the Description page of Project Settings.

/**
 * @file    UI/Vote/VoteWidget.cpp
 * @brief   UVoteWidget 구현
 *
 * @details 투표 UI 위젯의 C++ 구현 파일입니다.
 *          VoteManager 델리게이트 바인딩 및 UI 업데이트를 담당합니다.
 *
 * @author  [작성자]
 * @date    2026-02-05
 */

#include "UI/Vote/VoteWidget.h"
#include "Utils/Vote/VoteManagerComponent.h"
#include "Utils/Vote/VoteTypes.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"

// ============================================================================
// 라이프사이클
// ============================================================================

void UVoteWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteWidget] NativeConstruct"));

	// 초기에는 숨김
	SetVisibility(ESlateVisibility::Collapsed);
}

void UVoteWidget::NativeDestruct()
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteWidget] NativeDestruct"));

	// 델리게이트 언바인드
	if (CachedVoteManager.IsValid())
	{
		CachedVoteManager->OnVoteStarted.RemoveDynamic(this, &UVoteWidget::OnVoteStarted);
		CachedVoteManager->OnVoteUpdated.RemoveDynamic(this, &UVoteWidget::OnVoteUpdated);
		CachedVoteManager->OnVoteEnded.RemoveDynamic(this, &UVoteWidget::OnVoteEnded);
	}

	Super::NativeDestruct();
}

// ============================================================================
// 초기화
// ============================================================================

void UVoteWidget::InitializeVoteWidget(UVoteManagerComponent* VoteManager)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteWidget] InitializeVoteWidget 진입"));

	if (!VoteManager)
	{
		UE_LOG(LogHellunaVote, Warning, TEXT("[VoteWidget] InitializeVoteWidget 실패 - VoteManager가 null"));
		return;
	}

	CachedVoteManager = VoteManager;

	// 델리게이트 바인딩
	VoteManager->OnVoteStarted.AddDynamic(this, &UVoteWidget::OnVoteStarted);
	VoteManager->OnVoteUpdated.AddDynamic(this, &UVoteWidget::OnVoteUpdated);
	VoteManager->OnVoteEnded.AddDynamic(this, &UVoteWidget::OnVoteEnded);

	UE_LOG(LogHellunaVote, Log, TEXT("[VoteWidget] VoteManager 델리게이트 바인딩 완료"));
}

// ============================================================================
// UI 업데이트 함수
// ============================================================================

void UVoteWidget::OnVoteStarted_Implementation(const FVoteRequest& Request)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteWidget] OnVoteStarted - %s"), *Request.GetVoteTypeName());

	CurrentRequest = Request;
	CachedTimeout = Request.Timeout;

	// 제목 설정
	if (Text_VoteTitle)
	{
		Text_VoteTitle->SetText(FText::FromString(GetVoteTitleText(Request.VoteType)));
	}

	// 대상 설정
	if (Text_VoteTarget)
	{
		Text_VoteTarget->SetText(FText::FromString(GetVoteTargetText(Request)));
	}

	// 위젯 표시
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UVoteWidget::OnVoteUpdated_Implementation(const FVoteStatus& Status)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteWidget] OnVoteUpdated - %s"), *Status.ToString());

	// 찬성 카운트
	if (Text_AgreeCount)
	{
		Text_AgreeCount->SetText(FText::FromString(
			FString::Printf(TEXT("찬성: %d / %d"), Status.AgreeCount, Status.TotalPlayers)));
	}

	// 반대 카운트
	if (Text_DisagreeCount)
	{
		Text_DisagreeCount->SetText(FText::FromString(
			FString::Printf(TEXT("반대: %d / %d"), Status.DisagreeCount, Status.TotalPlayers)));
	}

	// 남은 시간
	if (Text_RemainingTime)
	{
		Text_RemainingTime->SetText(FText::FromString(
			FString::Printf(TEXT("남은 시간: %.0f초"), Status.RemainingTime)));
	}

	// 프로그레스 바
	if (ProgressBar_Time && CachedTimeout > 0.0f)
	{
		float Progress = Status.RemainingTime / CachedTimeout;
		ProgressBar_Time->SetPercent(FMath::Clamp(Progress, 0.0f, 1.0f));
	}
}

void UVoteWidget::OnVoteEnded_Implementation(EVoteType VoteType, bool bPassed, const FString& Reason)
{
	UE_LOG(LogHellunaVote, Log, TEXT("[VoteWidget] OnVoteEnded - Passed: %s, Reason: %s"),
		bPassed ? TEXT("true") : TEXT("false"), *Reason);

	// 잠시 결과 표시 후 숨김 (BP에서 애니메이션 추가 가능)
	SetVisibility(ESlateVisibility::Collapsed);
}

// ============================================================================
// 헬퍼 함수
// ============================================================================

FString UVoteWidget::GetVoteTitleText(EVoteType VoteType) const
{
	switch (VoteType)
	{
	case EVoteType::MapMove:
		return TEXT("맵 이동 투표");
	case EVoteType::Kick:
		return TEXT("강퇴 투표");
	case EVoteType::Difficulty:
		return TEXT("난이도 변경 투표");
	default:
		return TEXT("투표");
	}
}

FString UVoteWidget::GetVoteTargetText(const FVoteRequest& Request) const
{
	switch (Request.VoteType)
	{
	case EVoteType::MapMove:
		return FString::Printf(TEXT("→ %s"), *Request.TargetMapName.ToString());
	case EVoteType::Kick:
		if (Request.TargetPlayer.IsValid())
		{
			return FString::Printf(TEXT("대상: %s"), *Request.TargetPlayer->GetPlayerName());
		}
		return TEXT("대상: ???");
	case EVoteType::Difficulty:
		return FString::Printf(TEXT("난이도: %d"), Request.TargetDifficulty);
	default:
		return TEXT("");
	}
}
