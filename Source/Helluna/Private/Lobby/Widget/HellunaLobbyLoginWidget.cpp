// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyLoginWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 13] 로비 전용 로그인 위젯 구현
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaLobbyLoginWidget.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

#include "Lobby/HellunaLobbyLog.h"

void UHellunaLobbyLoginWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (LoginButton)
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UHellunaLobbyLoginWidget::OnLoginButtonClicked);
	}
}

void UHellunaLobbyLoginWidget::OnLoginButtonClicked()
{
	if (!IDInputTextBox || !PasswordInputTextBox)
	{
		return;
	}

	const FString PlayerId = IDInputTextBox->GetText().ToString().TrimStartAndEnd();
	const FString Password = PasswordInputTextBox->GetText().ToString().TrimStartAndEnd();

	if (PlayerId.IsEmpty())
	{
		ShowError(TEXT("아이디를 입력해주세요."));
		return;
	}

	if (Password.IsEmpty())
	{
		ShowError(TEXT("비밀번호를 입력해주세요."));
		return;
	}

	// 버튼 비활성화 (중복 클릭 방지)
	LoginButton->SetIsEnabled(false);

	// Controller에 Server RPC 호출
	AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(GetOwningPlayer());
	if (LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyLoginWidget] Server_RequestLobbyLogin 호출 | PlayerId=%s"), *PlayerId);
		LobbyPC->Server_RequestLobbyLogin(PlayerId, Password);
	}
	else
	{
		ShowError(TEXT("서버 연결 오류"));
		LoginButton->SetIsEnabled(true);
	}
}

void UHellunaLobbyLoginWidget::ShowError(const FString& Message)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
	}

	if (LoginButton)
	{
		LoginButton->SetIsEnabled(true);
	}
}

void UHellunaLobbyLoginWidget::ShowSuccess()
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(TEXT("로그인 성공!")));
		MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
	}
}
