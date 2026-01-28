// HellunaLoginWidget.cpp
// 로그인 UI 위젯 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// 📌 수정일: 2025-01-28 (Phase B)
// ============================================

#include "Login/HellunaLoginWidget.h"
#include "Login/HellunaLoginController.h"
#include "Login/HellunaLoginGameMode.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Kismet/GameplayStatics.h"

// ============================================
// 📌 [Phase B] Inv_PlayerController include
// GihyeonMap에서 로그인 시 사용
// ============================================
#include "Player/Inv_PlayerController.h"

void UHellunaLoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [LoginWidget] NativeConstruct                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	// ============================================
	// 📌 필수 위젯 체크
	// ============================================
	bool bHasError = false;

	if (!ServerConnectPanel) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'ServerConnectPanel' 없음!")); bHasError = true; }
	if (!IPInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'IPInputTextBox' 없음!")); bHasError = true; }
	if (!ConnectButton) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'ConnectButton' 없음!")); bHasError = true; }
	if (!LoginPanel) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'LoginPanel' 없음!")); bHasError = true; }
	if (!IDInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'IDInputTextBox' 없음!")); bHasError = true; }
	if (!PasswordInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'PasswordInputTextBox' 없음!")); bHasError = true; }
	if (!LoginButton) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'LoginButton' 없음!")); bHasError = true; }
	if (!MessageText) { UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'MessageText' 없음!")); bHasError = true; }

	if (bHasError)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red, 
				TEXT("❌ [LoginWidget] 필수 위젯이 없습니다!"));
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] ✅ 모든 필수 위젯 확인 완료"));

	// ============================================
	// 📌 버튼 클릭 이벤트 바인딩
	// ============================================
	if (ConnectButton)
	{
		ConnectButton->OnClicked.AddDynamic(this, &UHellunaLoginWidget::OnConnectButtonClicked);
	}

	if (LoginButton)
	{
		LoginButton->OnClicked.AddDynamic(this, &UHellunaLoginWidget::OnLoginButtonClicked);
	}

	// ============================================
	// 📌 [Phase B] 맵에 따라 패널 결정
	// ============================================
	ENetMode NetMode = GetWorld()->GetNetMode();
	FString MapName = GetWorld()->GetMapName();
	
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ NetMode: %d"), static_cast<int32>(NetMode));
	UE_LOG(LogTemp, Warning, TEXT("│ MapName: %s"), *MapName);
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));

	if (MapName.Contains(TEXT("LoginLevel")))
	{
		// LoginLevel → IP 접속 패널
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] → ServerConnectPanel 표시"));
		ShowServerConnectPanel();
		
		// 사용법 안내
		ShowMessage(TEXT("IP 빈칸→서버시작 / IP 입력→서버접속"), false);
	}
	else
	{
		// 게임 맵 → 로그인 패널
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] → LoginPanel 표시"));
		ShowLoginPanel();
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaLoginWidget::OnConnectButtonClicked()
{
	// ============================================
	// 📌 [Phase B] 접속/시작 버튼 클릭
	// 
	// IP가 비어있으면 → 호스트로 서버 시작
	// IP가 있으면 → 클라이언트로 서버 접속
	// ============================================
	
	FString IPAddress = GetIPAddress();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [LoginWidget] OnConnectButtonClicked               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ IP 입력값: '%s'"), *IPAddress);

	if (IPAddress.IsEmpty())
	{
		// ============================================
		// 📌 [호스트 모드] IP가 비어있으면 서버 시작!
		// ============================================
		UE_LOG(LogTemp, Warning, TEXT("║ → 호스트 모드: 서버 시작!                                  ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		
		SetLoadingState(true);
		ShowMessage(TEXT("서버 시작 중..."), false);

		// GameMode의 TravelToGameMap 호출
		AHellunaLoginGameMode* GameMode = Cast<AHellunaLoginGameMode>(GetWorld()->GetAuthGameMode());
		if (GameMode)
		{
			UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] GameMode->TravelToGameMap() 호출"));
			GameMode->TravelToGameMap();
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ LoginGameMode를 찾을 수 없습니다!"));
			ShowMessage(TEXT("GameMode를 찾을 수 없습니다!"), true);
			SetLoadingState(false);
		}
	}
	else
	{
		// ============================================
		// 📌 [클라이언트 모드] IP가 있으면 서버 접속!
		// ============================================
		UE_LOG(LogTemp, Warning, TEXT("║ → 클라이언트 모드: %s 에 접속!"), *IPAddress);
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
		
		SetLoadingState(true);
		ShowMessage(FString::Printf(TEXT("%s 에 접속 중..."), *IPAddress), false);

		// open IP 명령 실행
		APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
		if (PC)
		{
			FString Command = FString::Printf(TEXT("open %s"), *IPAddress);
			UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] 명령 실행: %s"), *Command);
			PC->ConsoleCommand(Command);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaLoginWidget::OnLoginButtonClicked()
{
	// ============================================
	// 📌 [Phase B] 로그인 버튼 클릭
	// GihyeonMap에서만 사용됨
	// ============================================
	FString PlayerId = GetPlayerId();
	FString Password = GetPassword();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║         [LoginWidget] OnLoginButtonClicked                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (PlayerId.IsEmpty())
	{
		ShowMessage(TEXT("아이디를 입력해주세요."), true);
		return;
	}

	if (Password.IsEmpty())
	{
		ShowMessage(TEXT("비밀번호를 입력해주세요."), true);
		return;
	}

	SetLoadingState(true);
	ShowMessage(TEXT("로그인 중..."), false);

	// ============================================
	// 📌 [Phase B] 현재 맵 확인
	// ============================================
	FString MapName = GetWorld()->GetMapName();
	
	if (MapName.Contains(TEXT("LoginLevel")))
	{
		// LoginLevel에서는 로그인 불가
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] ⚠️ LoginLevel에서는 로그인 불가!"));
		ShowMessage(TEXT("먼저 서버에 접속/시작 해주세요!"), true);
		SetLoadingState(false);
		return;
	}

	// ============================================
	// 📌 [Phase B] GihyeonMap에서 로그인
	// Inv_PlayerController의 OnLoginButtonClicked 호출
	// → Server_RequestLogin RPC 실행
	// ============================================
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC))
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] → Inv_PlayerController->OnLoginButtonClicked 호출"));
		InvPC->OnLoginButtonClicked(PlayerId, Password);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ Inv_PlayerController를 찾을 수 없습니다!"));
		ShowMessage(TEXT("PlayerController를 찾을 수 없습니다!"), true);
		SetLoadingState(false);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaLoginWidget::ShowMessage(const FString& Message, bool bIsError)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FSlateColor(bIsError ? FLinearColor::Red : FLinearColor::White));
	}
}

