#include "Login/Widget/HellunaServerConnectWidget.h"
#include "Login/Controller/HellunaServerConnectController.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"

void UHellunaServerConnectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT(""));
	UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
	UE_LOG(LogTemp, Warning, TEXT("║     [ServerConnectWidget] NativeConstruct                  ║"));
	UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));

	bool bHasError = false;
	if (!IPInputTextBox) { UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] IPInputTextBox 없음!")); bHasError = true; }
	if (!ConnectButton) { UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] ConnectButton 없음!")); bHasError = true; }
	if (!MessageText) { UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] MessageText 없음!")); bHasError = true; }

	if (bHasError)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
				TEXT("[ServerConnectWidget] 필수 위젯 없음!"));
		}
		return;
	}

	if (ConnectButton)
	{
		ConnectButton->OnClicked.AddDynamic(this, &UHellunaServerConnectWidget::OnConnectButtonClicked);
	}

	ShowMessage(TEXT("IP 빈칸 → 호스트 / IP 입력 → 접속"), false);

	UE_LOG(LogTemp, Warning, TEXT("[ServerConnectWidget] 초기화 완료"));
	UE_LOG(LogTemp, Warning, TEXT(""));
}

void UHellunaServerConnectWidget::OnConnectButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("[ServerConnectWidget] OnConnectButtonClicked"));

	FString IP = GetIPAddress();

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (AHellunaServerConnectController* ConnectController = Cast<AHellunaServerConnectController>(PC))
	{
		ConnectController->OnConnectButtonClicked(IP);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ServerConnectWidget] ServerConnectController 없음!"));
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
