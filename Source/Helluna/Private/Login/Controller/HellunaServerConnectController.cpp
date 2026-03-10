#include "Login/Controller/HellunaServerConnectController.h"
#include "Helluna.h"  // 전처리기 플래그
#include "Login/Widget/HellunaServerConnectWidget.h"
#include "Login/GameMode/HellunaLoginGameMode.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
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
#if HELLUNA_DEBUG_SERVERCONNECTION
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("ConnectWidgetClass 미설정! BP에서 설정 필요"));
		}
#endif
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

		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance()))
		{
			GI->ConnectedServerIP = TEXT("127.0.0.1");
			UE_LOG(LogHelluna, Log, TEXT("[ServerConnectController] Host 모드 ConnectedServerIP 저장: %s"), *GI->ConnectedServerIP);
			GI->ShowLoadingScreen(TEXT("서버 시작 중..."));
		}

		UWorld* World = GetWorld();
		if (!World) return;
		AHellunaLoginGameMode* GM = Cast<AHellunaLoginGameMode>(World->GetAuthGameMode());
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

		// [Phase 12c] 접속 IP 저장 (포트 제거) — Deploy/로비 복귀에 재사용
		if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(GetGameInstance()))
		{
			FString IP = IPAddress;
			int32 ColonIdx;
			if (IP.FindChar(TEXT(':'), ColonIdx))
			{
				IP = IP.Left(ColonIdx);
			}
			GI->ConnectedServerIP = IP;
			UE_LOG(LogHelluna, Log, TEXT("[ServerConnectController] ConnectedServerIP 저장: %s"), *IP);

			// 로딩 화면 표시 (open 명령 전)
			GI->ShowLoadingScreen(TEXT("서버 접속 중..."));
		}

		// IP 형식 검증 - 위험 문자 차단
		FString SafeIP = IPAddress;
		SafeIP.ReplaceInline(TEXT(";"), TEXT(""));
		SafeIP.ReplaceInline(TEXT("\""), TEXT(""));
		SafeIP.ReplaceInline(TEXT("'"), TEXT(""));
		SafeIP.ReplaceInline(TEXT("|"), TEXT(""));
		SafeIP.ReplaceInline(TEXT("&"), TEXT(""));
		if (SafeIP.IsEmpty()) return;

		FString Command = FString::Printf(TEXT("open %s"), *SafeIP);
#if HELLUNA_DEBUG_SERVERCONNECTION
		UE_LOG(LogHelluna, Warning, TEXT("[ServerConnectController] 명령: %s"), *Command);
#endif
		ConsoleCommand(Command);
	}

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogHelluna, Warning, TEXT(""));
#endif
}
