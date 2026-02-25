// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyStashWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// ============================================================================
// 📌 Phase 4 Step 4-4: 로비 메인 듀얼 위젯
// ============================================================================
//
// 📌 레이아웃:
//   ┌─── StashPanel (좌) ─────┐  ┌── LoadoutSpatialInventory (우) ──┐
//   │ [장비][소모품][재료]     │  │  [장착슬롯: 무기/방어구/...]     │
//   │ ┌─────────────────────┐ │  │  ┌──────────────────────────┐    │
//   │ │     Grid (탭별)     │ │  │  │ [장비][소모품][재료]      │    │
//   │ └─────────────────────┘ │  │  │   Grid (탭별) + 장착슬롯  │    │
//   └─────────────────────────┘  │  └──────────────────────────┘    │
//                                └──────────────────────────────────┘
//                      [ 출격 버튼 ]
//
// 📌 역할:
//   - 상위 컨테이너: StashPanel + LoadoutPanel 양쪽을 소유하고 초기화
//   - 아이템 전송: TransferItemToLoadout/ToStash → Server RPC 호출
//   - 출격: OnDeployClicked → Server_Deploy RPC 호출
//
// 📌 BP 위젯 생성 시 주의사항 (WBP_HellunaLobbyStashWidget):
//   1. StashPanel → UHellunaLobbyPanel 위젯 (WBP_HellunaLobbyPanel 인스턴스)
//   2. LoadoutSpatialInventory → UInv_SpatialInventory 위젯 (WBP_Inv_SpatialInventory 인스턴스)
//      → 내부 Grid 3개의 bSkipAutoInit = true 설정 필수!
//   3. Button_Deploy → Button 위젯
//   4. 세 위젯 모두 BindWidget 이름이 정확히 일치해야 함!
//
// 📌 아이템 전송 경로 (클라→서버):
//   UI 버튼 클릭 → TransferItemToLoadout(EntryIndex)
//     → GetLobbyController() → LobbyPC->Server_TransferItem(EntryIndex, Direction)
//     → 서버: ExecuteTransfer → TransferItemTo (FastArray 조작)
//     → FastArray Mark/Dirty → 리플리케이션 → 클라이언트 Grid 자동 업데이트
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#include "Lobby/Widget/HellunaLobbyStashWidget.h"
#include "Lobby/Widget/HellunaLobbyPanel.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Components/Button.h"

// 로그 카테고리 (공유 헤더 — DEFINE은 HellunaLobbyGameMode.cpp)
#include "Lobby/HellunaLobbyLog.h"

// ════════════════════════════════════════════════════════════════════════════════
// NativeOnInitialized — 위젯 생성 시 초기화
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: CreateWidget 후 위젯 트리 구성 완료
// 📌 역할: 출격 버튼 이벤트 바인딩 + BindWidget 상태 진단
//
// 📌 주의: 이 시점에서는 StashPanel/LoadoutPanel은 InvComp과 아직 미바인딩
//    → InitializePanels()가 별도로 호출되어야 실제로 아이템이 표시됨
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] NativeOnInitialized 시작"));

	// ── BindWidget 상태 진단 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   StashPanel=%s"), StashPanel ? TEXT("바인딩됨") : TEXT("⚠ nullptr (BindWidget 확인!)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   LoadoutSpatialInventory=%s"), LoadoutSpatialInventory ? TEXT("바인딩됨") : TEXT("⚠ nullptr (BindWidget 확인!)"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Deploy=%s"), Button_Deploy ? TEXT("바인딩됨") : TEXT("⚠ nullptr (BindWidget 확인!)"));

	// ── 출격 버튼 OnClicked 이벤트 바인딩 ──
	if (Button_Deploy)
	{
		Button_Deploy->OnClicked.AddDynamic(this, &ThisClass::OnDeployClicked);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Deploy → OnDeployClicked 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget]   Button_Deploy가 nullptr! → WBP에서 'Button_Deploy' 이름의 Button 위젯을 추가하세요"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] NativeOnInitialized 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// InitializePanels — 양쪽 패널을 각각의 InvComp와 바인딩
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 경로:
//   LobbyController::ShowLobbyWidget()
//     → LobbyStashWidgetInstance->InitializePanels(StashComp, LoadoutComp)
//
// 📌 내부 동작:
//   1) CachedStashComp / CachedLoadoutComp 캐시 (나중에 전송 시 참조)
//   2) StashPanel.SetPanelTitle("STASH (창고)")
//   3) StashPanel.InitializeWithComponent(StashComp)
//      → 내부에서 3개 Grid에 SetInventoryComponent(StashComp) 호출
//   4) LoadoutSpatialInventory → SetInventoryComponent(LoadoutComp)
//      → 인게임과 동일한 SpatialInventory UI에 LoadoutComp 바인딩
//
// 📌 이 함수 호출 후:
//   - StashComp에 아이템이 있으면 좌측 Grid에 자동 표시됨
//     (RestoreFromSaveData → FastArray 추가 → OnItemAdded 델리게이트 → Grid.AddItem)
//   - LoadoutComp은 비어있으므로 우측은 빈 Grid
//
// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 SharedHoverItem 초기화 연결
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::InitializePanels(UInv_InventoryComponent* StashComp, UInv_InventoryComponent* LoadoutComp)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] ── InitializePanels 시작 ──"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   StashComp=%s | LoadoutComp=%s"),
		StashComp ? *StashComp->GetName() : TEXT("nullptr"),
		LoadoutComp ? *LoadoutComp->GetName() : TEXT("nullptr"));

	CachedStashComp = StashComp;
	CachedLoadoutComp = LoadoutComp;

	// ── Stash Panel 초기화 (좌측: 창고) ──
	if (StashPanel && StashComp)
	{
		StashPanel->SetPanelTitle(FText::FromString(TEXT("STASH (창고)")));
		StashPanel->InitializeWithComponent(StashComp);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] StashPanel ← StashComp 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] Stash 측 초기화 실패!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget]   StashPanel=%s | StashComp=%s"),
			StashPanel ? TEXT("O") : TEXT("X (WBP에서 BindWidget 확인)"),
			StashComp ? TEXT("O") : TEXT("X (LobbyController 생성자 확인)"));
	}

	// ── Loadout SpatialInventory 초기화 (우측: 출격장비 — 인게임과 동일 UI) ──
	// [Phase 4 Lobby] LoadoutPanel(HellunaLobbyPanel) → LoadoutSpatialInventory(Inv_SpatialInventory)로 교체
	// → 장착 슬롯 + 3탭 Grid를 인게임과 동일하게 사용
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 SharedHoverItem 연결
	if (LoadoutSpatialInventory && LoadoutComp)
	{
		LoadoutSpatialInventory->SetInventoryComponent(LoadoutComp);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] LoadoutSpatialInventory ← LoadoutComp 바인딩 완료"));
	}
	else
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] Loadout 측 초기화 실패!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget]   LoadoutSpatialInventory=%s | LoadoutComp=%s"),
			LoadoutSpatialInventory ? TEXT("O") : TEXT("X (WBP에서 BindWidget 'LoadoutSpatialInventory' 확인)"),
			LoadoutComp ? TEXT("O") : TEXT("X (LobbyController 생성자 확인)"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] ── InitializePanels 완료 ──"));
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 SharedHoverItem 초기화 연결
}

