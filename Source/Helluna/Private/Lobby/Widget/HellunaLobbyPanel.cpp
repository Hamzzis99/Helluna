// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyPanel.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// ============================================================================
// 📌 Phase 4 Step 4-3: 로비 인벤토리 패널 (단일 패널)
// ============================================================================
//
// 📌 구조:
//   ┌──────────────────────────────────┐
//   │ [장비] [소모품] [재료]  ← 탭 버튼 │
//   │ ┌──────────────────────────────┐ │
//   │ │                              │ │
//   │ │    Grid (WidgetSwitcher)     │ │
//   │ │                              │ │
//   │ └──────────────────────────────┘ │
//   └──────────────────────────────────┘
//
// 📌 역할:
//   - Stash 패널 또는 Loadout 패널 한쪽을 담당
//   - 3개 Grid (장비/소모품/재료)를 WidgetSwitcher로 탭 전환
//   - InitializeWithComponent()로 외부 InvComp와 바인딩
//
// 📌 기존 Inv_SpatialInventory와의 차이:
//   - 장착 슬롯(EquippedGridSlot) 없음 → 로비에서는 장착 불가
//   - 아이템 설명(ItemDescription) 없음 → 간소화
//   - SetInventoryComponent()로 수동 바인딩 (bSkipAutoInit=true 필수!)
//
// 📌 BP 위젯 생성 시 주의사항 (WBP_HellunaLobbyPanel):
//   1. Grid_Equippables, Grid_Consumables, Grid_Craftables → Inv_InventoryGrid 위젯
//   2. 각 Grid의 bSkipAutoInit = true (Details 패널에서 체크!)
//   3. Switcher → WidgetSwitcher (3개 Grid를 자식으로)
//   4. Button_Equippables/Consumables/Craftables → Button 위젯
//   5. Text_PanelTitle → TextBlock (선택적, 없어도 동작)
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
// NativeOnInitialized — 위젯 생성 시 초기화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: CreateWidget → 위젯 트리 구성 완료 후
// 📌 역할: 탭 버튼 이벤트 바인딩 + 기본 탭(장비) 활성화
//
// 📌 주의: 이 시점에서는 아직 InvComp와 바인딩되지 않은 상태!
//    → InitializeWithComponent()가 나중에 호출되어야 Grid에 아이템이 표시됨
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] NativeOnInitialized 시작"));

	// ── BindWidget 상태 진단 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Grid_Equippables=%s"), Grid_Equippables ? TEXT("바인딩됨") : TEXT("⚠ nullptr (BindWidget 확인!)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Grid_Consumables=%s"), Grid_Consumables ? TEXT("바인딩됨") : TEXT("⚠ nullptr (BindWidget 확인!)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Grid_Craftables=%s"), Grid_Craftables ? TEXT("바인딩됨") : TEXT("⚠ nullptr (BindWidget 확인!)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Switcher=%s"), Switcher ? TEXT("바인딩됨") : TEXT("⚠ nullptr (BindWidget 확인!)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Button_Equippables=%s"), Button_Equippables ? TEXT("O") : TEXT("X"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Button_Consumables=%s"), Button_Consumables ? TEXT("O") : TEXT("X"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Button_Craftables=%s"), Button_Craftables ? TEXT("O") : TEXT("X"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Text_PanelTitle=%s (선택적)"), Text_PanelTitle ? TEXT("O") : TEXT("X"));

	// ── 탭 버튼 OnClicked 이벤트 바인딩 ──
	if (Button_Equippables)
	{
		Button_Equippables->OnClicked.AddDynamic(this, &ThisClass::ShowEquippables);
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPanel]   Button_Equippables → ShowEquippables 바인딩"));
	}
	if (Button_Consumables)
	{
		Button_Consumables->OnClicked.AddDynamic(this, &ThisClass::ShowConsumables);
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPanel]   Button_Consumables → ShowConsumables 바인딩"));
	}
	if (Button_Craftables)
	{
		Button_Craftables->OnClicked.AddDynamic(this, &ThisClass::ShowCraftables);
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPanel]   Button_Craftables → ShowCraftables 바인딩"));
	}

	// ── 기본 활성 탭: 장비 ──
	if (Switcher && Grid_Equippables)
	{
		SetActiveGrid(Grid_Equippables, Button_Equippables);
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   기본 탭 '장비' 활성화 완료"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel]   기본 탭 설정 실패! Switcher=%s, Grid_E=%s"),
			Switcher ? TEXT("O") : TEXT("X"), Grid_Equippables ? TEXT("O") : TEXT("X"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] NativeOnInitialized 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// InitializeWithComponent — 외부 InvComp와 3개 Grid 바인딩
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 경로:
//   LobbyStashWidget::InitializePanels()
//     → StashPanel->InitializeWithComponent(StashComp)
//     → LoadoutPanel->InitializeWithComponent(LoadoutComp)
//
// 📌 내부 동작:
//   1) BoundComponent 캐시 (나중에 CollectAllGridItems에서 사용)
//   2) 3개 Grid에 SetInventoryComponent(InComp) 호출
//      → 각 Grid는 bSkipAutoInit=true 상태여야 함!
//      → SetInventoryComponent는 기존 바인딩 해제 → 새 바인딩 설정
//      → OnItemAdded, OnItemRemoved, OnStackChange 등 델리게이트 연결
//
// 📌 주의:
//   Grid의 bSkipAutoInit가 false이면 NativeOnInitialized에서 자동 바인딩이 실행됨
//   → 잘못된 InvComp(첫 번째 것만)에 바인딩될 수 있음!
//   → 반드시 BP 디자이너에서 각 Grid의 bSkipAutoInit = true 체크!
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::InitializeWithComponent(UInv_InventoryComponent* InComp)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] InitializeWithComponent 시작 | InComp=%s"),
		InComp ? *InComp->GetName() : TEXT("nullptr"));

	if (!InComp)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel] InitializeWithComponent: InComp is nullptr!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel]   → LobbyController의 StashComp/LoadoutComp가 생성되었는지 확인"));
		return;
	}

	BoundComponent = InComp;

	// ── 3개 Grid에 InvComp 수동 바인딩 ──
	// 각 Grid의 SetInventoryComponent()는 기존 델리게이트 해제 → 새 델리게이트 바인딩 수행
	int32 BindCount = 0;
	if (Grid_Equippables)
	{
		Grid_Equippables->SetInventoryComponent(InComp);
		++BindCount;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Grid_Equippables ← InvComp 바인딩 완료 (카테고리: 장비)"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel]   Grid_Equippables가 nullptr! → BindWidget 확인 필요"));
	}

	if (Grid_Consumables)
	{
		Grid_Consumables->SetInventoryComponent(InComp);
		++BindCount;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Grid_Consumables ← InvComp 바인딩 완료 (카테고리: 소모품)"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel]   Grid_Consumables가 nullptr! → BindWidget 확인 필요"));
	}

	if (Grid_Craftables)
	{
		Grid_Craftables->SetInventoryComponent(InComp);
		++BindCount;
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel]   Grid_Craftables ← InvComp 바인딩 완료 (카테고리: 재료)"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel]   Grid_Craftables가 nullptr! → BindWidget 확인 필요"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] InitializeWithComponent 완료 | InvComp=%s | 바인딩된 Grid=%d/3"),
		*InComp->GetName(), BindCount);
}

