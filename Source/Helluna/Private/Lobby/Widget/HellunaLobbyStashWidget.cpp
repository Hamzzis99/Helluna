// ════════════════════════════════════════════════════════════════════════════════
// File: Source/Helluna/Private/Lobby/Widget/HellunaLobbyStashWidget.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 메인 위젯 — 탑 네비게이션 바 + 3탭 (Play / Loadout / Character)
//
// 📌 레이아웃 (Phase 번외 리팩토링):
//   ┌─────────────────────────────────────────────────────────┐
//   │  [PLAY]  [LOADOUT]  [CHARACTER]           TopNavBar     │
//   ├─────────────────────────────────────────────────────────┤
//   │  Page 0: PlayPage      — 캐릭터 프리뷰 + 맵 카드 + START│
//   │  Page 1: LoadoutPage   — Stash + Loadout + Deploy (기존) │
//   │  Page 2: CharacterPage — 캐릭터 선택 (기존)              │
//   └─────────────────────────────────────────────────────────┘
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
#include "Lobby/Widget/HellunaLobbyCharSelectWidget.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "HellunaTypes.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

// 로그 카테고리 (공유 헤더 — DEFINE은 HellunaLobbyGameMode.cpp)
#include "Lobby/HellunaLobbyLog.h"

// ════════════════════════════════════════════════════════════════════════════════
// NativeOnInitialized — 위젯 생성 시 초기화
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] NativeOnInitialized 시작"));

	// ── BindWidget 상태 진단 ──
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   MainSwitcher=%s"), MainSwitcher ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Tab_Play=%s"), Button_Tab_Play ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Tab_Loadout=%s"), Button_Tab_Loadout ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Tab_Character=%s"), Button_Tab_Character ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Start=%s"), Button_Start ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Deploy=%s"), Button_Deploy ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   CharacterSelectPanel=%s"), CharacterSelectPanel ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   StashPanel=%s"), StashPanel ? TEXT("OK") : TEXT("nullptr"));
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   LoadoutSpatialInventory=%s"), LoadoutSpatialInventory ? TEXT("OK") : TEXT("nullptr"));

	// ── 탭 버튼 OnClicked 바인딩 ──
	if (Button_Tab_Play)
	{
		Button_Tab_Play->OnClicked.AddUniqueDynamic(this, &ThisClass::OnTabPlayClicked); // U22: 중복 바인딩 방지
	}
	if (Button_Tab_Loadout)
	{
		Button_Tab_Loadout->OnClicked.AddUniqueDynamic(this, &ThisClass::OnTabLoadoutClicked); // U22
	}
	if (Button_Tab_Character)
	{
		Button_Tab_Character->OnClicked.AddUniqueDynamic(this, &ThisClass::OnTabCharacterClicked); // U22
	}

	// ── Play 탭: START 버튼 바인딩 ──
	if (Button_Start)
	{
		Button_Start->OnClicked.AddUniqueDynamic(this, &ThisClass::OnStartClicked); // U22
	}

	// ── Loadout 탭: 출격 버튼 바인딩 (기존) ──
	if (Button_Deploy)
	{
		Button_Deploy->OnClicked.AddUniqueDynamic(this, &ThisClass::OnDeployClicked); // U22
	}

	// ── 캐릭터 선택 완료 델리게이트 바인딩 ──
	if (CharacterSelectPanel)
	{
		CharacterSelectPanel->OnCharacterSelected.AddUniqueDynamic(this, &ThisClass::OnCharacterSelectedHandler); // U22
	}

	// ── 경고 텍스트 초기 숨김 ──
	if (Text_NoCharWarning)
	{
		Text_NoCharWarning->SetVisibility(ESlateVisibility::Collapsed);
	}

	// ── 시작 탭: Play ──
	SwitchToTab(LobbyTab::Play);

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] NativeOnInitialized 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// InitializePanels — 양쪽 패널을 각각의 InvComp와 바인딩
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

	// ── [Phase 4 Fix] 우클릭 전송 모드 활성화 ──
	if (StashPanel)
	{
		StashPanel->EnableLobbyTransferMode();
		StashPanel->OnPanelTransferRequested.RemoveDynamic(this, &ThisClass::OnStashItemTransferRequested);
		StashPanel->OnPanelTransferRequested.AddDynamic(this, &ThisClass::OnStashItemTransferRequested);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] StashPanel → 우클릭 전송 모드 ON (→ Loadout)"));
	}

	if (LoadoutSpatialInventory)
	{
		LoadoutSpatialInventory->EnableLobbyTransferMode();
		LoadoutSpatialInventory->OnSpatialTransferRequested.RemoveDynamic(this, &ThisClass::OnLoadoutItemTransferRequested);
		LoadoutSpatialInventory->OnSpatialTransferRequested.AddDynamic(this, &ThisClass::OnLoadoutItemTransferRequested);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] LoadoutSpatialInventory → 우클릭 전송 모드 ON (→ Stash)"));
	}

	// ── [Fix19] 전송 대상 Grid 교차 연결 (용량 사전 체크용) ──
	if (StashPanel && LoadoutSpatialInventory)
	{
		// Stash → Loadout 방향
		if (StashPanel->GetGrid_Equippables() && LoadoutSpatialInventory->GetGrid_Equippables())
		{
			StashPanel->GetGrid_Equippables()->SetLobbyTargetGrid(LoadoutSpatialInventory->GetGrid_Equippables());
		}
		if (StashPanel->GetGrid_Consumables() && LoadoutSpatialInventory->GetGrid_Consumables())
		{
			StashPanel->GetGrid_Consumables()->SetLobbyTargetGrid(LoadoutSpatialInventory->GetGrid_Consumables());
		}
		if (StashPanel->GetGrid_Craftables() && LoadoutSpatialInventory->GetGrid_Craftables())
		{
			StashPanel->GetGrid_Craftables()->SetLobbyTargetGrid(LoadoutSpatialInventory->GetGrid_Craftables());
		}

		// Loadout → Stash 방향
		if (LoadoutSpatialInventory->GetGrid_Equippables() && StashPanel->GetGrid_Equippables())
		{
			LoadoutSpatialInventory->GetGrid_Equippables()->SetLobbyTargetGrid(StashPanel->GetGrid_Equippables());
		}
		if (LoadoutSpatialInventory->GetGrid_Consumables() && StashPanel->GetGrid_Consumables())
		{
			LoadoutSpatialInventory->GetGrid_Consumables()->SetLobbyTargetGrid(StashPanel->GetGrid_Consumables());
		}
		if (LoadoutSpatialInventory->GetGrid_Craftables() && StashPanel->GetGrid_Craftables())
		{
			LoadoutSpatialInventory->GetGrid_Craftables()->SetLobbyTargetGrid(StashPanel->GetGrid_Craftables());
		}

		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix19] 전송 대상 Grid 교차 연결 완료 (Stash↔Loadout)"));

		// ── [CrossSwap] 크로스 Grid Swap 델리게이트 바인딩 ──
		auto BindCrossSwap = [this](UInv_InventoryGrid* Grid)
		{
			if (Grid)
			{
				Grid->OnLobbyCrossSwapRequested.RemoveDynamic(this, &ThisClass::OnCrossSwapRequested);
				Grid->OnLobbyCrossSwapRequested.AddUniqueDynamic(this, &ThisClass::OnCrossSwapRequested);
			}
		};

		// Stash 측 Grid 바인딩
		BindCrossSwap(StashPanel->GetGrid_Equippables());
		BindCrossSwap(StashPanel->GetGrid_Consumables());
		BindCrossSwap(StashPanel->GetGrid_Craftables());

		// Loadout 측 Grid 바인딩
		BindCrossSwap(LoadoutSpatialInventory->GetGrid_Equippables());
		BindCrossSwap(LoadoutSpatialInventory->GetGrid_Consumables());
		BindCrossSwap(LoadoutSpatialInventory->GetGrid_Craftables());

		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [CrossSwap] 크로스 Grid Swap 델리게이트 바인딩 완료"));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] ── InitializePanels 완료 ──"));
}

