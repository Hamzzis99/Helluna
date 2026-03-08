// File: Source/Helluna/Public/Chat/HellunaChatTypes.h
// 채팅 시스템 공용 타입 정의 (Phase 10)

#pragma once

#include "CoreMinimal.h"
#include "HellunaChatTypes.generated.h"

// 로그 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogHellunaChat, Log, All);

// ════════════════════════════════════════════════════════════════════════════════
// 채팅 메시지 타입
// ════════════════════════════════════════════════════════════════════════════════

UENUM(BlueprintType)
enum class EChatMessageType : uint8
{
	/** 플레이어가 보낸 일반 메시지 */
	Player		UMETA(DisplayName = "Player Message (플레이어 메시지)"),

	/** 시스템 알림 메시지 (접속/퇴장, 낮/밤 전환 등) */
	System		UMETA(DisplayName = "System Message (시스템 메시지)"),
};

// ════════════════════════════════════════════════════════════════════════════════
// 채팅 메시지 구조체
// ════════════════════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FChatMessage
{
	GENERATED_BODY()

	/** 발신자 이름 (Player 타입: PlayerUniqueId, System 타입: 빈 문자열) */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FString SenderName;

	/** 메시지 본문 */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	FString Message;

	/** 메시지 타입 */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	EChatMessageType MessageType = EChatMessageType::Player;

	/** 서버 시간 (GetWorld()->GetTimeSeconds() 기준) */
	UPROPERTY(BlueprintReadOnly, Category = "Chat")
	float ServerTime = 0.f;
};

// ════════════════════════════════════════════════════════════════════════════════
// 채팅 메시지 수신 델리게이트
// ════════════════════════════════════════════════════════════════════════════════

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatMessageReceived, const FChatMessage&, ChatMessage);