// ════════════════════════════════════════════════════════════════════════════════
// CollectAllGridItems — 3개 Grid의 모든 아이템 상태를 수집 (저장용)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 용도: Deploy(출격) 시 또는 Logout 시 현재 패널의 아이템을 SQLite에 저장하기 위해 수집
//    현재는 Server_Deploy에서 InvComp->CollectInventoryDataForSave()를 직접 사용하므로
//    이 함수는 추후 Grid 위치 정보까지 포함한 저장이 필요할 때 사용
//
// ════════════════════════════════════════════════════════════════════════════════
TArray<FInv_SavedItemData> UHellunaLobbyPanel::CollectAllGridItems() const
{
	TArray<FInv_SavedItemData> AllItems;

	if (Grid_Equippables)
	{
		const int32 Before = AllItems.Num();
		AllItems.Append(Grid_Equippables->CollectGridState());
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] CollectAll: 장비 Grid → %d개"), AllItems.Num() - Before);
	}
	if (Grid_Consumables)
	{
		const int32 Before = AllItems.Num();
		AllItems.Append(Grid_Consumables->CollectGridState());
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] CollectAll: 소모품 Grid → %d개"), AllItems.Num() - Before);
	}
	if (Grid_Craftables)
	{
		const int32 Before = AllItems.Num();
		AllItems.Append(Grid_Craftables->CollectGridState());
		UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] CollectAll: 재료 Grid → %d개"), AllItems.Num() - Before);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] CollectAllGridItems 완료 | 총 %d개 수집"), AllItems.Num());
	return AllItems;
}