// ════════════════════════════════════════════════════════════════════════════════
// 탭 네비게이션
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::SwitchToTab(int32 TabIndex)
{
	if (!MainSwitcher)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] SwitchToTab: MainSwitcher nullptr!"));
		return;
	}

	// U31: TabIndex 범위 검증
	const int32 ClampedIndex = FMath::Clamp(TabIndex, 0, 2);
	MainSwitcher->SetActiveWidgetIndex(ClampedIndex);
	UpdateTabVisuals(ClampedIndex);

	// ── 프리뷰 씬 Solo 모드 연동 ──
	AHellunaLobbyController* LobbyPC = GetLobbyController();

	if (CachedPreviewScene.IsValid())
	{
		if (TabIndex == LobbyTab::Play)
		{
			// Play 탭: 선택된 캐릭터 Solo (미선택이면 기본 Index 0)
			int32 HeroIndex = 0;
			if (LobbyPC && LobbyPC->GetSelectedHeroType() != EHellunaHeroType::None)
			{
				HeroIndex = HeroTypeToIndex(LobbyPC->GetSelectedHeroType());
			}
			CachedPreviewScene->SetSoloCharacter(HeroIndex);
		}
		else if (TabIndex == LobbyTab::Character)
		{
			// CHARACTER 탭: Solo 모드 해제, 전체 표시
			CachedPreviewScene->ClearSoloMode();
		}
		// Loadout 탭: 프리뷰 상태 유지 (변경 없음)
	}

	// ── Level Streaming 배경 전환 ──
	if (LobbyPC)
	{
		LobbyPC->LoadBackgroundForTab(TabIndex);
	}

	// ── Play 탭일 때 캐릭터 미선택 경고 표시 ──
	if (Text_NoCharWarning)
	{
		const bool bShowWarning = (TabIndex == LobbyTab::Play) && !IsCharacterSelected();
		Text_NoCharWarning->SetVisibility(bShowWarning ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] SwitchToTab(%d) — %s"),
		TabIndex,
		TabIndex == LobbyTab::Play ? TEXT("Play") :
		TabIndex == LobbyTab::Loadout ? TEXT("Loadout") :
		TabIndex == LobbyTab::Character ? TEXT("Character") : TEXT("Unknown"));
}

