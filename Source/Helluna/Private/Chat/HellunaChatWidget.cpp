// File: Source/Helluna/Private/Chat/HellunaChatWidget.cpp
// 인게임 채팅 위젯 구현 (Phase 10)

#include "Chat/HellunaChatWidget.h"
#include "Chat/HellunaChatTypes.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

// LogHellunaChat 로그 카테고리 정의 (프로젝트 전체에서 1회만)
DEFINE_LOG_CATEGORY(LogHellunaChat);

// ════════════════════════════════════════════════════════════════════════════════
// 라이프사이클
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaChatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 입력 영역 초기 상태: 숨김
	if (Border_InputArea)
	{
		Border_InputArea->SetVisibility(ESlateVisibility::Collapsed);
	}

	// TextBox_Input의 OnTextCommitted 바인딩 (B6: NativeConstruct 중복 호출 대비 AddUniqueDynamic)
	if (TextBox_Input)
	{
		TextBox_Input->OnTextCommitted.AddUniqueDynamic(this, &UHellunaChatWidget::OnChatTextCommitted);
	}

	bChatInputActive = false;

	UE_LOG(LogHellunaChat, Log, TEXT("[ChatWidget] NativeConstruct 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// 입력 토글
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaChatWidget::ToggleChatInput()
{
	if (bChatInputActive)
	{
		DeactivateChatInput();
	}
	else
	{
		ActivateChatInput();
	}
}

void UHellunaChatWidget::ActivateChatInput()
{
	if (bChatInputActive) return;
	if (!Border_InputArea || !TextBox_Input) return;

	bChatInputActive = true;

	// 입력 영역 표시
	Border_InputArea->SetVisibility(ESlateVisibility::Visible);

	// 텍스트박스 초기화 및 포커스
	TextBox_Input->SetText(FText::GetEmpty());

	// 입력 모드: GameAndUI (게임 화면 보이면서 텍스트 입력 가능)
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(TextBox_Input->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}

	UE_LOG(LogHellunaChat, Verbose, TEXT("[ChatWidget] 채팅 입력 활성화"));
}

void UHellunaChatWidget::DeactivateChatInput()
{
	if (!bChatInputActive) return;
	if (!Border_InputArea || !TextBox_Input) return;

	bChatInputActive = false;

	// 입력 영역 숨김
	Border_InputArea->SetVisibility(ESlateVisibility::Collapsed);

	// 텍스트 초기화
	TextBox_Input->SetText(FText::GetEmpty());

	// U14: 인벤토리/컨테이너 UI가 열린 상태에서 채팅 종료 시 마우스 커서 유지
	// bShowMouseCursor가 true면 다른 UI가 마우스를 사용 중 → GameAndUI 유지
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		if (PC->bShowMouseCursor)
		{
			// 다른 UI(인벤토리/컨테이너)가 마우스 커서를 사용 중 → GameAndUI 유지
			PC->SetInputMode(FInputModeGameAndUI());
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly());
		}
	}

	UE_LOG(LogHellunaChat, Verbose, TEXT("[ChatWidget] 채팅 입력 비활성화"));
}

// ════════════════════════════════════════════════════════════════════════════════
// 텍스트 입력 완료 콜백
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaChatWidget::OnChatTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		// Enter: 메시지 전송
		FString MessageStr = Text.ToString().TrimStartAndEnd();
		if (!MessageStr.IsEmpty())
		{
			OnChatMessageSubmitted.Broadcast(MessageStr);
		}
		DeactivateChatInput();
	}
	else if (CommitMethod == ETextCommit::OnCleared)
	{
		// Escape: 입력 취소
		DeactivateChatInput();
	}
	// OnUserMovedFocus 등: 무시 (입력 유지)
}

// ════════════════════════════════════════════════════════════════════════════════
// 메시지 수신 및 표시
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaChatWidget::OnReceiveChatMessage(const FChatMessage& ChatMessage)
{
	if (!ScrollBox_Messages) return;

	// 메시지 텍스트 블록 생성
	UTextBlock* MessageText = CreateMessageTextBlock(ChatMessage);
	if (!MessageText) return;

	// ScrollBox에 추가
	ScrollBox_Messages->AddChild(MessageText);

	// 최대 메시지 수 초과 시 가장 오래된 메시지 제거
	while (ScrollBox_Messages->GetChildrenCount() > MaxDisplayedMessages)
	{
		UWidget* OldestChild = ScrollBox_Messages->GetChildAt(0);
		if (OldestChild)
		{
			ScrollBox_Messages->RemoveChild(OldestChild);
		}
	}

	// W5: 스크롤을 맨 아래로 (같은 프레임에서는 새 자식의 지오메트리 미계산 → 다음 틱으로 지연)
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UScrollBox> WeakScrollBox = ScrollBox_Messages;
		World->GetTimerManager().SetTimerForNextTick([WeakScrollBox]()
		{
			if (WeakScrollBox.IsValid())
			{
				WeakScrollBox->ScrollToEnd();
			}
		});
	}
}

UTextBlock* UHellunaChatWidget::CreateMessageTextBlock(const FChatMessage& ChatMessage)
{
	UTextBlock* TextBlock = NewObject<UTextBlock>(this);
	if (!TextBlock) return nullptr;

	// 메시지 포맷
	FString FormattedText;
	FSlateColor TextColor;

	if (ChatMessage.MessageType == EChatMessageType::System)
	{
		// 시스템 메시지: 노란색
		FormattedText = FString::Printf(TEXT("[시스템] %s"), *ChatMessage.Message);
		TextColor = FSlateColor(FLinearColor(1.f, 0.85f, 0.f, 1.f)); // 노란색
	}
	else
	{
		// 플레이어 메시지: 흰색
		FormattedText = FString::Printf(TEXT("[%s] %s"), *ChatMessage.SenderName, *ChatMessage.Message);
		TextColor = FSlateColor(FLinearColor::White);
	}

	TextBlock->SetText(FText::FromString(FormattedText));
	TextBlock->SetColorAndOpacity(TextColor);

	// 폰트 크기 설정
	FSlateFontInfo FontInfo = TextBlock->GetFont();
	FontInfo.Size = 14;
	TextBlock->SetFont(FontInfo);

	// 자동 줄바꿈
	TextBlock->SetAutoWrapText(true);

	return TextBlock;
}
