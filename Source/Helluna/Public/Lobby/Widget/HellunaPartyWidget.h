// File: Source/Helluna/Public/Lobby/Widget/HellunaPartyWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 12g] 파티 팝업 위젯
//
// 레이아웃:
//   ┌─────────────────────────────────────────────────┐
//   │  [파티 만들기]  [파티 참가]           [X]  Close │  ← PartySwitcher Page 0
//   │  ┌─────────────────────────┐                    │
//   │  │  코드 입력: [______]    │  [참가]            │
//   │  └─────────────────────────┘                    │
//   ├─────────────────────────────────────────────────┤
//   │  파티 코드: ABC123     [복사]                    │  ← PartySwitcher Page 1
//   │  ┌─ 멤버 목록 ─────────────┐                    │
//   │  │  [Crown] Player1 (리더) │                    │
//   │  │         Player2         │                    │
//   │  │         Player3         │                    │
//   │  └─────────────────────────┘                    │
//   │  ┌─ 채팅 ──────────────────┐                    │
//   │  │  ...                    │                    │
//   │  └─────────────────────────┘                    │
//   │  [준비]                           [파티 나가기] │
//   └─────────────────────────────────────────────────┘
//
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Lobby/Party/HellunaPartyTypes.h"
#include "HellunaPartyWidget.generated.h"

// 전방 선언
class UWidgetSwitcher;
class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UScrollBox;
class AHellunaLobbyController;
class UHellunaPartyMemberEntry;

UCLASS()
class HELLUNA_API UHellunaPartyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	// ════════════════════════════════════════════════════════════════
	// 외부 인터페이스
	// ════════════════════════════════════════════════════════════════

	/** 파티 상태 갱신 (Controller에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "Party (파티)",
		meta = (DisplayName = "Update Party State (파티 상태 갱신)"))
	void UpdatePartyState(const FHellunaPartyInfo& PartyInfo);

	/** 파티 해산됨 (Controller에서 호출) */
	UFUNCTION(BlueprintCallable, Category = "Party (파티)",
		meta = (DisplayName = "On Party Disbanded (파티 해산)"))
	void OnPartyDisbanded(const FString& Reason);

	/** 파티 에러 표시 */
	UFUNCTION(BlueprintCallable, Category = "Party (파티)",
		meta = (DisplayName = "Show Party Error (파티 에러 표시)"))
	void ShowPartyError(const FString& ErrorMessage);

	/** 채팅 메시지 수신 */
	UFUNCTION(BlueprintCallable, Category = "Party (파티)",
		meta = (DisplayName = "Add Chat Message (채팅 메시지 추가)"))
	void AddChatMessage(const FHellunaPartyChatMessage& ChatMessage);

protected:
	// ════════════════════════════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ════════════════════════════════════════════════════════════════

	/** 페이지 전환: Page0=미가입, Page1=파티 상세 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> PartySwitcher;

	/** 닫기 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Close;

	// ── Page 0: 파티 미가입 ──

	/** 파티 코드 입력 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> TextBox_PartyCode;

	/** 참가 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Join;

	/** 파티 만들기 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_CreateParty;

	// ── Page 1: 파티 상세 ──

	/** 파티 코드 표시 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Text_PartyCode;

	/** 코드 복사 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_CopyCode;

	/** 멤버 목록 컨테이너 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> MemberListBox;

	/** 채팅 스크롤 박스 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> ScrollBox_PartyChat;

	/** 준비/해제 토글 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Ready;

	/** 준비 상태 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_ReadyStatus;

	/** 파티 나가기 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Leave;

	/** 채팅 입력 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> TextBox_ChatInput;

	/** 채팅 전송 버튼 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_SendChat;

	/** 영웅 중복 경고 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_HeroDupeWarning;

	// ════════════════════════════════════════════════════════════════
	// 멤버 엔트리 위젯 클래스 (BP에서 설정)
	// ════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, Category = "Party (파티)",
		meta = (DisplayName = "Member Entry Widget Class (멤버 엔트리 위젯 클래스)"))
	TSubclassOf<UHellunaPartyMemberEntry> MemberEntryClass;

private:
	// ════════════════════════════════════════════════════════════════
	// 버튼 콜백
	// ════════════════════════════════════════════════════════════════

	UFUNCTION()
	void OnCloseClicked();

	UFUNCTION()
	void OnJoinClicked();

	UFUNCTION()
	void OnCreatePartyClicked();

	UFUNCTION()
	void OnCopyCodeClicked();

	UFUNCTION()
	void OnReadyClicked();

	UFUNCTION()
	void OnLeaveClicked();

	UFUNCTION()
	void OnSendChatClicked();

	UFUNCTION()
	void OnChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	// ════════════════════════════════════════════════════════════════
	// Controller 델리게이트 핸들러
	// ════════════════════════════════════════════════════════════════

	UFUNCTION()
	void HandlePartyStateChanged(const FHellunaPartyInfo& PartyInfo);

	UFUNCTION()
	void HandlePartyChatReceived(const FHellunaPartyChatMessage& ChatMessage);

	UFUNCTION()
	void HandlePartyError(const FString& ErrorMessage);

	UFUNCTION()
	void OnKickMemberRequested(const FString& PlayerId);

	// ════════════════════════════════════════════════════════════════
	// 내부 헬퍼
	// ════════════════════════════════════════════════════════════════

	AHellunaLobbyController* GetLobbyController() const;

	/** 멤버 목록 UI 갱신 */
	void RefreshMemberList(const FHellunaPartyInfo& PartyInfo);

	/** 현재 파티 가입 여부에 따라 페이지 전환 */
	void SwitchToPage(bool bInParty);

	/** 영웅 중복 검사 + 경고 표시 */
	void CheckHeroDuplication(const FHellunaPartyInfo& PartyInfo);

	// ════════════════════════════════════════════════════════════════
	// 내부 상태
	// ════════════════════════════════════════════════════════════════

	/** 현재 파티 정보 캐시 */
	FHellunaPartyInfo CachedPartyInfo;

	/** 로컬 플레이어의 준비 상태 캐시 */
	bool bLocalReady = false;
};
