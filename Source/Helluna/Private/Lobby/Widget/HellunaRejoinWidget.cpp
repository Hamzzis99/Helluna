// ════════════════════════════════════════════════════════════════════════════════
// HellunaRejoinWidget.cpp — [Phase 14e] 게임 재참가 위젯
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaRejoinWidget.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

#include "Lobby/HellunaLobbyLog.h"

void UHellunaRejoinWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (Btn_Rejoin)
	{
		Btn_Rejoin->OnClicked.AddUniqueDynamic(this, &UHellunaRejoinWidget::OnRejoinClicked);
	}
	if (Btn_Abandon)
	{
		Btn_Abandon->OnClicked.AddUniqueDynamic(this, &UHellunaRejoinWidget::OnAbandonClicked);
	}
}

void UHellunaRejoinWidget::NativeDestruct()
{
	if (Btn_Rejoin)
	{
		Btn_Rejoin->OnClicked.RemoveDynamic(this, &UHellunaRejoinWidget::OnRejoinClicked);
	}
	if (Btn_Abandon)
	{
		Btn_Abandon->OnClicked.RemoveDynamic(this, &UHellunaRejoinWidget::OnAbandonClicked);
	}

	Super::NativeDestruct();
}

void UHellunaRejoinWidget::SetRejoinInfo(int32 Port)
{
	CachedPort = Port;
	if (Text_Message)
	{
		Text_Message->SetText(FText::FromString(
			FString::Printf(TEXT("진행 중인 게임이 있습니다.\n재참가하시겠습니까?\n(포트: %d)"), Port)));
	}
}

void UHellunaRejoinWidget::OnRejoinClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[RejoinWidget] 재참가 클릭 | Port=%d"), CachedPort);

	AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(GetOwningPlayer());
	if (LobbyPC)
	{
		LobbyPC->Server_RejoinGame();
	}

	RemoveFromParent();
}

void UHellunaRejoinWidget::OnAbandonClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[RejoinWidget] 포기 클릭 | Port=%d"), CachedPort);

	AHellunaLobbyController* LobbyPC = Cast<AHellunaLobbyController>(GetOwningPlayer());
	if (LobbyPC)
	{
		LobbyPC->Server_AbandonGame();
	}

	RemoveFromParent();
}
