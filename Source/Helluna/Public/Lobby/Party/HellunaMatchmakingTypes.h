// ============================================================================
// HellunaMatchmakingTypes.h
// ============================================================================
//
// [Phase 15] 매치메이킹 시스템 공용 타입 정의
//
// 사용처:
//   - HellunaLobbyGameMode (매칭 큐 관리)
//   - HellunaLobbyController (매칭 RPC)
//   - HellunaLobbyStashWidget (매칭 UI)
//
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "HellunaMatchmakingTypes.generated.h"

// ============================================================================
// UENUM
// ============================================================================

/** 매칭 큐 상태 */
UENUM(BlueprintType)
enum class EMatchmakingStatus : uint8
{
	None      = 0  UMETA(DisplayName = "None (없음)"),
	Searching = 1  UMETA(DisplayName = "Searching (매칭 중)"),
	Found     = 2  UMETA(DisplayName = "Match Found (매칭 완료)"),
	Deploying = 3  UMETA(DisplayName = "Deploying (출격 중)")
};

// ============================================================================
// USTRUCT
// ============================================================================

/** 큐 엔트리 (1 파티 or 1 솔로 = 1 엔트리) — 서버 전용 */
USTRUCT()
struct FMatchmakingQueueEntry
{
	GENERATED_BODY()

	int32 EntryId = 0;

	/** 파티면 파티ID, 솔로면 0 */
	int32 PartyId = 0;

	/** 이 엔트리의 플레이어 ID 목록 */
	TArray<FString> PlayerIds;

	/** 각 플레이어 영웅 타입 (PlayerIds와 인덱스 매칭) */
	TArray<int32> HeroTypes;

	/** 큐 진입 서버 시간 (FPlatformTime::Seconds) */
	double QueueEnterTime = 0.0;

	int32 GetPlayerCount() const { return PlayerIds.Num(); }
};

/** 클라이언트용 상태 정보 (RPC 전달용) */
USTRUCT(BlueprintType)
struct FMatchmakingStatusInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	EMatchmakingStatus Status = EMatchmakingStatus::None;

	/** 큐 대기 시간 (초) */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	float ElapsedTime = 0.f;

	/** 현재 매칭된 인원 */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	int32 CurrentPlayerCount = 0;

	/** 목표 인원 */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	int32 TargetPlayerCount = 3;
};

// ============================================================================
// Delegate
// ============================================================================

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMatchmakingStatusChanged, const FMatchmakingStatusInfo&, StatusInfo);