// ════════════════════════════════════════════════════════════════════════════════
// SetPanelTitle — 패널 상단 제목 텍스트 변경
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 "STASH (창고)" 또는 "LOADOUT (출격장비)" 를 표시
// 📌 Text_PanelTitle은 BindWidgetOptional이므로 없어도 크래시 안 남
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::SetPanelTitle(const FText& InTitle)
{
	if (Text_PanelTitle)
	{
		Text_PanelTitle->SetText(InTitle);
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPanel] 패널 제목 설정: '%s'"), *InTitle.ToString());
	}
	else
	{
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPanel] Text_PanelTitle 없음 → 제목 표시 스킵 (BindWidgetOptional)"));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 탭 전환 — ShowEquippables / ShowConsumables / ShowCraftables
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 탭 버튼 클릭 시 호출 (NativeOnInitialized에서 OnClicked에 바인딩됨)
// 📌 WidgetSwitcher의 ActiveWidget을 해당 Grid로 변경
// 📌 현재 활성 탭 버튼은 비활성화 (시각적 피드백)
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::ShowEquippables()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] 탭 전환 → 장비"));
	SetActiveGrid(Grid_Equippables, Button_Equippables);
}

void UHellunaLobbyPanel::ShowConsumables()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] 탭 전환 → 소모품"));
	SetActiveGrid(Grid_Consumables, Button_Consumables);
}

void UHellunaLobbyPanel::ShowCraftables()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[LobbyPanel] 탭 전환 → 재료"));
	SetActiveGrid(Grid_Craftables, Button_Craftables);
}

// ════════════════════════════════════════════════════════════════════════════════
// SetActiveGrid — 활성 Grid/탭 설정 (내부 헬퍼)
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 WidgetSwitcher.SetActiveWidgetIndex로 보이는 Grid 변경
// 📌 모든 탭 버튼 활성화 → 현재 탭 버튼만 비활성화 (선택된 탭 표시)
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::SetActiveGrid(UInv_InventoryGrid* Grid, UButton* ActiveButton)
{
	if (!Switcher || !Grid)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel] SetActiveGrid: Switcher=%s, Grid=%s"),
			Switcher ? TEXT("O") : TEXT("X"), Grid ? TEXT("O") : TEXT("X"));
		return;
	}

	// WidgetSwitcher에서 해당 Grid의 인덱스로 전환
	const int32 GridIndex = Switcher->GetChildIndex(Grid);
	if (GridIndex != INDEX_NONE)
	{
		Switcher->SetActiveWidgetIndex(GridIndex);
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[LobbyPanel] Switcher → Index %d"), GridIndex);
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[LobbyPanel] SetActiveGrid: Grid가 Switcher의 자식이 아님!"));
	}

	ActiveGrid = Grid;

	// 모든 버튼 활성화 → 현재 탭만 비활성화
	if (Button_Equippables) Button_Equippables->SetIsEnabled(true);
	if (Button_Consumables) Button_Consumables->SetIsEnabled(true);
	if (Button_Craftables) Button_Craftables->SetIsEnabled(true);

	DisableButton(ActiveButton);
}

// ════════════════════════════════════════════════════════════════════════════════
// DisableButton — 현재 활성 탭 버튼 비활성화 (시각적 피드백)
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyPanel::DisableButton(UButton* Button)
{
	if (Button)
	{
		Button->SetIsEnabled(false);
	}
}
