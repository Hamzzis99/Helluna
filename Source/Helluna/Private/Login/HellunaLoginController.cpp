// HellunaLoginController.cpp
// 로그인 레벨 전용 PlayerController 구현
// 
// ============================================
// 📌 작성자: Gihyeon
// 📌 작성일: 2025-01-23
// ============================================

#include "Login/HellunaLoginController.h"
#include "Login/HellunaLoginWidget.h"
#include "Login/HellunaLoginGameMode.h"
#include "Blueprint/UserWidget.h"

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

	// ============================================
	// 📌 필수 설정 체크
	// LoginWidgetClass가 설정되지 않으면 에러!
	// ============================================
	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] ❌ LoginWidgetClass가 설정되지 않았습니다! Blueprint에서 반드시 설정해주세요!"));
		
		// 에디터에서 경고 메시지 박스 표시
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, 
				TEXT("❌ [LoginController] LoginWidgetClass가 설정되지 않았습니다! Blueprint에서 설정해주세요!"));
		}
		return;
	}

	// ============================================
	// 📌 클라이언트에서만 UI 표시
	// 서버에서는 UI가 필요 없음
	// ============================================
	if (IsLocalController())
	{
		// 입력 모드를 UI + Game으로 설정
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		// 로그인 위젯 표시
		ShowLoginWidget();

		UE_LOG(LogTemp, Log, TEXT("[LoginController] BeginPlay: 로그인 UI 표시"));
	}
}

void AHellunaLoginController::ShowLoginWidget()
{
	// ============================================
	// 📌 로그인 위젯 생성 및 표시
	// ============================================
	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] ShowLoginWidget: LoginWidgetClass가 설정되지 않았습니다!"));
		return;
	}

	if (!LoginWidget)
	{
		LoginWidget = CreateWidget<UHellunaLoginWidget>(this, LoginWidgetClass);
	}

	if (LoginWidget && !LoginWidget->IsInViewport())
	{
		LoginWidget->AddToViewport();
		UE_LOG(LogTemp, Log, TEXT("[LoginController] ShowLoginWidget: 로그인 위젯 표시됨"));
	}
}

void AHellunaLoginController::HideLoginWidget()
{
	if (LoginWidget && LoginWidget->IsInViewport())
	{
		LoginWidget->RemoveFromParent();
		UE_LOG(LogTemp, Log, TEXT("[LoginController] HideLoginWidget: 로그인 위젯 숨김"));
	}
}

void AHellunaLoginController::OnLoginButtonClicked(const FString& PlayerId, const FString& Password)
{
	// ============================================
	// 📌 입력 유효성 검사
	// ============================================
	if (PlayerId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] OnLoginButtonClicked: 아이디가 비어있습니다."));
		// TODO: UI에 에러 메시지 표시
		return;
	}

	if (Password.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] OnLoginButtonClicked: 비밀번호가 비어있습니다."));
		// TODO: UI에 에러 메시지 표시
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[LoginController] OnLoginButtonClicked: 로그인 요청 - ID: %s"), *PlayerId);

	// 서버에 로그인 요청
	Server_RequestLogin(PlayerId, Password);
}

void AHellunaLoginController::Server_RequestLogin_Implementation(const FString& PlayerId, const FString& Password)
{
	// ============================================
	// 📌 서버에서 실행됨
	// GameMode에서 실제 검증 로직 수행
	// ============================================
	UE_LOG(LogTemp, Log, TEXT("[LoginController] Server_RequestLogin: 서버에서 로그인 요청 수신 - ID: %s"), *PlayerId);

	AHellunaLoginGameMode* LoginGameMode = Cast<AHellunaLoginGameMode>(GetWorld()->GetAuthGameMode());
	if (LoginGameMode)
	{
		LoginGameMode->ProcessLogin(this, PlayerId, Password);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] Server_RequestLogin: LoginGameMode를 찾을 수 없습니다!"));
		Client_LoginResult(false, TEXT("서버 오류: GameMode를 찾을 수 없습니다."));
	}
}

void AHellunaLoginController::Client_LoginResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
	// ============================================
	// 📌 클라이언트에서 실행됨
	// 로그인 결과에 따라 UI 업데이트
	// 
	// 성공 시: 성공 메시지 표시 → 맵 이동 대기
	// 실패 시: 에러 메시지 표시 → 재시도 가능
	// ============================================
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[LoginController] Client_LoginResult: 로그인 성공!"));

		// UI에 성공 메시지 표시
		if (LoginWidget)
		{
			LoginWidget->ShowMessage(TEXT("로그인 성공! 게임 맵으로 이동 중..."), false);
			LoginWidget->SetLoadingState(true);
		}

		// 맵 이동은 서버에서 처리 (ServerTravel)
		// 클라이언트는 대기 상태
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] Client_LoginResult: 로그인 실패 - %s"), *ErrorMessage);

		// UI에 에러 메시지 표시
		if (LoginWidget)
		{
			LoginWidget->ShowMessage(ErrorMessage, true);
			LoginWidget->SetLoadingState(false);
		}
	}
}
