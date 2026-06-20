// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyLoginWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// [Phase 13] 로비 전용 로그인/회원가입 위젯 구현
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaLobbyLoginWidget.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Kismet/KismetSystemLibrary.h"

#include "Lobby/HellunaLobbyLog.h"

// [Fix45-H2] NativeDestruct — 버튼 델리게이트 해제
void UHellunaLobbyLoginWidget::NativeDestruct()
{
	if (LoginButton) { LoginButton->OnClicked.RemoveDynamic(this, &UHellunaLobbyLoginWidget::OnLoginButtonClicked); }
	if (LoginTabButton) { LoginTabButton->OnClicked.RemoveDynamic(this, &UHellunaLobbyLoginWidget::OnLoginTabClicked); }
	if (SignupTabButton) { SignupTabButton->OnClicked.RemoveDynamic(this, &UHellunaLobbyLoginWidget::OnSignupTabClicked); }
	if (QuitButton) { QuitButton->OnClicked.RemoveDynamic(this, &UHellunaLobbyLoginWidget::OnQuitClicked); }

	Super::NativeDestruct();
}

void UHellunaLobbyLoginWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (LoginButton)
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UHellunaLobbyLoginWidget::OnLoginButtonClicked);
	}

	if (LoginTabButton)
	{
		LoginTabButton->OnClicked.AddUniqueDynamic(this, &UHellunaLobbyLoginWidget::OnLoginTabClicked);
	}

	if (SignupTabButton)
	{
		SignupTabButton->OnClicked.AddUniqueDynamic(this, &UHellunaLobbyLoginWidget::OnSignupTabClicked);
	}

	if (QuitButton)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UHellunaLobbyLoginWidget::OnQuitClicked);
	}

	// 초기 상태: 로그인 모드
	SwitchToLoginMode();
}

void UHellunaLobbyLoginWidget::OnLoginTabClicked()
{
	if (!bIsSignupMode)
	{
		return;
	}
	SwitchToLoginMode();
}

void UHellunaLobbyLoginWidget::OnSignupTabClicked()
{
	if (bIsSignupMode)
	{
		return;
	}
	SwitchToSignupMode();
}

void UHellunaLobbyLoginWidget::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}

void UHellunaLobbyLoginWidget::SwitchToLoginMode()
{
	bIsSignupMode = false;

	// 비밀번호 확인 입력란 + 라벨 숨김 (로그인 모드)
	if (ConfirmPasswordInputTextBox)
	{
		ConfirmPasswordInputTextBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (Text_PWConfirmLabel)
	{
		Text_PWConfirmLabel->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 버튼 라벨 = LOGIN (_04 시안)
	if (Text_LoginLabel)
	{
		Text_LoginLabel->SetText(FText::FromString(TEXT("LOGIN")));
	}

	// 링크 토글: 현재 로그인 모드 → 회원가입(SIGN IN) 링크만 노출
	if (LoginTabButton)
	{
		LoginTabButton->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (SignupTabButton)
	{
		SignupTabButton->SetVisibility(ESlateVisibility::Visible);
	}

	// 메시지 초기화
	if (MessageText)
	{
		MessageText->SetText(FText::GetEmpty());
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyLoginWidget] 로그인 모드로 전환"));
}

void UHellunaLobbyLoginWidget::SwitchToSignupMode()
{
	bIsSignupMode = true;

	// 비밀번호 확인 입력란 + 라벨 표시 (회원가입 모드)
	if (ConfirmPasswordInputTextBox)
	{
		ConfirmPasswordInputTextBox->SetVisibility(ESlateVisibility::Visible);
		ConfirmPasswordInputTextBox->SetText(FText::GetEmpty());
	}
	if (Text_PWConfirmLabel)
	{
		Text_PWConfirmLabel->SetVisibility(ESlateVisibility::Visible);
	}

	// 버튼 라벨 = SIGN IN (_05 시안)
	if (Text_LoginLabel)
	{
		Text_LoginLabel->SetText(FText::FromString(TEXT("SIGN IN")));
	}

	// 링크 토글: 현재 회원가입 모드 → 로그인(LOGIN) 링크만 노출
	if (LoginTabButton)
	{
		LoginTabButton->SetVisibility(ESlateVisibility::Visible);
	}
	if (SignupTabButton)
	{
		SignupTabButton->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 메시지 초기화
	if (MessageText)
	{
		MessageText->SetText(FText::GetEmpty());
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyLoginWidget] 회원가입 모드로 전환"));
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

	AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(GetOwningPlayer());
	if (!LobbyPC)
	{
		ShowError(TEXT("서버 연결 오류"));
		return;
	}

	if (bIsSignupMode)
	{
		// ── 회원가입 모드 ──
		if (!ConfirmPasswordInputTextBox)
		{
			ShowError(TEXT("위젯 오류"));
			return;
		}

		const FString ConfirmPassword = ConfirmPasswordInputTextBox->GetText().ToString().TrimStartAndEnd();

		if (ConfirmPassword.IsEmpty())
		{
			ShowError(TEXT("비밀번호 확인을 입력해주세요."));
			return;
		}

		if (Password != ConfirmPassword)
		{
			ShowError(TEXT("비밀번호가 일치하지 않습니다."));
			return;
		}

		// 버튼 비활성화 (중복 클릭 방지)
		LoginButton->SetIsEnabled(false);

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyLoginWidget] Server_RequestLobbySignup 호출 | PlayerId=%s"), *PlayerId);
		LobbyPC->Server_RequestLobbySignup(PlayerId, Password);
	}
	else
	{
		// ── 로그인 모드 ──
		// 버튼 비활성화 (중복 클릭 방지)
		LoginButton->SetIsEnabled(false);

		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyLoginWidget] Server_RequestLobbyLogin 호출 | PlayerId=%s"), *PlayerId);
		LobbyPC->Server_RequestLobbyLogin(PlayerId, Password);
	}
}

void UHellunaLobbyLoginWidget::HandleSignupResult(bool bSuccess, const FString& Message)
{
	if (bSuccess)
	{
		// 회원가입 성공 → 성공 메시지 + 로그인 모드로 전환
		if (MessageText)
		{
			MessageText->SetText(FText::FromString(Message));
			MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
		}

		// 입력 필드 초기화
		if (PasswordInputTextBox)
		{
			PasswordInputTextBox->SetText(FText::GetEmpty());
		}
		if (ConfirmPasswordInputTextBox)
		{
			ConfirmPasswordInputTextBox->SetText(FText::GetEmpty());
		}

		// 로그인 모드로 전환 (ID는 유지)
		SwitchToLoginMode();

		// SwitchToLoginMode가 MessageText를 초기화하므로 다시 설정
		if (MessageText)
		{
			MessageText->SetText(FText::FromString(Message));
			MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
		}
	}
	else
	{
		ShowError(Message);
	}

	if (LoginButton)
	{
		LoginButton->SetIsEnabled(true);
	}
}

FString UHellunaLobbyLoginWidget::GetEnteredPlayerId() const
{
	return IDInputTextBox ? IDInputTextBox->GetText().ToString().TrimStartAndEnd() : FString();
}

FString UHellunaLobbyLoginWidget::GetEnteredPassword() const
{
	return PasswordInputTextBox ? PasswordInputTextBox->GetText().ToString().TrimStartAndEnd() : FString();
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