void UHellunaLobbyStashWidget::OnTabPlayClicked()
{
	SwitchToTab(LobbyTab::Play);
}

void UHellunaLobbyStashWidget::OnTabLoadoutClicked()
{
	SwitchToTab(LobbyTab::Loadout);
}

void UHellunaLobbyStashWidget::OnTabCharacterClicked()
{
	SwitchToTab(LobbyTab::Character);
}

void UHellunaLobbyStashWidget::UpdateTabVisuals(int32 ActiveTabIndex)
{
	TArray<UButton*> TabButtons = { Button_Tab_Play, Button_Tab_Loadout, Button_Tab_Character };

	for (int32 i = 0; i < TabButtons.Num(); ++i)
	{
		if (!TabButtons[i]) continue;

		const bool bActive = (i == ActiveTabIndex);
		TabButtons[i]->SetBackgroundColor(bActive ? ActiveTabColor : InactiveTabColor);
	}

	CurrentTabIndex = ActiveTabIndex;
}

// ════════════════════════════════════════════════════════════════════════════════
// Play 탭 — START 버튼
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::OnStartClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] START 버튼 클릭!"));

	// 캐릭터 미선택 체크 (클라이언트 UX 방어 — 서버에서도 별도 체크)
	if (!IsCharacterSelected())
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] OnStartClicked: 캐릭터 미선택 → 경고 표시"));
		if (Text_NoCharWarning)
		{
			Text_NoCharWarning->SetVisibility(ESlateVisibility::Visible);
		}
		return;
	}

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] OnStartClicked: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] → Server_Deploy RPC 호출 (START)"));
	LobbyPC->Server_Deploy();
}

// ════════════════════════════════════════════════════════════════════════════════
// 중앙 프리뷰 설정 (ShowLobbyWidget에서 호출)
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::SetupCenterPreview(AHellunaCharacterSelectSceneV2* InPreviewScene)
{
	// 프리뷰 씬 캐시 (직접 뷰포트 모드 — RT/MID 불필요)
	CachedPreviewScene = InPreviewScene;

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] SetupCenterPreview: 씬 캐시 완료 (직접 뷰포트 모드)"));

	// 초기 Solo 모드 적용 (Play 탭이 기본이므로)
	if (CachedPreviewScene.IsValid() && CurrentTabIndex == LobbyTab::Play)
	{
		int32 HeroIndex = 0;
		AHellunaLobbyController* LobbyPC = GetLobbyController();
		if (LobbyPC && LobbyPC->GetSelectedHeroType() != EHellunaHeroType::None)
		{
			HeroIndex = HeroTypeToIndex(LobbyPC->GetSelectedHeroType());
		}
		CachedPreviewScene->SetSoloCharacter(HeroIndex);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] SetupCenterPreview: 초기 Solo 모드 → Index %d"), HeroIndex);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// IsCharacterSelected — 캐릭터 선택 여부
// ════════════════════════════════════════════════════════════════════════════════

bool UHellunaLobbyStashWidget::IsCharacterSelected() const
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	return LobbyPC && LobbyPC->GetSelectedHeroType() != EHellunaHeroType::None;
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 4 Fix] 우클릭 전송 핸들러
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::OnStashItemTransferRequested(int32 EntryIndex, int32 TargetGridIndex)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] Stash 우클릭 전송 → Loadout | EntryIndex=%d, TargetGridIndex=%d"), EntryIndex, TargetGridIndex);
	TransferItemToLoadout(EntryIndex, TargetGridIndex);
}

