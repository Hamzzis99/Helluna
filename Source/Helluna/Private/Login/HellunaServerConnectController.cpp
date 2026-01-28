#include "Login/HellunaServerConnectController.h"
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

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [ServerConnectController] BeginPlay                    ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ Controller: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("║ IsLocalController: %s"), IsLocalController() ? TEXT("TRUE") : TEXT("FALSE"));
	UE_LOG(LogTemp, Warning, TEXT("║ NetMode: %d"), static_cast<int32>(GetNetMode()));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	if (!ConnectWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[ServerConnectController] ConnectWidgetClass 미설정!"));
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

	UE_LOG(LogTemp, Warning, TEXT(""));
}

void AHellunaServerConnectController::ShowConnectWidget()
{
	UE_LOG(LogTemp, Warning, TEXT("[ServerConnectController] ShowConnectWidget"));

	if (!ConnectWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[ServerConnectController] ConnectWidgetClass가 nullptr!"));
		return;
	}

	if (!ConnectWidget)
	{
		ConnectWidget = CreateWidget<UHellunaServerConnectWidget>(this, ConnectWidgetClass);
	}

	if (ConnectWidget && !ConnectWidget->IsInViewport())
	{
		ConnectWidget->AddToViewport();
		UE_LOG(LogTemp, Warning, TEXT("[ServerConnectController] 위젯 표시됨"));
	}
}

void AHellunaServerConnectController::HideConnectWidget()
{
	if (ConnectWidget && ConnectWidget->IsInViewport())
	{
		ConnectWidget->RemoveFromParent();
		UE_LOG(LogTemp, Warning, TEXT("[ServerConnectController] 위젯 숨김"));
	}
}

void AHellunaServerConnectController::OnConnectButtonClicked(const FString& IPAddress)
{
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [ServerConnectController] OnConnectButtonClicked       ║"));
	UE_LOG(LogTemp, Warning, TEXT("╠════════════════════════════════════════════════════════════╣"));
	UE_LOG(LogTemp, Warning, TEXT("║ IP: '%s'"), *IPAddress);

	if (IPAddress.IsEmpty())
	{
		// 호스트 모드: 서버 시작
		UE_LOG(LogTemp, Warning, TEXT("║ → 호스트 모드: 서버 시작!                                  ║"));
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

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
			UE_LOG(LogTemp, Error, TEXT("[ServerConnectController] LoginGameMode 없음!"));
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
		UE_LOG(LogTemp, Warning, TEXT("║ → 클라이언트 모드: %s 에 접속!"), *IPAddress);
		UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

		if (ConnectWidget)
		{
			ConnectWidget->ShowMessage(FString::Printf(TEXT("%s 에 접속 중..."), *IPAddress), false);
			ConnectWidget->SetLoadingState(true);
		}

		FString Command = FString::Printf(TEXT("open %s"), *IPAddress);
		UE_LOG(LogTemp, Warning, TEXT("[ServerConnectController] 명령: %s"), *Command);
		ConsoleCommand(Command);
	}

	UE_LOG(LogTemp, Warning, TEXT(""));
}