// ════════════════════════════════════════════════════════════════════════════════
// TransferItemToLoadout — Stash → Loadout 아이템 전송
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: UI에서 Stash 아이템의 "Loadout으로 보내기" 버튼 클릭
// 📌 처리: Server RPC 호출 → 서버에서 실제 전송 처리 → 리플리케이션으로 UI 자동 업데이트
//
// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::TransferItemToLoadout(int32 ItemEntryIndex)
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] TransferToLoadout: LobbyController 없음!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget]   → GetOwningPlayer()가 AHellunaLobbyController인지 확인"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] TransferToLoadout → EntryIndex=%d | Stash→Loadout"), ItemEntryIndex);
	LobbyPC->Server_TransferItem(ItemEntryIndex, ELobbyTransferDirection::StashToLoadout);
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
}

// ════════════════════════════════════════════════════════════════════════════════
// TransferItemToStash — Loadout → Stash 아이템 전송
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: UI에서 Loadout 아이템의 "Stash로 보내기" 버튼 클릭
// 📌 처리: Server RPC 호출 → 서버에서 실제 전송 처리 → 리플리케이션으로 UI 자동 업데이트
//
// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::TransferItemToStash(int32 ItemEntryIndex)
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] TransferToStash: LobbyController 없음!"));
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget]   → GetOwningPlayer()가 AHellunaLobbyController인지 확인"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] TransferToStash → EntryIndex=%d | Loadout→Stash"), ItemEntryIndex);
	LobbyPC->Server_TransferItem(ItemEntryIndex, ELobbyTransferDirection::LoadoutToStash);
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
}

// ════════════════════════════════════════════════════════════════════════════════
// OnDeployClicked — 출격 버튼 클릭 콜백
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 경로: Button_Deploy.OnClicked → OnDeployClicked
// 📌 처리: LobbyPC->Server_Deploy() Server RPC 호출
//    → 서버에서 SQLite 저장 + ClientTravel 지시
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::OnDeployClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] ══════════════════════════════════════"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 출격 버튼 클릭!"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] ══════════════════════════════════════"));

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] OnDeployClicked: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] → Server_Deploy RPC 호출"));
	LobbyPC->Server_Deploy();
}

// ════════════════════════════════════════════════════════════════════════════════
// GetLobbyController — 현재 클라이언트의 LobbyController 가져오기
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 GetOwningPlayer(): 이 위젯을 소유한 PlayerController 반환
// 📌 Cast<AHellunaLobbyController>: 로비 전용 Controller로 캐스팅
//    실패하면 nullptr → 호출자가 처리
//
// ════════════════════════════════════════════════════════════════════════════════
AHellunaLobbyController* UHellunaLobbyStashWidget::GetLobbyController() const
{
	APlayerController* PC = GetOwningPlayer();
	AHellunaLobbyController* LobbyPC = PC ? Cast<AHellunaLobbyController>(PC) : nullptr;

	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[StashWidget] GetLobbyController: 캐스팅 실패 | PC=%s"),
			PC ? *PC->GetClass()->GetName() : TEXT("nullptr"));
	}

	return LobbyPC;
}
