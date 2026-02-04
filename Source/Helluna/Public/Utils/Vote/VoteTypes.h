// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "VoteTypes.generated.h"

// 로그 카테고리 선언
DECLARE_LOG_CATEGORY_EXTERN(LogHellunaVote, Log, All);

/**
 * 투표 종류
 */
UENUM(BlueprintType)
enum class EVoteType : uint8
{
	None		UMETA(DisplayName = "없음"),
	MapMove		UMETA(DisplayName = "맵 이동"),
	Kick		UMETA(DisplayName = "강퇴"),
	Difficulty	UMETA(DisplayName = "난이도")
};

/**
 * 투표 조건
 */
UENUM(BlueprintType)
enum class EVoteCondition : uint8
{
	Unanimous	UMETA(DisplayName = "만장일치"),
	Majority	UMETA(DisplayName = "과반수")
};

/**
 * 개인 투표 결과
 */
UENUM(BlueprintType)
enum class EVoteResult : uint8
{
	NotVoted	UMETA(DisplayName = "미투표"),
	Agree		UMETA(DisplayName = "찬성"),
	Disagree	UMETA(DisplayName = "반대")
};

/**
 * 중도 퇴장 정책
 */
UENUM(BlueprintType)
enum class EVoteDisconnectPolicy : uint8
{
	ExcludeAndContinue	UMETA(DisplayName = "제외 후 계속"),
	CancelVote			UMETA(DisplayName = "투표 취소")
};

/**
 * 투표 요청 구조체
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FVoteRequest
{
	GENERATED_BODY()

	/** 투표 종류 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	EVoteType VoteType = EVoteType::None;

	/** 투표 조건 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	EVoteCondition Condition = EVoteCondition::Majority;

	/** 투표 제한 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	float Timeout = 30.0f;

	/** 중도 퇴장 정책 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	EVoteDisconnectPolicy DisconnectPolicy = EVoteDisconnectPolicy::ExcludeAndContinue;

	/** 투표 시작자 */
	UPROPERTY(BlueprintReadWrite, Category = "Vote")
	TWeakObjectPtr<APlayerState> Initiator;

	// ========== 타입별 데이터 ==========

	/** 맵 이동용 - 목표 맵 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	FName TargetMapName;

	/** 강퇴용 - 대상 플레이어 */
	UPROPERTY(BlueprintReadWrite, Category = "Vote")
	TWeakObjectPtr<APlayerState> TargetPlayer;

	/** 난이도용 - 목표 난이도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	int32 TargetDifficulty = 0;

	// ========== 디버그용 함수 ==========

	/** 투표 타입 이름 반환 */
	FString GetVoteTypeName() const
	{
		switch (VoteType)
		{
		case EVoteType::None:		return TEXT("None");
		case EVoteType::MapMove:	return TEXT("MapMove");
		case EVoteType::Kick:		return TEXT("Kick");
		case EVoteType::Difficulty:	return TEXT("Difficulty");
		default:					return TEXT("Unknown");
		}
	}

	/** 유효성 검사 */
	bool IsValid() const
	{
		if (VoteType == EVoteType::None)
		{
			return false;
		}

		if (!Initiator.IsValid())
		{
			return false;
		}

		switch (VoteType)
		{
		case EVoteType::MapMove:
			return !TargetMapName.IsNone();

		case EVoteType::Kick:
			return TargetPlayer.IsValid();

		case EVoteType::Difficulty:
			return TargetDifficulty >= 0;

		default:
			return true;
		}
	}

	/** 로그 출력용 문자열 반환 */
	FString ToString() const
	{
		FString InitiatorName = Initiator.IsValid() ? Initiator->GetPlayerName() : TEXT("Invalid");
		FString ConditionStr = (Condition == EVoteCondition::Unanimous) ? TEXT("만장일치") : TEXT("과반수");
		FString PolicyStr = (DisconnectPolicy == EVoteDisconnectPolicy::ExcludeAndContinue) ? TEXT("제외 후 계속") : TEXT("투표 취소");

		FString Result = FString::Printf(
			TEXT("[VoteRequest] Type: %s, Condition: %s, Timeout: %.1f, DisconnectPolicy: %s, Initiator: %s"),
			*GetVoteTypeName(),
			*ConditionStr,
			Timeout,
			*PolicyStr,
			*InitiatorName
		);

		switch (VoteType)
		{
		case EVoteType::MapMove:
			Result += FString::Printf(TEXT(", TargetMap: %s"), *TargetMapName.ToString());
			break;

		case EVoteType::Kick:
			{
				FString TargetName = TargetPlayer.IsValid() ? TargetPlayer->GetPlayerName() : TEXT("Invalid");
				Result += FString::Printf(TEXT(", TargetPlayer: %s"), *TargetName);
			}
			break;

		case EVoteType::Difficulty:
			Result += FString::Printf(TEXT(", TargetDifficulty: %d"), TargetDifficulty);
			break;

		default:
			break;
		}

		return Result;
	}
};

/**
 * 투표 현황 구조체 - UI용
 */
USTRUCT(BlueprintType)
struct HELLUNA_API FVoteStatus
{
	GENERATED_BODY()

	/** 전체 인원 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	int32 TotalPlayers = 0;

	/** 찬성 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	int32 AgreeCount = 0;

	/** 반대 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	int32 DisagreeCount = 0;

	/** 미투표 수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	int32 NotVotedCount = 0;

	/** 남은 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vote")
	float RemainingTime = 0.0f;

	/** 로그 출력용 문자열 반환 */
	FString ToString() const
	{
		return FString::Printf(
			TEXT("[VoteStatus] Total: %d, Agree: %d, Disagree: %d, NotVoted: %d, RemainingTime: %.1f"),
			TotalPlayers,
			AgreeCount,
			DisagreeCount,
			NotVotedCount,
			RemainingTime
		);
	}
};
