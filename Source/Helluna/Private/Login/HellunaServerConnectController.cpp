#include "Login/HellunaServerConnectController.h"
#include "Helluna.h"  // 전처리기 플래그
#include "Login/HellunaServerConnectWidget.h"
#include "Login/HellunaLoginGameMode.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"

AHellunaServerConnectController::AHellunaServerConnectController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AHellunaServerConnectController::BeginPlay()
{
	Super::BeginPlay();

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [ServerConnectController] BeginPlay                    ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogHelluna, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogHelluna, Warning, TEXT("║ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	if (!ConnectWidgetClass)
	{
		UE_LOG(LogHelluna, Error, TEXT("[ServerConnectController] ConnectWidgetClass 미설정!"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("ConnectWidgetClass 미설정! BP에서 설정 필요"));
		}
		return;
	}

	if (IsLocalController())
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);

		ShowConnectWidget();
	}

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}

void AHellunaServerConnectController::ShowConnectWidget()
{
#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT("[ServerConnectController] ShowConnectWidget"));
#endif

	if (!ConnectWidgetClass)
	{
		UE_LOG(LogHelluna, Error, TEXT("[ServerConnectController] ConnectWidgetClass가 nullptr!"));
		return;
	}

	if (!ConnectWidget)
	{
		ConnectWidget = CreateWidget<UHellunaServerConnectWidget>(this, ConnectWidgetClass);
	}

	if (ConnectWidget && !ConnectWidget->IsInViewport())
	{
		ConnectWidget->AddToViewport();
#if HELLUNA_DEBUG_SERVERCONNECTION
		UE_LOG(LogHelluna, Warning, TEXT("[ServerConnectController] 위젯 표시됨"));
#endif
	}
}

void AHellunaServerConnectController::HideConnectWidget()
{
	if (ConnectWidget && ConnectWidget->IsInViewport())
	{
		ConnectWidget->RemoveFromParent();
#if HELLUNA_DEBUG_SERVERCONNECTION
		UE_LOG(LogHelluna, Warning, TEXT("[ServerConnectController] 위젯 숨김"));
#endif
	}
}

void AHellunaServerConnectController::OnConnectButtonClicked(const FString& IPAddress)
{
#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
	UE_LOG(LogHelluna, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogHelluna, Warning, TEXT("║     [ServerConnectController] OnConnectButtonClicked       ║"));
	UE_LOG(LogHelluna, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogHelluna, Warning, TEXT("║ IP: '%s'"), *IPAddress);
#endif

	if (IPAddress.IsEmpty())
	{
		// 호스트 모드: 서버 시작
#if HELLUNA_DEBUG_SERVERCONNECTION
		UE_LOG(LogHelluna, Warning, TEXT("║ → 호스트 모드: 서버 시작!                                  ║"));
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

		if (ConnectWidget)
		{
			ConnectWidget->ShowMessage(TEXT("서버 시작 중..."), false);
			ConnectWidget->SetLoadingState(true);
		}

		AHellunaLoginGameMode* GM = Cast<AHellunaLoginGameMode>(GetWorld()->GetAuthGameMode());
		if (GM)
		{
			GM->TravelToGameMap();
		}
		else
		{
			UE_LOG(LogHelluna, Error, TEXT("[ServerConnectController] LoginGameMode 없음!"));
			if (ConnectWidget)
			{
				ConnectWidget->ShowMessage(TEXT("GameMode 오류!"), true);
				ConnectWidget->SetLoadingState(false);
			}
		}
	}
	else
	{
		// 클라이언트 모드: 서버 접속
#if HELLUNA_DEBUG_SERVERCONNECTION
		UE_LOG(LogHelluna, Warning, TEXT("║ → 클라이언트 모드: %s 에 접속!"), *IPAddress);
		UE_LOG(LogHelluna, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

		if (ConnectWidget)
		{
			ConnectWidget->ShowMessage(FString::Printf(TEXT("%s 에 접속 중..."), *IPAddress), false);
			ConnectWidget->SetLoadingState(true);
		}

		FString Command = FString::Printf(TEXT("open %s"), *IPAddress);
#if HELLUNA_DEBUG_SERVERCONNECTION
		UE_LOG(LogHelluna, Warning, TEXT("[ServerConnectController] 명령: %s"), *Command);
#endif
		ConsoleCommand(Command);
	}

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}
