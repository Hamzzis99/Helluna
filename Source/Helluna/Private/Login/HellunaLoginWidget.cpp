// HellunaLoginWidget.cpp
// 로그인 UI 위젯 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#include "Login/HellunaLoginWidget.h"
#include "Login/HellunaLoginController.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Kismet/GameplayStatics.h"

void UHellunaLoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] ★ NativeConstruct 호출됨!"));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// ============================================
	// 📌 필수 위젯 체크
	// ============================================
	bool bHasError = false;

	if (!ServerConnectPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'ServerConnectPanel' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (!IPInputTextBox)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'IPInputTextBox' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (!ConnectButton)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'ConnectButton' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (!LoginPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'LoginPanel' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (!IDInputTextBox)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'IDInputTextBox' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (!PasswordInputTextBox)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'PasswordInputTextBox' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (!LoginButton)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'LoginButton' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (!MessageText)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginWidget] ❌ 'MessageText' 위젯이 없습니다!"));
		bHasError = true;
	}

	if (bHasError)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red, 
				TEXT("❌ [LoginWidget] 필수 위젯이 없습니다! Output Log를 확인해주세요."));
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
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] ConnectButton 바인딩 완료"));
	}

	if (LoginButton)
	{
		LoginButton->OnClicked.AddDynamic(this, &UHellunaLoginWidget::OnLoginButtonClicked);
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] LoginButton 바인딩 완료"));
	}

	// ============================================
	// 📌 [Phase B] 현재 맵에 따라 표시할 패널 결정
	// 
	// LoginLevel: ServerConnectPanel만 표시 (IP 입력)
	// GihyeonMap: LoginPanel만 표시 (로그인 입력)
	// 
	// Phase B에서는:
	// - LoginLevel에서 IP 접속만 하고 바로 GihyeonMap으로 이동
	// - GihyeonMap에서 로그인 UI 표시
	// ============================================
	ENetMode NetMode = GetWorld()->GetNetMode();
	FString MapName = GetWorld()->GetMapName();
	
	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] NetMode: %d"), static_cast<int32>(NetMode));
	UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] MapName: %s"), *MapName);

	// 맵 이름으로 판단
	if (MapName.Contains(TEXT("LoginLevel")))
	{
		// ============================================
		// 📌 [Phase B] LoginLevel → IP 접속 패널만!
		// ============================================
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] LoginLevel 감지 → ServerConnectPanel 표시"));
		
		if (NetMode == NM_Client)
		{
			// 이미 서버에 접속한 상태 → 대기 메시지
			UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] 이미 서버 접속됨 → GihyeonMap으로 이동 대기"));
			ShowMessage(TEXT("서버 접속 완료! 게임 맵으로 이동 중..."), false);
			SetLoadingState(true);
			
			// ServerConnectPanel 표시 (로딩 상태)
			ShowServerConnectPanel();
		}
		else
		{
			// 아직 서버에 접속 안 함 → IP 입력 패널
			ShowServerConnectPanel();
		}
	}
	else
	{
		// ============================================
		// 📌 [Phase B] GihyeonMap 또는 다른 맵 → 로그인 패널!
		// ============================================
		UE_LOG(LogTemp, Warning, TEXT("[LoginWidget] 게임 맵 감지 → LoginPanel 표시"));
		ShowLoginPanel();
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaLoginWidget::OnConnectButtonClicked()
{
	// ============================================
	// 📌 서버 접속 버튼 클릭
	// ============================================
	FString IPAddress = GetIPAddress();

	if (IPAddress.IsEmpty())
	{
		ShowMessage(TEXT("서버 IP를 입력해주세요."), true);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[LoginWidget] OnConnectButtonClicked: IP = %s"), *IPAddress);

	// 로딩 상태로 전환
	SetLoadingState(true);
	ShowMessage(TEXT("서버에 접속 중..."), false);

	// ============================================
	// 📌 서버 접속 시도
	// "Open IP:Port" 명령 실행
	// ============================================
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (PC)
	{
		// 콘솔 명령으로 서버 접속
		FString Command = FString::Printf(TEXT("open %s"), *IPAddress);
		PC->ConsoleCommand(Command);

		UE_LOG(LogTemp, Log, TEXT("[LoginWidget] 서버 접속 명령 실행: %s"), *Command);
	}
}

void UHellunaLoginWidget::OnLoginButtonClicked()
{
	// ============================================
	// 📌 로그인 버튼 클릭
	// ============================================
	FString PlayerId = GetPlayerId();
	FString Password = GetPassword();

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

	UE_LOG(LogTemp, Log, TEXT("[LoginWidget] OnLoginButtonClicked: ID = %s"), *PlayerId);

	// 로딩 상태로 전환
	SetLoadingState(true);
	ShowMessage(TEXT("로그인 중..."), false);

	// ============================================
	// 📌 LoginController에 로그인 요청
	// ============================================
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (AHellunaLoginController* LoginController = Cast<AHellunaLoginController>(PC))
	{
		LoginController->OnLoginButtonClicked(PlayerId, Password);
	}
	else
	{
		ShowMessage(TEXT("로그인 컨트롤러를 찾을 수 없습니다."), true);
		SetLoadingState(false);
	}
}

void UHellunaLoginWidget::ShowMessage(const FString& Message, bool bIsError)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));

		// 에러면 빨간색, 아니면 흰색
		if (bIsError)
		{
			MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else
		{
			MessageText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
	}
}

void UHellunaLoginWidget::ShowLoginPanel()
{
	// ============================================
	// 📌 로그인 패널 표시 (서버 접속 성공 후)
	// ============================================
	if (ServerConnectPanel)
	{
		ServerConnectPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (LoginPanel)
	{
		LoginPanel->SetVisibility(ESlateVisibility::Visible);
	}

	ShowMessage(TEXT(""), false);
	SetLoadingState(false);

	UE_LOG(LogTemp, Log, TEXT("[LoginWidget] ShowLoginPanel: 로그인 패널 표시"));
}

void UHellunaLoginWidget::ShowServerConnectPanel()
{
	// ============================================
	// 📌 서버 접속 패널 표시 (초기 상태)
	// ============================================
	if (ServerConnectPanel)
	{
		ServerConnectPanel->SetVisibility(ESlateVisibility::Visible);
	}

	if (LoginPanel)
	{
		LoginPanel->SetVisibility(ESlateVisibility::Collapsed);
	}

	ShowMessage(TEXT(""), false);
	SetLoadingState(false);

	UE_LOG(LogTemp, Log, TEXT("[LoginWidget] ShowServerConnectPanel: 서버 접속 패널 표시"));
}

void UHellunaLoginWidget::SetLoadingState(bool bLoading)
{
	// ============================================
	// 📌 로딩 상태에 따라 버튼 활성화/비활성화
	// ============================================
	if (ConnectButton)
	{
		ConnectButton->SetIsEnabled(!bLoading);
	}

	if (LoginButton)
	{
		LoginButton->SetIsEnabled(!bLoading);
	}
}

FString UHellunaLoginWidget::GetIPAddress() const
{
	if (IPInputTextBox)
	{
		return IPInputTextBox->GetText().ToString();
	}
	return TEXT("");
}

FString UHellunaLoginWidget::GetPlayerId() const
{
	if (IDInputTextBox)
	{
		return IDInputTextBox->GetText().ToString();
	}
	return TEXT("");
}

FString UHellunaLoginWidget::GetPassword() const
{
	if (PasswordInputTextBox)
	{
		return PasswordInputTextBox->GetText().ToString();
	}
	return TEXT("");
}
