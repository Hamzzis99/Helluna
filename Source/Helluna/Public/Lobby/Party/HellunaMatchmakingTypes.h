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
#include "Engine/Texture2D.h"
#include "Lobby/Party/HellunaPartyTypes.h"
#include "HellunaMatchmakingTypes.generated.h"

// ============================================================================
// UENUM
// ============================================================================

/** [Phase 18] 로비 게임 모드 (Solo/Duo/Squad) */
UENUM(BlueprintType)
enum class ELobbyGameMode : uint8
{
	Solo   = 0  UMETA(DisplayName = "Solo (솔로)"),
	Duo    = 1  UMETA(DisplayName = "Duo (듀오)"),
	Squad  = 2  UMETA(DisplayName = "Squad (스쿼드)")
};

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

/** 게임 맵 설정 정보 */
USTRUCT(BlueprintType)
struct FHellunaGameMapInfo
{
	GENERATED_BODY()

	/** 맵 식별 키 (예: "GihyeonMap", "GihyeonMap2") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString MapKey;

	/** UI 표시 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString DisplayName;

	/** UE 맵 경로 (예: "/Game/Maps/GihyeonMap") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	FString MapPath;

	/** [Phase 17] 맵 썸네일 이미지 (PUBG식 맵 카드 UI) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TSoftObjectPtr<UTexture2D> MapThumbnail;
};

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

	/** [Phase 16] 선택한 맵 키 (같은 맵끼리만 매칭) */
	FString SelectedMapKey;

	/** [Phase 18] 큐 진입 시 게임 모드 (Solo/Duo/Squad) */
	ELobbyGameMode GameMode = ELobbyGameMode::Squad;

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

// ============================================================================
// [Phase 17] 매칭 카운트다운 구조체 + 델리게이트
// ============================================================================

/** 매칭 완료 시 클라이언트에 전달할 정보 */
USTRUCT(BlueprintType)
struct FMatchmakingFoundInfo
{
	GENERATED_BODY()

	/** 매칭된 전원의 파티 멤버 정보 (SetPartyPreview 호출용) */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	TArray<FHellunaPartyMemberInfo> MatchedMembers;

	/** 카운트다운 총 시간 (초) */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	int32 CountdownSeconds = 5;

	/** 본인 영웅이 재배정 되었는지 */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	bool bHeroWasReassigned = false;

	/** 원래 선택했던 영웅 (재배정 시에만 유효) */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	int32 OriginalHeroType = 3; // None

	/** 새로 배정된 영웅 (재배정 시에만 유효) */
	UPROPERTY(BlueprintReadOnly, Category = "Matchmaking")
	int32 AssignedHeroType = 3; // None
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMatchmakingFoundChanged, const FMatchmakingFoundInfo&, FoundInfo);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMatchmakingCountdownChanged, int32, RemainingSeconds);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMatchmakingCancelledChanged, const FString&, Reason);

// ============================================================================
// [Phase 18] 모드 유틸
// ============================================================================

/** 게임 모드의 최대 인원 (Solo=1, Duo=2, Squad=3) */
inline int32 GetModeCapacity(ELobbyGameMode Mode)
{
	switch (Mode)
	{
	case ELobbyGameMode::Solo:  return 1;
	case ELobbyGameMode::Duo:   return 2;
	case ELobbyGameMode::Squad: return 3;
	default:                    return 3;
	}
}
