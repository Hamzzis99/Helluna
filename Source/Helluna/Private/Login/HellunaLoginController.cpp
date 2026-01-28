// HellunaLoginController.cpp
// 로그인 레벨 전용 PlayerController 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// 📌 수정일: 2025-01-28 (Phase B - 로그인 로직을 DefenseGameMode로 이동)
// 
// [Phase B 역할]:
// - IP 입력 UI만 담당
// - 로그인은 GihyeonMap(DefenseGameMode)에서 처리
// - ※ LoginGameMode::ProcessLogin()은 제거됨!
// ============================================

#include "Login/HellunaLoginController.h"
#include "Login/HellunaLoginWidget.h"
#include "Blueprint/UserWidget.h"

// ============================================
// 📌 [Phase B] LoginGameMode include 제거됨
// 로그인 로직이 DefenseGameMode로 이동했으므로
// LoginController에서는 더 이상 LoginGameMode 필요 없음
// ============================================

AHellunaLoginController::AHellunaLoginController()
{
	// 마우스 커서 표시 (UI 조작용)
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AHellunaLoginController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ★ BeginPlay 호출됨!"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	// ============================================
	// 📌 필수 설정 체크
	// ============================================
	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] ❌ LoginWidgetClass가 설정되지 않았습니다!"));
		
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, 
				TEXT("❌ [LoginController] LoginWidgetClass가 설정되지 않았습니다!"));
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ✅ LoginWidgetClass: %s"), *LoginWidgetClass->GetName());

	// ============================================
	// 📌 [Phase B] 클라이언트에서만 UI 표시
	// IP 입력 UI만 표시 (로그인은 GihyeonMap에서!)
	// ============================================
	if (IsLocalController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 로컬 컨트롤러 → UI 표시 시작"));

		// 입력 모드를 UI Only로 설정
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		// 로그인 위젯 표시
		ShowLoginWidget();

		UE_LOG(LogTemp, Warning, TEXT("[LoginController] ✅ UI 표시 완료"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 로컬 컨트롤러 아님 → UI 표시 안 함"));
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::ShowLoginWidget()
{
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ShowLoginWidget 호출됨"));

	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] ShowLoginWidget: LoginWidgetClass가 nullptr!"));
		return;
	}

	if (!LoginWidget)
	{
		LoginWidget = CreateWidget<UHellunaLoginWidget>(this, LoginWidgetClass);
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] ShowLoginWidget: 위젯 생성됨"));
	}

	if (LoginWidget && !LoginWidget->IsInViewport())
	{
		LoginWidget->AddToViewport();
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] ShowLoginWidget: ✅ 위젯이 Viewport에 추가됨"));
	}
}

void AHellunaLoginController::HideLoginWidget()
{
	if (LoginWidget && LoginWidget->IsInViewport())
	{
		LoginWidget->RemoveFromParent();
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] HideLoginWidget: 위젯 숨김"));
	}
}

void AHellunaLoginController::OnLoginButtonClicked(const FString& PlayerId, const FString& Password)
{
	// ============================================
	// 📌 [Phase B] LoginLevel에서는 로그인 하지 않음!
	// 
	// 이 함수가 호출된다는 것은:
	// - 사용자가 LoginLevel에서 로그인 버튼을 눌렀음
	// - Phase B에서는 허용되지 않음
	// 
	// 안내 메시지 표시하고 종료
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ OnLoginButtonClicked 호출됨!"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ Phase B: LoginLevel에서는 로그인하지 않습니다!"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ 먼저 서버에 접속하면 자동으로 게임 맵으로 이동합니다."));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// 사용자에게 안내 메시지 표시
	if (LoginWidget)
	{
		LoginWidget->ShowMessage(TEXT("먼저 서버에 접속해주세요! 게임 맵에서 로그인합니다."), true);
		LoginWidget->SetLoadingState(false);
	}

	// ============================================
	// 📌 [Phase B] Server RPC 호출하지 않음!
	// 로그인은 GihyeonMap에서만 가능
	// ============================================
}

void AHellunaLoginController::Server_RequestLogin_Implementation(const FString& PlayerId, const FString& Password)
{
	// ============================================
	// 📌 [Phase B] LoginLevel에서는 로그인 처리 안 함!
	// 
	// 이 함수가 호출되면 안 됨 (OnLoginButtonClicked에서 막음)
	// 혹시 호출되더라도 무시
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ Server_RequestLogin 호출됨 (서버)"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ Phase B: LoginLevel에서는 로그인을 처리하지 않습니다!"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ⚠️ 이 RPC는 호출되면 안 됩니다."));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));

	// 클라이언트에 에러 알림
	Client_LoginResult(false, TEXT("Phase B: LoginLevel에서는 로그인하지 않습니다. 서버 접속 후 게임 맵에서 로그인해주세요."));
}

void AHellunaLoginController::Client_LoginResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
	// ============================================
	// 📌 클라이언트에서 실행됨
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ★ Client_LoginResult 호출됨 (클라이언트)"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] bSuccess: %s"), bSuccess ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("========================================"));

	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] ✅ 성공!"));

		if (LoginWidget)
		{
			LoginWidget->ShowMessage(TEXT("성공! 게임 맵으로 이동 중..."), false);
			LoginWidget->SetLoadingState(true);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] ❌ 실패: %s"), *ErrorMessage);

		if (LoginWidget)
		{
			LoginWidget->ShowMessage(ErrorMessage, true);
			LoginWidget->SetLoadingState(false);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("========================================"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}
