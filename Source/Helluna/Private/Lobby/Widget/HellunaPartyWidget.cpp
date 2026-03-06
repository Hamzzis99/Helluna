// File: Source/Helluna/Private/Lobby/Widget/HellunaPartyWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 12g] 파티 팝업 위젯 구현
//
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaPartyWidget.h"
#include "Lobby/Widget/HellunaPartyMemberEntry.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "HAL/PlatformApplicationMisc.h"

// 로그 카테고리 (HellunaLobbyLog.h — DEFINE은 HellunaLobbyGameMode.cpp)
#include "Lobby/HellunaLobbyLog.h"

// ════════════════════════════════════════════════════════════════════════════════
// NativeOnInitialized
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaPartyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] NativeOnInitialized 시작"));

	// ── 버튼 바인딩 ──
	if (Button_Close)
	{
		Button_Close->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCloseClicked);
	}
	if (Button_Join)
	{
		Button_Join->OnClicked.AddUniqueDynamic(this, &ThisClass::OnJoinClicked);
	}
	if (Button_CreateParty)
	{
		Button_CreateParty->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCreatePartyClicked);
	}
	if (Button_CopyCode)
	{
		Button_CopyCode->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCopyCodeClicked);
	}
	if (Button_Ready)
	{
		Button_Ready->OnClicked.AddUniqueDynamic(this, &ThisClass::OnReadyClicked);
	}
	if (Button_Leave)
	{
		Button_Leave->OnClicked.AddUniqueDynamic(this, &ThisClass::OnLeaveClicked);
	}
	if (Button_SendChat)
	{
		Button_SendChat->OnClicked.AddUniqueDynamic(this, &ThisClass::OnSendChatClicked);
	}
	if (TextBox_ChatInput)
	{
		TextBox_ChatInput->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::OnChatInputCommitted);
	}

	// ── Controller 델리게이트 바인딩 ──
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (LobbyPC)
	{
		LobbyPC->OnPartyStateChanged.AddUniqueDynamic(this, &ThisClass::HandlePartyStateChanged);
		LobbyPC->OnPartyChatReceived.AddUniqueDynamic(this, &ThisClass::HandlePartyChatReceived);
		LobbyPC->OnPartyError.AddUniqueDynamic(this, &ThisClass::HandlePartyError);
		UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] Controller 델리게이트 바인딩 완료"));

		// 이미 파티 중이면 상세 페이지 표시
		if (LobbyPC->CurrentPartyInfo.IsValid())
		{
			UpdatePartyState(LobbyPC->CurrentPartyInfo);
		}
		else
		{
			SwitchToPage(false);
		}
	}
	else
	{
		SwitchToPage(false);
		UE_LOG(LogHellunaLobby, Warning, TEXT("[PartyWidget] NativeOnInitialized: LobbyController nullptr!"));
	}

	// ── 영웅 중복 경고 초기 숨김 ──
	if (Text_HeroDupeWarning)
	{
		Text_HeroDupeWarning->SetVisibility(ESlateVisibility::Collapsed);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] NativeOnInitialized 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// NativeDestruct — 델리게이트 해제
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaPartyWidget::NativeDestruct()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (LobbyPC)
	{
		LobbyPC->OnPartyStateChanged.RemoveDynamic(this, &ThisClass::HandlePartyStateChanged);
		LobbyPC->OnPartyChatReceived.RemoveDynamic(this, &ThisClass::HandlePartyChatReceived);
		LobbyPC->OnPartyError.RemoveDynamic(this, &ThisClass::HandlePartyError);
	}

	Super::NativeDestruct();
}

