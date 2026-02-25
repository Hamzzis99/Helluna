// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyStashWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 메인 듀얼 위젯 — Stash + Loadout 양쪽 패널 + 출격 버튼
//
// 📌 핵심 기능:
//   - InitializePanels(): StashComp → StashPanel, LoadoutComp → LoadoutPanel 바인딩
//   - TransferItemToLoadout/ToStash(): Server RPC를 통한 아이템 전송
//   - OnDeployClicked(): 출격 Server RPC 호출
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaLobbyStashWidget.h"
#include "Lobby/Widget/HellunaLobbyPanel.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Components/Button.h"

// 로그 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogHellunaLobby, Log, All);

// ════════════════════════════════════════════════════════════════════════════════
// NativeOnInitialized
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// ── 출격 버튼 바인딩 ──
	if (Button_Deploy)
	{
		Button_Deploy->OnClicked.AddDynamic(this, &ThisClass::OnDeployClicked);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 출격 버튼 바인딩 완료"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] NativeOnInitialized | StashPanel=%s | LoadoutPanel=%s | Button_Deploy=%s"),
		StashPanel ? TEXT("O") : TEXT("X"),
		LoadoutPanel ? TEXT("O") : TEXT("X"),
		Button_Deploy ? TEXT("O") : TEXT("X"));
}

// ════════════════════════════════════════════════════════════════════════════════
// InitializePanels — 양쪽 패널 바인딩
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::InitializePanels(UInv_InventoryComponent* StashComp, UInv_InventoryComponent* LoadoutComp)
{
	CachedStashComp = StashComp;
	CachedLoadoutComp = LoadoutComp;

	// ── Stash Panel 초기화 ──
	if (StashPanel && StashComp)
	{
		StashPanel->SetPanelTitle(FText::FromString(TEXT("STASH (창고)")));
		StashPanel->InitializeWithComponent(StashComp);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] StashPanel ← StashComp 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] StashPanel(%s) 또는 StashComp(%s) 누락!"),
			StashPanel ? TEXT("O") : TEXT("X"),
			StashComp ? TEXT("O") : TEXT("X"));
	}

	// ── Loadout Panel 초기화 ──
	if (LoadoutPanel && LoadoutComp)
	{
		LoadoutPanel->SetPanelTitle(FText::FromString(TEXT("LOADOUT (출격장비)")));
		LoadoutPanel->InitializeWithComponent(LoadoutComp);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] LoadoutPanel ← LoadoutComp 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] LoadoutPanel(%s) 또는 LoadoutComp(%s) 누락!"),
			LoadoutPanel ? TEXT("O") : TEXT("X"),
			LoadoutComp ? TEXT("O") : TEXT("X"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] InitializePanels 완료"));
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 SharedHoverItem 초기화 연결
}

// ════════════════════════════════════════════════════════════════════════════════
// TransferItemToLoadout — Stash → Loadout
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::TransferItemToLoadout(int32 ItemEntryIndex)
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] TransferToLoadout: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] TransferToLoadout → EntryIndex=%d"), ItemEntryIndex);
	LobbyPC->Server_TransferItem(ItemEntryIndex, ELobbyTransferDirection::StashToLoadout);
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
}

// ════════════════════════════════════════════════════════════════════════════════
// TransferItemToStash — Loadout → Stash
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::TransferItemToStash(int32 ItemEntryIndex)
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] TransferToStash: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] TransferToStash → EntryIndex=%d"), ItemEntryIndex);
	LobbyPC->Server_TransferItem(ItemEntryIndex, ELobbyTransferDirection::LoadoutToStash);
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
}

// ════════════════════════════════════════════════════════════════════════════════
// OnDeployClicked — 출격 버튼 클릭
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::OnDeployClicked()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] OnDeployClicked: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 출격 버튼 클릭! → Server_Deploy 호출"));
	LobbyPC->Server_Deploy();
}

// ════════════════════════════════════════════════════════════════════════════════
// GetLobbyController — 현재 LobbyController 가져오기
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyController* UHellunaLobbyStashWidget::GetLobbyController() const
{
	APlayerController* PC = GetOwningPlayer();
	return PC ? Cast<AHellunaLobbyController>(PC) : nullptr;
}
