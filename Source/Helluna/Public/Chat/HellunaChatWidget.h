// File: Source/Helluna/Public/Chat/HellunaChatWidget.h
// 인게임 채팅 위젯 (Phase 10)
//
// 사용법:
//   1. 이 클래스를 부모로 WBP_HellunaChatWidget 생성
//   2. BP에서 ScrollBox_Messages, TextBox_Input, Border_InputArea 배치
//   3. BP_HellunaHeroController에서 ChatWidgetClass 지정
//
// 작성자: Gihyeon (Phase 10)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Chat/HellunaChatTypes.h"
#include "HellunaChatWidget.generated.h"

class UScrollBox;
class UEditableTextBox;
class UBorder;
class UTextBlock;

// 위젯에서 메시지 전송 요청 시 발동 (HeroController에서 수신)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChatMessageSubmitted, const FString&, Message);

/**
 * 인게임 채팅 위젯
 *
 * - 메시지 표시 영역 (ScrollBox)
 * - 입력 영역 (EditableTextBox, Enter 토글)
 * - GameState의 OnChatMessageReceived 델리게이트에 바인딩
 */
UCLASS()
class HELLUNA_API UHellunaChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ════════════════════════════════════════════════════════════════════════════
	// 외부 인터페이스
	// ════════════════════════════════════════════════════════════════════════════

	/** 채팅 입력창 토글 (Enter 키에서 호출) */
	void ToggleChatInput();

	/** 입력창 활성화 */
	void ActivateChatInput();

	/** 입력창 비활성화 + 게임 입력 복원 */
	void DeactivateChatInput();

	/** GameState 델리게이트에서 호출 — 메시지 수신 */
	UFUNCTION()
	void OnReceiveChatMessage(const FChatMessage& ChatMessage);

	/** 메시지 제출 델리게이트 (HeroController에서 바인딩) */
	UPROPERTY(BlueprintAssignable, Category = "Chat (채팅)")
	FOnChatMessageSubmitted OnChatMessageSubmitted;

	/** 입력 활성 상태 조회 */
	UFUNCTION(BlueprintPure, Category = "Chat (채팅)")
	bool IsChatInputActive() const { return bChatInputActive; }

protected:
	// ════════════════════════════════════════════════════════════════════════════
	// BindWidget (BP에서 반드시 배치할 위젯)
	// ════════════════════════════════════════════════════════════════════════════

	/** 메시지 목록 스크롤박스 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Messages ScrollBox (메시지 스크롤박스)"))
	TObjectPtr<UScrollBox> ScrollBox_Messages;

	/** 채팅 입력 텍스트박스 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Input TextBox (입력 텍스트박스)"))
	TObjectPtr<UEditableTextBox> TextBox_Input;

	/** 입력 영역 컨테이너 (토글 시 Collapsed/Visible 전환) */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget, DisplayName = "Input Area Border (입력 영역 보더)"))
	TObjectPtr<UBorder> Border_InputArea;

	// ════════════════════════════════════════════════════════════════════════════
	// 라이프사이클
	// ════════════════════════════════════════════════════════════════════════════

	virtual void NativeConstruct() override;

	// ════════════════════════════════════════════════════════════════════════════
	// 내부 함수
	// ════════════════════════════════════════════════════════════════════════════

	/** TextBox_Input의 OnTextCommitted 바인딩 */
	UFUNCTION()
	void OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** 메시지 텍스트 위젯 생성 (ScrollBox에 추가할 UTextBlock) */
	UTextBlock* CreateMessageTextBlock(const FChatMessage& ChatMessage);

private:
	/** 입력 활성 상태 */
	bool bChatInputActive = false;

	/** 최대 표시 메시지 수 */
	static constexpr int32 MaxDisplayedMessages = 100;
};
