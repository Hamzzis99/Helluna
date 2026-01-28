#include "Login/HellunaLoginController.h"
#include "Login/HellunaLoginWidget.h"
#include "GameMode/HellunaDefenseGameMode.h"
#include "GameFramework/PlayerState.h"
#include "Blueprint/UserWidget.h"

AHellunaLoginController::AHellunaLoginController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AHellunaLoginController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] BeginPlay                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ ControllerID: %d"), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("║ NetConnection: %s"), GetNetConnection() ? TEXT("Valid") : TEXT("nullptr"));
	
	APlayerState* PS = GetPlayerState<APlayerState>();
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerState: %s"), PS ? *PS->GetName() : TEXT("nullptr"));
	
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ LoginWidgetClass: %s"), LoginWidgetClass ? *LoginWidgetClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("║ GameControllerClass: %s"), GameControllerClass ? *GameControllerClass->GetName() : TEXT("미설정!"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] LoginWidgetClass 미설정!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("LoginWidgetClass 미설정! BP에서 설정 필요"));
		}
	}

	if (!GameControllerClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] GameControllerClass 미설정!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("GameControllerClass 미설정! BP에서 설정 필요"));
		}
	}

	if (IsLocalController() && LoginWidgetClass)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, this, &AHellunaLoginController::ShowLoginWidget, 0.3f, false);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::ShowLoginWidget()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("┌────────────────────────────────────────────────────────────┐"));
	UE_LOG(LogTemp, Warning, TEXT("│ [LoginController] ShowLoginWidget                          │"));
	UE_LOG(LogTemp, Warning, TEXT("└────────────────────────────────────────────────────────────┘"));

	if (!LoginWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] LoginWidgetClass가 nullptr!"));
		return;
	}

	if (!LoginWidget)
	{
		LoginWidget = CreateWidget<UHellunaLoginWidget>(this, LoginWidgetClass);
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 위젯 생성됨: %s"), LoginWidget ? *LoginWidget->GetName() : TEXT("실패"));
	}

	if (LoginWidget && !LoginWidget->IsInViewport())
	{
		LoginWidget->AddToViewport(100);
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 위젯 Viewport에 추가됨"));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::HideLoginWidget()
{
	UE_LOG(LogTemp, Warning, TEXT("[LoginController] HideLoginWidget"));

	if (LoginWidget && LoginWidget->IsInViewport())
	{
		LoginWidget->RemoveFromParent();
		UE_LOG(LogTemp, Warning, TEXT("[LoginController] 위젯 숨김"));
	}
}

void AHellunaLoginController::OnLoginButtonClicked(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] OnLoginButtonClicked                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("║ → Server_RequestLogin RPC 호출!                            ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (LoginWidget)
	{
		LoginWidget->ShowMessage(TEXT("로그인 중..."), false);
		LoginWidget->SetLoadingState(true);
	}

	Server_RequestLogin(PlayerId, Password);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::Server_RequestLogin_Implementation(const FString& PlayerId, const FString& Password)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] Server_RequestLogin (서버)           ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ PlayerId: '%s'"), *PlayerId);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("║ HasAuthority: %s"), HasAuthority() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ → DefenseGameMode::ProcessLogin 호출!                      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(GetWorld()->GetAuthGameMode());
	if (GM)
	{
		GM->ProcessLogin(this, PlayerId, Password);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[LoginController] DefenseGameMode 없음!"));
		Client_LoginResult(false, TEXT("서버 오류: GameMode를 찾을 수 없습니다."));
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::Client_LoginResult_Implementation(bool bSuccess, const FString& ErrorMessage)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] Client_LoginResult (클라이언트)      ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ bSuccess: %s"), bSuccess ? TEXT("TRUE ✅") : TEXT("FALSE ❌"));
	UE_LOG(LogTemp, Warning, TEXT("║ ErrorMessage: '%s'"), *ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s (ID: %d)"), *GetName(), GetUniqueID());
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	ShowLoginResult(bSuccess, ErrorMessage);

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::Client_PrepareControllerSwap_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [LoginController] Client_PrepareControllerSwap         ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller 교체 준비 중...                                 ║"));
	UE_LOG(LogTemp, Warning, TEXT("║ UI 정리 시작                                               ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	HideLoginWidget();

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;

	UE_LOG(LogTemp, Warning, TEXT("[LoginController] UI 정리 완료, Controller 교체 대기"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaLoginController::ShowLoginResult(bool bSuccess, const FString& Message)
{
	if (!LoginWidget) return;

	if (bSuccess)
	{
		LoginWidget->ShowMessage(TEXT("로그인 성공!"), false);
	}
	else
	{
		LoginWidget->ShowMessage(Message, true);
		LoginWidget->SetLoadingState(false);
	}
}
