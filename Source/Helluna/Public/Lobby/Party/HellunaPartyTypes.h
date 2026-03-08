// File: Source/Helluna/Public/Lobby/Party/HellunaPartyTypes.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 파티 시스템 공용 타입 정의
//
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 12a] 파티 관련 USTRUCT, UENUM, 델리게이트 선언
//
// 사용처:
//   - HellunaSQLiteSubsystem (파티 DB CRUD)
//   - HellunaLobbyGameMode (파티 서버 로직)
//   - HellunaLobbyController (파티 RPC)
//   - HellunaPartyWidget (파티 UI)
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "HellunaPartyTypes.generated.h"

// ════════════════════════════════════════════════════════════════
// UENUM
// ════════════════════════════════════════════════════════════════

/** 파티 내 역할 */
UENUM(BlueprintType)
enum class EHellunaPartyRole : uint8
{
	Leader = 0	UMETA(DisplayName = "Leader (리더)"),
	Member = 1	UMETA(DisplayName = "Member (멤버)")
};

/** 게임 채널 상태 */
UENUM(BlueprintType)
enum class EChannelStatus : uint8
{
	Empty   = 0	UMETA(DisplayName = "Empty (비어있음)"),
	Playing = 1	UMETA(DisplayName = "Playing (플레이 중)"),
	Offline = 2	UMETA(DisplayName = "Offline (오프라인)")
};

// ════════════════════════════════════════════════════════════════
// USTRUCT
// ════════════════════════════════════════════════════════════════

/** 파티 멤버 1명의 정보 */
USTRUCT(BlueprintType)
struct FHellunaPartyMemberInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString PlayerId;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString DisplayName;

	/** 선택한 영웅 (int32 — EHellunaHeroType 값, 기본 3=None) */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 SelectedHeroType = 3;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	EHellunaPartyRole Role = EHellunaPartyRole::Member;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	bool bIsReady = false;
};

/** 파티 전체 정보 */
USTRUCT(BlueprintType)
struct FHellunaPartyInfo
{
	GENERATED_BODY()

	/** DB의 party_groups.id (0 = 무효) */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 PartyId = 0;

	/** 6자리 파티 코드 */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString PartyCode;

	/** 리더 PlayerId */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString LeaderId;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	TArray<FHellunaPartyMemberInfo> Members;

	/** 유효한 파티인지 (PartyId > 0) */
	bool IsValid() const { return PartyId > 0; }
};

/** 파티 채팅 메시지 (메모리 전용, DB 미저장) */
USTRUCT(BlueprintType)
struct FHellunaPartyChatMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString SenderName;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	float ServerTime = 0.f;
};

/** 게임 채널 정보 (서버 레지스트리) */
USTRUCT(BlueprintType)
struct FGameChannelInfo
{
	GENERATED_BODY()

	/** "channel_7778" 등 */
	UPROPERTY(BlueprintReadOnly, Category = "Channel")
	FString ChannelId;

	/** 게임서버 포트 */
	UPROPERTY(BlueprintReadOnly, Category = "Channel")
	int32 Port = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Channel")
	EChannelStatus Status = EChannelStatus::Empty;

	UPROPERTY(BlueprintReadOnly, Category = "Channel")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Channel")
	int32 MaxPlayers = 3;

	UPROPERTY(BlueprintReadOnly, Category = "Channel")
	FString MapName;
};

// ════════════════════════════════════════════════════════════════
// 델리게이트
// ════════════════════════════════════════════════════════════════

/** 파티 상태 변경 (위젯 갱신용) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPartyStateChanged, const FHellunaPartyInfo&, PartyInfo);

/** 파티 채팅 수신 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPartyChatReceived, const FHellunaPartyChatMessage&, ChatMessage);

/** 파티 에러 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPartyError, const FString&, ErrorMessage);
