// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyPanel.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 인벤토리 패널 — 3탭(장비/소모품/재료) Grid + WidgetSwitcher
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaLobbyPanel.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Player/Inv_PlayerController.h"

// 로그 카테고리
DECLARE_LOG_CATEGORY_EXTERN(LogHellunaLobby, Log, All);

// ════════════════════════════════════════════════════════════════════════════════
// NativeOnInitialized
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// ── 탭 버튼 바인딩 ──
	if (Button_Equippables)
	{
		Button_Equippables->OnClicked.AddDynamic(this, &ThisClass::ShowEquippables);
	}
	if (Button_Consumables)
	{
		Button_Consumables->OnClicked.AddDynamic(this, &ThisClass::ShowConsumables);
	}
	if (Button_Craftables)
	{
		Button_Craftables->OnClicked.AddDynamic(this, &ThisClass::ShowCraftables);
	}

	// 기본 탭: 장비
	if (Switcher && Grid_Equippables)
	{
		SetActiveGrid(Grid_Equippables, Button_Equippables);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] NativeOnInitialized 완료 | Grid_E=%s Grid_C=%s Grid_Cr=%s"),
		Grid_Equippables ? TEXT("O") : TEXT("X"),
		Grid_Consumables ? TEXT("O") : TEXT("X"),
		Grid_Craftables ? TEXT("O") : TEXT("X"));
}

// ════════════════════════════════════════════════════════════════════════════════
// InitializeWithComponent — 외부 InvComp 바인딩
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::InitializeWithComponent(UInv_InventoryComponent* InComp)
{
	if (!InComp)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel] InitializeWithComponent: InComp is nullptr!"));
		return;
	}

	BoundComponent = InComp;

	// ── 3개 Grid에 InvComp 수동 바인딩 ──
	// 각 Grid는 bSkipAutoInit=true 상태여야 함 (BP 디자이너에서 설정)
	if (Grid_Equippables)
	{
		Grid_Equippables->SetInventoryComponent(InComp);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] Grid_Equippables ← InvComp 바인딩 완료"));
	}
	if (Grid_Consumables)
	{
		Grid_Consumables->SetInventoryComponent(InComp);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] Grid_Consumables ← InvComp 바인딩 완료"));
	}
	if (Grid_Craftables)
	{
		Grid_Craftables->SetInventoryComponent(InComp);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] Grid_Craftables ← InvComp 바인딩 완료"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] InitializeWithComponent 완료 | InvComp=%s"),
		*InComp->GetName());
}

// ════════════════════════════════════════════════════════════════════════════════
// CollectAllGridItems — 3개 Grid에서 아이템 상태 수집
// ════════════════════════════════════════════════════════════════════════════════
TArray<FInv_SavedItemData> UHellunaLobbyPanel::CollectAllGridItems() const
{
	TArray<FInv_SavedItemData> AllItems;

	if (Grid_Equippables)
	{
		AllItems.Append(Grid_Equippables->CollectGridState());
	}
	if (Grid_Consumables)
	{
		AllItems.Append(Grid_Consumables->CollectGridState());
	}
	if (Grid_Craftables)
	{
		AllItems.Append(Grid_Craftables->CollectGridState());
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] CollectAllGridItems: 총 %d개 수집"), AllItems.Num());
	return AllItems;
}

// ════════════════════════════════════════════════════════════════════════════════
// SetPanelTitle — 패널 제목 변경
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::SetPanelTitle(const FText& InTitle)
{
	if (Text_PanelTitle)
	{
		Text_PanelTitle->SetText(InTitle);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 탭 전환 — ShowEquippables / ShowConsumables / ShowCraftables
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::ShowEquippables()
{
	SetActiveGrid(Grid_Equippables, Button_Equippables);
}

void UHellunaLobbyPanel::ShowConsumables()
{
	SetActiveGrid(Grid_Consumables, Button_Consumables);
}

void UHellunaLobbyPanel::ShowCraftables()
{
	SetActiveGrid(Grid_Craftables, Button_Craftables);
}

// ════════════════════════════════════════════════════════════════════════════════
// SetActiveGrid — 활성 탭 설정
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::SetActiveGrid(UInv_InventoryGrid* Grid, UButton* ActiveButton)
{
	if (!Switcher || !Grid)
	{
		return;
	}

	// WidgetSwitcher에서 해당 Grid의 인덱스로 전환
	const int32 GridIndex = Switcher->GetChildIndex(Grid);
	if (GridIndex != INDEX_NONE)
	{
		Switcher->SetActiveWidgetIndex(GridIndex);
	}

	ActiveGrid = Grid;

	// 버튼 상태 업데이트 (활성 탭 버튼 비활성화)
	if (Button_Equippables) Button_Equippables->SetIsEnabled(true);
	if (Button_Consumables) Button_Consumables->SetIsEnabled(true);
	if (Button_Craftables) Button_Craftables->SetIsEnabled(true);

	DisableButton(ActiveButton);
}

// ════════════════════════════════════════════════════════════════════════════════
// DisableButton — 현재 활성 탭 버튼 비활성화
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::DisableButton(UButton* Button)
{
	if (Button)
	{
		Button->SetIsEnabled(false);
	}
}