void UHellunaLobbyStashWidget::OnLoadoutItemTransferRequested(int32 EntryIndex, int32 TargetGridIndex)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] Loadout 우클릭 전송 → Stash | EntryIndex=%d, TargetGridIndex=%d"), EntryIndex, TargetGridIndex);
	TransferItemToStash(EntryIndex, TargetGridIndex);
}

// ════════════════════════════════════════════════════════════════════════════════
// [CrossSwap] 크로스 Grid Swap 핸들러
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::OnCrossSwapRequested(int32 RepID_A, int32 RepID_B, int32 TargetGridIndex)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] CrossSwap 요청: RepID_A=%d ↔ RepID_B=%d | TargetGridIndex=%d"), RepID_A, RepID_B, TargetGridIndex);

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] CrossSwap: LobbyController 없음!"));
		return;
	}

	LobbyPC->Server_SwapTransferItem(RepID_A, RepID_B, TargetGridIndex);
}

// ════════════════════════════════════════════════════════════════════════════════
// TransferItemToLoadout — Stash → Loadout 아이템 전송
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::TransferItemToLoadout(int32 ItemEntryIndex, int32 TargetGridIndex)
{
	if (ItemEntryIndex < 0)
	{
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[StashWidget] TransferToLoadout: 빈 슬롯 (EntryIndex=%d) → 무시"), ItemEntryIndex);
		return;
	}

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] TransferToLoadout: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] TransferToLoadout → EntryIndex=%d, TargetGridIndex=%d | Stash→Loadout"), ItemEntryIndex, TargetGridIndex);
	LobbyPC->Server_TransferItem(ItemEntryIndex, ELobbyTransferDirection::StashToLoadout, TargetGridIndex);
}

// ════════════════════════════════════════════════════════════════════════════════
// TransferItemToStash — Loadout → Stash 아이템 전송
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::TransferItemToStash(int32 ItemEntryIndex, int32 TargetGridIndex)
{
	if (ItemEntryIndex < 0)
	{
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[StashWidget] TransferToStash: 빈 슬롯 (EntryIndex=%d) → 무시"), ItemEntryIndex);
		return;
	}

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] TransferToStash: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] TransferToStash → EntryIndex=%d, TargetGridIndex=%d | Loadout→Stash"), ItemEntryIndex, TargetGridIndex);
	LobbyPC->Server_TransferItem(ItemEntryIndex, ELobbyTransferDirection::LoadoutToStash, TargetGridIndex);
}

// ════════════════════════════════════════════════════════════════════════════════
// OnDeployClicked — 출격 버튼 클릭 콜백 (Loadout 탭)
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::OnDeployClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 출격 버튼 클릭! (Loadout 탭)"));

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] OnDeployClicked: LobbyController 없음!"));
		return;
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] → Server_Deploy RPC 호출 (Deploy)"));
	LobbyPC->Server_Deploy();
}

// ════════════════════════════════════════════════════════════════════════════════
// GetLobbyController — 현재 클라이언트의 LobbyController 가져오기
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

// ════════════════════════════════════════════════════════════════════════════════
// SwitchToInventoryPage — 하위호환 (내부적으로 SwitchToTab(Loadout) 호출)
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::SwitchToInventoryPage()
{
	SwitchToTab(LobbyTab::Loadout);
}

// ════════════════════════════════════════════════════════════════════════════════
// OnCharacterSelectedHandler — 캐릭터 선택 완료
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 기존: SwitchToInventoryPage() 자동 호출 → 삭제
// 📌 변경: 경고 숨김 + Play 탭이면 Solo 프리뷰 업데이트 (탭 이동 안 함)
//
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::OnCharacterSelectedHandler(EHellunaHeroType SelectedHero)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] OnCharacterSelectedHandler | Hero=%d"), static_cast<int32>(SelectedHero));

	// 경고 텍스트 숨김
	if (Text_NoCharWarning)
	{
		Text_NoCharWarning->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Play 탭에 있으면 Solo 프리뷰를 선택된 캐릭터로 업데이트
	if (CurrentTabIndex == LobbyTab::Play && CachedPreviewScene.IsValid())
	{
		const int32 HeroIndex = HeroTypeToIndex(SelectedHero);
		if (HeroIndex >= 0)
		{
			CachedPreviewScene->SetSoloCharacter(HeroIndex);
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] Play 탭 Solo 프리뷰 업데이트 → Index %d"), HeroIndex);
		}
	}
}