void UHellunaLoginWidget::ShowLoginPanel()
{
	if (ServerConnectPanel) ServerConnectPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (LoginPanel) LoginPanel->SetVisibility(ESlateVisibility::Visible);
	ShowMessage(TEXT(""), false);
	SetLoadingState(false);
}

void UHellunaLoginWidget::ShowServerConnectPanel()
{
	if (ServerConnectPanel) ServerConnectPanel->SetVisibility(ESlateVisibility::Visible);
	if (LoginPanel) LoginPanel->SetVisibility(ESlateVisibility::Collapsed);
	SetLoadingState(false);
}

void UHellunaLoginWidget::SetLoadingState(bool bLoading)
{
	if (ConnectButton) ConnectButton->SetIsEnabled(!bLoading);
	if (LoginButton) LoginButton->SetIsEnabled(!bLoading);
}

FString UHellunaLoginWidget::GetIPAddress() const
{
	return IPInputTextBox ? IPInputTextBox->GetText().ToString() : TEXT("");
}

FString UHellunaLoginWidget::GetPlayerId() const
{
	return IDInputTextBox ? IDInputTextBox->GetText().ToString() : TEXT("");
}

FString UHellunaLoginWidget::GetPassword() const
{
	return PasswordInputTextBox ? PasswordInputTextBox->GetText().ToString() : TEXT("");
}
