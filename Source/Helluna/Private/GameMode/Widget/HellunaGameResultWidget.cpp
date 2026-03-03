// File: Source/Helluna/Private/GameMode/Widget/HellunaGameResultWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: 게임 결과 UI 위젯 구현 (BindWidget 패턴)
// ════════════════════════════════════════════════════════════════════════════════

#include "GameMode/Widget/HellunaGameResultWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Kismet/GameplayStatics.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"

void UHellunaGameResultWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 버튼 클릭 바인딩
	if (IsValid(Btn_ReturnToLobby))
	{
		Btn_ReturnToLobby->OnClicked.AddUniqueDynamic(this, &UHellunaGameResultWidget::OnReturnToLobbyClicked);
	}
}

void UHellunaGameResultWidget::SetResultData(
	const TArray<FInv_SavedItemData>& InResultItems,
	bool bInSurvived,
	const FString& InReason)
{
	ResultItems = InResultItems;
	bSurvived = bInSurvived;
	EndReason = InReason;

	UpdateUI();
}

void UHellunaGameResultWidget::ReturnToLobby()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameResultWidget] ReturnToLobby: OwningPlayer가 null!"));
		return;
	}

	// [Phase 12f] LobbyURL이 비어있으면 GameInstance에서 동적 구성
	if (LobbyURL.IsEmpty())
	{
		UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(PC->GetGameInstance());
		if (GI && !GI->ConnectedServerIP.IsEmpty())
		{
			LobbyURL = FString::Printf(TEXT("%s:%d"), *GI->ConnectedServerIP, GI->LobbyServerPort);
			UE_LOG(LogTemp, Log, TEXT("[GameResultWidget] LobbyURL 동적 구성: %s"), *LobbyURL);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[GameResultWidget] ReturnToLobby: LobbyURL이 비어있고 ConnectedServerIP도 없음!"));
			return;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[GameResultWidget] ReturnToLobby: ClientTravel → %s"), *LobbyURL);

	RemoveFromParent();
	PC->ClientTravel(LobbyURL, TRAVEL_Absolute);
}

void UHellunaGameResultWidget::OnReturnToLobbyClicked()
{
	ReturnToLobby();
}

void UHellunaGameResultWidget::UpdateUI()
{
	// 생존/사망 상태 텍스트
	if (IsValid(Text_SurvivalStatus))
	{
		if (bSurvived)
		{
			Text_SurvivalStatus->SetText(FText::FromString(TEXT("탈출 성공!")));
		}
		else
		{
			Text_SurvivalStatus->SetText(FText::FromString(TEXT("사망...")));
		}
	}

	// 종료 사유
	if (IsValid(Text_EndReason))
	{
		Text_EndReason->SetText(FText::FromString(EndReason));
	}

	// 아이템 개수
	if (IsValid(Text_ItemCount))
	{
		const FString ItemCountStr = FString::Printf(TEXT("보존 아이템: %d개"), ResultItems.Num());
		Text_ItemCount->SetText(FText::FromString(ItemCountStr));
	}
}