// ════════════════════════════════════════════════════════════════════════════════
// 외부 인터페이스
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaPartyWidget::UpdatePartyState(const FHellunaPartyInfo& PartyInfo)
{
	CachedPartyInfo = PartyInfo;

	if (PartyInfo.IsValid())
	{
		SwitchToPage(true);

		// 파티 코드 표시
		if (Text_PartyCode)
		{
			Text_PartyCode->SetText(FText::FromString(PartyInfo.PartyCode));
		}

		// 멤버 목록 갱신
		RefreshMemberList(PartyInfo);

		// 로컬 플레이어 준비 상태 추적
		AHellunaLobbyController* LobbyPC = GetLobbyController();
		if (LobbyPC)
		{
			const FString LocalPlayerId = LobbyPC->GetPlayerId();
			for (const FHellunaPartyMemberInfo& Member : PartyInfo.Members)
			{
				if (Member.PlayerId == LocalPlayerId)
				{
					bLocalReady = Member.bIsReady;
					break;
				}
			}
		}

		// 준비 버튼 텍스트 갱신
		if (Text_ReadyStatus)
		{
			Text_ReadyStatus->SetText(FText::FromString(bLocalReady ? TEXT("Ready!") : TEXT("Not Ready")));
		}

		// 영웅 중복 검사
		CheckHeroDuplication(PartyInfo);
	}
	else
	{
		SwitchToPage(false);
	}
}

void UHellunaPartyWidget::OnPartyDisbanded(const FString& Reason)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] 파티 해산: %s"), *Reason);
	CachedPartyInfo = FHellunaPartyInfo();
	bLocalReady = false;
	SwitchToPage(false);
}

void UHellunaPartyWidget::ShowPartyError(const FString& ErrorMessage)
{
	UE_LOG(LogHellunaLobby, Warning, TEXT("[PartyWidget] 에러: %s"), *ErrorMessage);
	// TODO: BP에서 에러 팝업/토스트 UI 확장 가능
}

void UHellunaPartyWidget::AddChatMessage(const FHellunaPartyChatMessage& ChatMessage)
{
	if (!ScrollBox_PartyChat)
	{
		return;
	}

	// 간단한 텍스트 기반 채팅 메시지 추가
	UTextBlock* MsgText = NewObject<UTextBlock>(this);
	if (MsgText)
	{
		const FString FormattedMsg = FString::Printf(TEXT("[%s] %s"), *ChatMessage.SenderName, *ChatMessage.Message);
		MsgText->SetText(FText::FromString(FormattedMsg));

		FSlateFontInfo FontInfo = MsgText->GetFont();
		FontInfo.Size = 12;
		MsgText->SetFont(FontInfo);

		ScrollBox_PartyChat->AddChild(MsgText);

		// ScrollBox에 자식이 너무 많으면 가장 오래된 것 제거
		const int32 MaxMessages = 100;
		while (ScrollBox_PartyChat->GetChildrenCount() > MaxMessages)
		{
			ScrollBox_PartyChat->RemoveChildAt(0);
		}

		ScrollBox_PartyChat->ScrollToEnd();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 버튼 콜백
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaPartyWidget::OnCloseClicked()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UHellunaPartyWidget::OnJoinClicked()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		return;
	}

	if (!TextBox_PartyCode)
	{
		return;
	}

	const FString Code = TextBox_PartyCode->GetText().ToString().TrimStartAndEnd();
	if (Code.Len() != 6)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[PartyWidget] 파티 코드는 6자리여야 합니다: '%s'"), *Code);
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] 파티 참가 요청: %s"), *Code);
	LobbyPC->Server_JoinParty(Code);
}

void UHellunaPartyWidget::OnCreatePartyClicked()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] 파티 생성 요청"));
	LobbyPC->Server_CreateParty();
}

void UHellunaPartyWidget::OnCopyCodeClicked()
{
	if (CachedPartyInfo.PartyCode.IsEmpty())
	{
		return;
	}

	FPlatformApplicationMisc::ClipboardCopy(*CachedPartyInfo.PartyCode);
	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] 파티 코드 복사: %s"), *CachedPartyInfo.PartyCode);
}

void UHellunaPartyWidget::OnReadyClicked()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		return;
	}

	bLocalReady = !bLocalReady;
	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] 준비 토글 → %s"), bLocalReady ? TEXT("Ready") : TEXT("Not Ready"));
	LobbyPC->Server_SetPartyReady(bLocalReady);
}

void UHellunaPartyWidget::OnLeaveClicked()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] 파티 나가기 요청"));
	LobbyPC->Server_LeaveParty();
}

