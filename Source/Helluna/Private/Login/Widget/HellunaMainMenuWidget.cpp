#include "Login/Widget/HellunaMainMenuWidget.h"
#include "Login/Controller/HellunaServerConnectController.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Helluna.h"

void UHellunaMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (GameStartButton)
	{
		GameStartButton->OnClicked.AddUniqueDynamic(this, &UHellunaMainMenuWidget::OnGameStartClicked);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UHellunaMainMenuWidget::OnQuitClicked);
	}
}

void UHellunaMainMenuWidget::NativeDestruct()
{
	if (GameStartButton)
	{
		GameStartButton->OnClicked.RemoveDynamic(this, &UHellunaMainMenuWidget::OnGameStartClicked);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.RemoveDynamic(this, &UHellunaMainMenuWidget::OnQuitClicked);
	}

	Super::NativeDestruct();
}

void UHellunaMainMenuWidget::OnGameStartClicked()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (AHellunaServerConnectController* ConnectController = Cast<AHellunaServerConnectController>(PC))
	{
		// 서버접속창 먼저 표시 후 메뉴 제거 (제거 순서가 안전)
		ConnectController->ShowConnectWidget();
		ConnectController->HideMainMenu();
	}
	else
	{
		UE_LOG(LogHelluna, Error, TEXT("[MainMenuWidget] ServerConnectController 없음!"));
	}
}

void UHellunaMainMenuWidget::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}
