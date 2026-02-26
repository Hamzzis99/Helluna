// File: Source/Helluna/Private/GameMode/Widget/HellunaGameResultWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
// Phase 7: 게임 결과 UI 위젯 구현
// ════════════════════════════════════════════════════════════════════════════════

#include "GameMode/Widget/HellunaGameResultWidget.h"
#include "Kismet/GameplayStatics.h"

void UHellunaGameResultWidget::SetResultData(
	const TArray<FInv_SavedItemData>& InResultItems,
	bool bInSurvived,
	const FString& InReason)
{
	ResultItems = InResultItems;
	bSurvived = bInSurvived;
	EndReason = InReason;

	// BP에서 UI 갱신 처리
	OnResultDataSet();
}

void UHellunaGameResultWidget::ReturnToLobby()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameResultWidget] ReturnToLobby: OwningPlayer가 null!"));
		return;
	}

	if (LobbyURL.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[GameResultWidget] ReturnToLobby: LobbyURL이 비어있음!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[GameResultWidget] ReturnToLobby: ClientTravel → %s"), *LobbyURL);

	// 위젯 제거 후 로비로 이동
	RemoveFromParent();
	PC->ClientTravel(LobbyURL, TRAVEL_Absolute);
}