void UHellunaPartyWidget::OnSendChatClicked()
{
	if (!TextBox_ChatInput)
	{
		return;
	}

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		return;
	}

	const FString Msg = TextBox_ChatInput->GetText().ToString().TrimStartAndEnd();
	if (Msg.IsEmpty())
	{
		return;
	}

	LobbyPC->Server_SendPartyChatMessage(Msg);
	TextBox_ChatInput->SetText(FText::GetEmpty());
}

void UHellunaPartyWidget::OnChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		OnSendChatClicked();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// Controller 델리게이트 핸들러
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaPartyWidget::HandlePartyStateChanged(const FHellunaPartyInfo& PartyInfo)
{
	UpdatePartyState(PartyInfo);
}

void UHellunaPartyWidget::HandlePartyChatReceived(const FHellunaPartyChatMessage& ChatMessage)
{
	AddChatMessage(ChatMessage);
}

void UHellunaPartyWidget::HandlePartyError(const FString& ErrorMessage)
{
	ShowPartyError(ErrorMessage);
}

// ════════════════════════════════════════════════════════════════════════════════
// 내부 헬퍼
// ════════════════════════════════════════════════════════════════════════════════

AHellunaLobbyController* UHellunaPartyWidget::GetLobbyController() const
{
	APlayerController* PC = GetOwningPlayer();
	return PC ? Cast<AHellunaLobbyController>(PC) : nullptr;
}

void UHellunaPartyWidget::RefreshMemberList(const FHellunaPartyInfo& PartyInfo)
{
	if (!MemberListBox)
	{
		return;
	}

	// 기존 엔트리 제거
	MemberListBox->ClearChildren();

	if (!MemberEntryClass)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[PartyWidget] MemberEntryClass 미설정! BP에서 설정 필요"));
		return;
	}

	// 로컬 플레이어 정보
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	const FString LocalPlayerId = LobbyPC ? LobbyPC->GetPlayerId() : FString();
	const bool bLocalIsLeader = (PartyInfo.LeaderId == LocalPlayerId);

	for (const FHellunaPartyMemberInfo& Member : PartyInfo.Members)
	{
		UHellunaPartyMemberEntry* Entry = CreateWidget<UHellunaPartyMemberEntry>(this, MemberEntryClass);
		if (!Entry)
		{
			continue;
		}

		const bool bIsLocal = (Member.PlayerId == LocalPlayerId);
		Entry->SetMemberInfo(Member, bLocalIsLeader, bIsLocal);

		// Kick 델리게이트 바인딩
		if (bLocalIsLeader && !bIsLocal)
		{
			Entry->OnKickRequested.AddUniqueDynamic(this, &ThisClass::OnKickMemberRequested);
		}

		MemberListBox->AddChild(Entry);
	}
}

void UHellunaPartyWidget::SwitchToPage(bool bInParty)
{
	if (PartySwitcher)
	{
		PartySwitcher->SetActiveWidgetIndex(bInParty ? 1 : 0);
	}
}

void UHellunaPartyWidget::CheckHeroDuplication(const FHellunaPartyInfo& PartyInfo)
{
	if (!Text_HeroDupeWarning)
	{
		return;
	}

	// 영웅 타입 중복 검사 (None=3 제외)
	TMap<int32, int32> HeroCounts;
	bool bHasDupe = false;

	for (const FHellunaPartyMemberInfo& Member : PartyInfo.Members)
	{
		if (Member.SelectedHeroType == static_cast<int32>(EHellunaHeroType::None)) // [Fix47-L2]
		{
			continue;
		}

		int32& Count = HeroCounts.FindOrAdd(Member.SelectedHeroType, 0);
		Count++;
		if (Count > 1)
		{
			bHasDupe = true;
		}
	}

	Text_HeroDupeWarning->SetVisibility(bHasDupe ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

// ════════════════════════════════════════════════════════════════════════════════
// Kick 멤버 (리더 전용)
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaPartyWidget::OnKickMemberRequested(const FString& PlayerId)
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[PartyWidget] 강퇴 요청: %s"), *PlayerId);
	LobbyPC->Server_KickPartyMember(PlayerId);
}
