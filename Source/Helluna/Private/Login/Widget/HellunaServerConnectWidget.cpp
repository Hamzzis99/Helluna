#include "Login/Widget/HellunaServerConnectWidget.h"
#include "Login/Controller/HellunaServerConnectController.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "Helluna.h"

void UHellunaServerConnectWidget::NativeConstruct()
{
	Super::NativeConstruct();

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [ServerConnectWidget] NativeConstruct                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
#endif

	bool bHasError = false;
#if HELLUNA_DEBUG_SERVERCONNECTION
	if (!IPInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] IPInputTextBox 없음!")); bHasError = true; }
	if (!ConnectButton) { UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] ConnectButton 없음!")); bHasError = true; }
	if (!MessageText) { UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] MessageText 없음!")); bHasError = true; }
#else
	if (!IPInputTextBox) { bHasError = true; }
	if (!ConnectButton) { bHasError = true; }
	if (!MessageText) { bHasError = true; }
#endif

	if (bHasError)
	{
#if HELLUNA_DEBUG_SERVERCONNECTION
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("[ServerConnectWidget] 필수 위젯 없음!"));
		}
#endif
		return;
	}

	if (ConnectButton)
	{
		ConnectButton->OnClicked.AddUniqueDynamic(this, &UHellunaServerConnectWidget::OnConnectButtonClicked);
	}

	ShowMessage(TEXT("IP 빈칸 → 호스트 / IP 입력 → 접속"), false);

#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogTemp, Warning, TEXT("[ServerConnectWidget] 초기화 완료"));
	UE_LOG(LogTemp, Warning, TEXT(""));
#endif
}

void UHellunaServerConnectWidget::OnConnectButtonClicked()
{
#if HELLUNA_DEBUG_SERVERCONNECTION
	UE_LOG(LogTemp, Warning, TEXT("[ServerConnectWidget] OnConnectButtonClicked"));
#endif

	FString IP = GetIPAddress();

	UWorld* World = GetWorld();
	if (!World) return;
	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (AHellunaServerConnectController* ConnectController = Cast<AHellunaServerConnectController>(PC))
	{
		ConnectController->OnConnectButtonClicked(IP);
	}
	else
	{
#if HELLUNA_DEBUG_SERVERCONNECTION
		UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] ServerConnectController 없음!"));
#endif
		ShowMessage(TEXT("Controller 오류!"), true);
	}
}

void UHellunaServerConnectWidget::ShowMessage(const FString& Message, bool bIsError)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
		MessageText->SetColorAndOpacity(FSlateColor(bIsError ? FLinearColor::Red : FLinearColor::White));
	}
}

void UHellunaServerConnectWidget::SetLoadingState(bool bLoading)
{
	if (ConnectButton)
	{
		ConnectButton->SetIsEnabled(!bLoading);
	}
}

FString UHellunaServerConnectWidget::GetIPAddress() const
{
	return IPInputTextBox ? IPInputTextBox->GetText().ToString() : TEXT("");
}
