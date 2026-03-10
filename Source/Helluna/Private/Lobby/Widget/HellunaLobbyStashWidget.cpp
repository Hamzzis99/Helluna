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
#include "Lobby/Party/HellunaMatchmakingTypes.h"
#include "Lobby/Widget/HellunaLobbyPanel.h"
#include "Lobby/Widget/HellunaLobbyCharSelectWidget.h"
#include "Widgets/Inventory/Spatial/Inv_SpatialInventory.h"
#include "Lobby/Controller/HellunaLobbyController.h"
#include "Login/Preview/HellunaCharacterSelectSceneV2.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Widgets/Inventory/GridSlots/Inv_EquippedGridSlot.h"
#include "HellunaTypes.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Engine/Texture2D.h"
#include "Lobby/GameMode/HellunaLobbyGameMode.h"

// 로그 카테고리 (공유 헤더 — DEFINE은 HellunaLobbyGameMode.cpp)
#include "Lobby/HellunaLobbyLog.h"

namespace
{
	constexpr float GSearchRingOuterDegreesPerSecond = 360.0f;
	constexpr float GSearchRingInnerDegreesPerSecond = -240.0f;
	const FVector2D GSearchRingBackdropSize(108.0f, 108.0f);
	const FVector2D GSearchRingOuterSize(108.0f, 108.0f);
	const FVector2D GSearchRingInnerSize(84.0f, 84.0f);
	const TCHAR* GSearchRingBackdropTexturePath = TEXT("/Game/Migration/VFX/EasyAtmos/Textures/systemTextures/T_circle_01.T_circle_01");
	const TCHAR* GSearchRingOuterTexturePath = TEXT("/Game/Migration/VFX/NiagaraExplosion01/Textures/T_Ring_002.T_Ring_002");
	const TCHAR* GSearchRingInnerTexturePath = TEXT("/Game/Migration/VFX/NiagaraExplosion01/Textures/T_Ring_004.T_Ring_004");

	void ApplySearchRingStyle(UImage* Image, const TCHAR* TexturePath, const FVector2D& BrushSize, const FLinearColor& Tint)
	{
		if (!IsValid(Image))
		{
			return;
		}

		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TexturePath))
		{
			Image->SetBrushFromTexture(Texture, false);
			Image->SetDesiredSizeOverride(BrushSize);
		}
		else
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] Search spinner texture load failed: %s"), TexturePath);
		}

		Image->SetColorAndOpacity(Tint);
		Image->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Image->SetRenderOpacity(Tint.A);
		Image->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Fix45-H1] NativeDestruct — 위젯 파괴 시 모든 델리게이트 해제
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::NativeDestruct()
{
	// ── NativeOnInitialized 바인딩 해제 ──
	if (Button_Tab_Play) { Button_Tab_Play->OnClicked.RemoveDynamic(this, &ThisClass::OnTabPlayClicked); }
	if (Button_Tab_Loadout) { Button_Tab_Loadout->OnClicked.RemoveDynamic(this, &ThisClass::OnTabLoadoutClicked); }
	if (Button_Tab_Character) { Button_Tab_Character->OnClicked.RemoveDynamic(this, &ThisClass::OnTabCharacterClicked); }
	if (Button_Start) { Button_Start->OnClicked.RemoveDynamic(this, &ThisClass::OnStartClicked); }
	if (Button_Party) { Button_Party->OnClicked.RemoveDynamic(this, &ThisClass::OnPartyClicked); }
	if (Button_Deploy) { Button_Deploy->OnClicked.RemoveDynamic(this, &ThisClass::OnDeployClicked); }
	if (CharacterSelectPanel) { CharacterSelectPanel->OnCharacterSelected.RemoveDynamic(this, &ThisClass::OnCharacterSelectedHandler); }
	if (PlayChatSendButton) { PlayChatSendButton->OnClicked.RemoveDynamic(this, &ThisClass::OnPlayChatSendClicked); }
	if (PlayChatInput) { PlayChatInput->OnTextCommitted.RemoveDynamic(this, &ThisClass::OnPlayChatInputCommitted); }
	// [Phase 15]
	if (Button_Mode_Solo) { Button_Mode_Solo->OnClicked.RemoveDynamic(this, &ThisClass::OnSoloModeClicked); }
	if (Button_Mode_Duo) { Button_Mode_Duo->OnClicked.RemoveDynamic(this, &ThisClass::OnDuoModeClicked); }
	if (Button_Mode_Squad) { Button_Mode_Squad->OnClicked.RemoveDynamic(this, &ThisClass::OnPartyModeClicked); }
	if (Button_CancelMatchmaking) { Button_CancelMatchmaking->OnClicked.RemoveDynamic(this, &ThisClass::OnCancelMatchmakingClicked); }
	// [Phase 17] 맵 선택 카드
	if (Button_MapPrev) { Button_MapPrev->OnClicked.RemoveDynamic(this, &ThisClass::OnMapPrevClicked); }
	if (Button_MapNext) { Button_MapNext->OnClicked.RemoveDynamic(this, &ThisClass::OnMapNextClicked); }
	// [Phase 17.1] 맵 선택 팝업
	if (Button_MapCard) { Button_MapCard->OnClicked.RemoveDynamic(this, &ThisClass::OnMapCardClicked); }
	if (Button_MapConfirm) { Button_MapConfirm->OnClicked.RemoveDynamic(this, &ThisClass::OnMapConfirmClicked); }
	if (Button_CloseMapPopup) { Button_CloseMapPopup->OnClicked.RemoveDynamic(this, &ThisClass::OnCloseMapPopupClicked); }

	// ── LobbyPC 외부 오브젝트 바인딩 해제 ──
	if (AHellunaLobbyController* LobbyPC = GetLobbyController())
	{
		LobbyPC->OnPartyStateChanged.RemoveDynamic(this, &ThisClass::OnPartyStateChangedHandler);
		LobbyPC->OnPartyChatReceived.RemoveDynamic(this, &ThisClass::HandlePlayChatReceived);
		LobbyPC->OnMatchmakingStatusChanged.RemoveDynamic(this, &ThisClass::HandleMatchmakingStatusChanged); // Phase 15
		// [Phase 17]
		LobbyPC->OnMatchmakingFound.RemoveDynamic(this, &ThisClass::HandleMatchmakingFound);
		LobbyPC->OnMatchmakingCountdown.RemoveDynamic(this, &ThisClass::HandleMatchmakingCountdown);
		LobbyPC->OnMatchmakingCancelled.RemoveDynamic(this, &ThisClass::HandleMatchmakingCancelled);
	}

	// ── InitializePanels 바인딩 해제 ──
	if (StashPanel) { StashPanel->OnPanelTransferRequested.RemoveDynamic(this, &ThisClass::OnStashItemTransferRequested); }
	if (LoadoutSpatialInventory) { LoadoutSpatialInventory->OnSpatialTransferRequested.RemoveDynamic(this, &ThisClass::OnLoadoutItemTransferRequested); }

	// ── CrossSwap Grid 바인딩 해제 ──
	auto UnbindCrossSwap = [this](UInv_InventoryGrid* Grid)
	{
		if (Grid) { Grid->OnLobbyCrossSwapRequested.RemoveDynamic(this, &ThisClass::OnCrossSwapRequested); }
	};
	if (StashPanel)
	{
		UnbindCrossSwap(StashPanel->GetGrid_Equippables());
		UnbindCrossSwap(StashPanel->GetGrid_Consumables());
		UnbindCrossSwap(StashPanel->GetGrid_Craftables());
	}
	if (LoadoutSpatialInventory)
	{
		UnbindCrossSwap(LoadoutSpatialInventory->GetGrid_Equippables());
		UnbindCrossSwap(LoadoutSpatialInventory->GetGrid_Consumables());
		UnbindCrossSwap(LoadoutSpatialInventory->GetGrid_Craftables());
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix45-H1] NativeDestruct — 모든 델리게이트 해제 완료"));

	Super::NativeDestruct();
}

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

	// ── [Phase 12g] 파티 버튼 바인딩 (Optional) ──
	if (Button_Party)
	{
		Button_Party->OnClicked.AddUniqueDynamic(this, &ThisClass::OnPartyClicked);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget]   Button_Party=OK (바인딩 완료)"));
	}

	// ── [Phase 15] 모드 토글 + 매칭 취소 바인딩 (Optional) ──
	if (Button_Mode_Solo)
	{
		Button_Mode_Solo->OnClicked.AddUniqueDynamic(this, &ThisClass::OnSoloModeClicked);
	}
	if (Button_Mode_Duo)
	{
		Button_Mode_Duo->OnClicked.AddUniqueDynamic(this, &ThisClass::OnDuoModeClicked);
	}
	if (Button_Mode_Squad)
	{
		Button_Mode_Squad->OnClicked.AddUniqueDynamic(this, &ThisClass::OnPartyModeClicked);
	}
	if (Button_CancelMatchmaking)
	{
		Button_CancelMatchmaking->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCancelMatchmakingClicked);
	}
	// 매칭 오버레이 초기 숨김
	if (MatchmakingOverlay)
	{
		MatchmakingOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
	// [Phase 18] 초기 모드 버튼 (솔로=1)
	UpdateModeButtonsForPartySize(1);

	// ── [Phase 17] PUBG식 맵 선택 카드 초기화 ──
	InitializeMapSelector();

	// ── [Phase 17.1] 맵 카드 버튼 + 팝업 버튼 바인딩 ──
	if (Button_MapCard)
	{
		Button_MapCard->OnClicked.AddUniqueDynamic(this, &ThisClass::OnMapCardClicked);
	}
	if (Button_MapConfirm)
	{
		Button_MapConfirm->OnClicked.AddUniqueDynamic(this, &ThisClass::OnMapConfirmClicked);
	}
	if (Button_CloseMapPopup)
	{
		Button_CloseMapPopup->OnClicked.AddUniqueDynamic(this, &ThisClass::OnCloseMapPopupClicked);
	}
	// 팝업 초기 숨김
	if (MapSelectPopupOverlay)
	{
		MapSelectPopupOverlay->SetVisibility(ESlateVisibility::Collapsed);
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

	// ── [Phase 12h] 파티 상태 변경 → START/READY 버튼 텍스트 갱신 ──
	if (AHellunaLobbyController* LobbyPC = GetLobbyController())
	{
		LobbyPC->OnPartyStateChanged.AddUniqueDynamic(this, &ThisClass::OnPartyStateChangedHandler);
		// [Phase 15] 매칭 상태 변경 바인딩
		LobbyPC->OnMatchmakingStatusChanged.AddUniqueDynamic(this, &ThisClass::HandleMatchmakingStatusChanged);
		// [Phase 17] 카운트다운 바인딩
		LobbyPC->OnMatchmakingFound.AddUniqueDynamic(this, &ThisClass::HandleMatchmakingFound);
		LobbyPC->OnMatchmakingCountdown.AddUniqueDynamic(this, &ThisClass::HandleMatchmakingCountdown);
		LobbyPC->OnMatchmakingCancelled.AddUniqueDynamic(this, &ThisClass::HandleMatchmakingCancelled);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] OnPartyStateChanged + OnMatchmakingStatusChanged + Phase17 바인딩 완료"));
	}

	// ── [Phase 12i] Play 탭 파티 채팅 바인딩 ──
	if (PlayChatSendButton)
	{
		PlayChatSendButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OnPlayChatSendClicked);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] PlayChatSendButton 바인딩 완료"));
	}
	if (PlayChatInput)
	{
		PlayChatInput->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::OnPlayChatInputCommitted);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] PlayChatInput Enter 바인딩 완료"));
	}

	// 초기 채팅 패널 숨김 (파티 없으면)
	UpdatePlayChatVisibility();

	ConfigureSearchSpinnerVisuals();
	SetSearchSpinnerVisible(false);

	// ── 시작 탭: Play ──
	SwitchToTab(LobbyTab::Play);

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] NativeOnInitialized 완료"));
}

// ════════════════════════════════════════════════════════════════════════════════
// InitializePanels — 양쪽 패널을 각각의 InvComp와 바인딩
// ════════════════════════════════════════════════════════════════════════════════
void UHellunaLobbyStashWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!ShouldAnimateSearchSpinner() || InDeltaTime <= 0.0f)
	{
		return;
	}

	SearchRingOuterAngle = FMath::Fmod(SearchRingOuterAngle + (GSearchRingOuterDegreesPerSecond * InDeltaTime), 360.0f);
	SearchRingInnerAngle = FMath::Fmod(SearchRingInnerAngle + (GSearchRingInnerDegreesPerSecond * InDeltaTime), 360.0f);

	if (SearchRingOuterAngle < 0.0f)
	{
		SearchRingOuterAngle += 360.0f;
	}
	if (SearchRingInnerAngle < 0.0f)
	{
		SearchRingInnerAngle += 360.0f;
	}

	if (Image_SearchRingOuter)
	{
		Image_SearchRingOuter->SetRenderTransformAngle(SearchRingOuterAngle);
	}
	if (Image_SearchRingInner)
	{
		Image_SearchRingInner->SetRenderTransformAngle(SearchRingInnerAngle);
	}
}

void UHellunaLobbyStashWidget::ConfigureSearchSpinnerVisuals()
{
	if (bSearchSpinnerConfigured)
	{
		return;
	}

	bSearchSpinnerConfigured = true;

	if (!Overlay_SearchSpinner && !Image_SearchRingBackdrop && !Image_SearchRingOuter && !Image_SearchRingInner)
	{
		UE_LOG(LogHellunaLobby, Verbose, TEXT("[StashWidget] Search spinner widgets are not bound."));
		return;
	}

	if (Overlay_SearchSpinner)
	{
		Overlay_SearchSpinner->SetRenderOpacity(1.0f);
	}

	ApplySearchRingStyle(Image_SearchRingBackdrop, GSearchRingBackdropTexturePath, GSearchRingBackdropSize, FLinearColor(0.34f, 0.40f, 0.95f, 0.16f));
	ApplySearchRingStyle(Image_SearchRingOuter, GSearchRingOuterTexturePath, GSearchRingOuterSize, FLinearColor(0.42f, 0.54f, 1.0f, 0.90f));
	ApplySearchRingStyle(Image_SearchRingInner, GSearchRingInnerTexturePath, GSearchRingInnerSize, FLinearColor(0.80f, 0.46f, 1.0f, 0.82f));

	SearchRingOuterAngle = 0.0f;
	SearchRingInnerAngle = 0.0f;

	if (Image_SearchRingOuter)
	{
		Image_SearchRingOuter->SetRenderTransformAngle(SearchRingOuterAngle);
	}
	if (Image_SearchRingInner)
	{
		Image_SearchRingInner->SetRenderTransformAngle(SearchRingInnerAngle);
	}
}

void UHellunaLobbyStashWidget::SetSearchSpinnerVisible(bool bVisible)
{
	const ESlateVisibility SpinnerVisibility = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;

	if (Overlay_SearchSpinner)
	{
		Overlay_SearchSpinner->SetVisibility(SpinnerVisibility);
		Overlay_SearchSpinner->SetRenderOpacity(bVisible ? 1.0f : 0.0f);
	}
	if (Image_SearchRingBackdrop)
	{
		Image_SearchRingBackdrop->SetVisibility(SpinnerVisibility);
	}
	if (Image_SearchRingOuter)
	{
		Image_SearchRingOuter->SetVisibility(SpinnerVisibility);
	}
	if (Image_SearchRingInner)
	{
		Image_SearchRingInner->SetVisibility(SpinnerVisibility);
	}

	if (bVisible)
	{
		SearchRingOuterAngle = 0.0f;
		SearchRingInnerAngle = 0.0f;
	}
}

bool UHellunaLobbyStashWidget::ShouldAnimateSearchSpinner() const
{
	if (!bInMatchmaking)
	{
		return false;
	}

	if (Text_Countdown && Text_Countdown->GetVisibility() == ESlateVisibility::Visible)
	{
		return false;
	}

	return Overlay_SearchSpinner && Overlay_SearchSpinner->GetVisibility() != ESlateVisibility::Collapsed;
}
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

	// ── [Fix39] 로비 복귀 시 장착 아이템 EquippedGridSlot 복원 ──
	// Inv_PlayerController::Client_RestoreEquippedItems (Phase 6)와 동일한 패턴
	// 로비 컨트롤러는 Inv_PlayerController를 상속하지 않으므로 여기서 직접 처리
	if (LoadoutSpatialInventory && LoadoutComp)
	{
		LoadoutSpatialInventory->CollectEquippedGridSlots();
		const TArray<TObjectPtr<UInv_EquippedGridSlot>>& EquippedSlots = LoadoutSpatialInventory->GetEquippedGridSlots();

		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix39] EquippedSlots=%d개"), EquippedSlots.Num());
		for (int32 s = 0; s < EquippedSlots.Num(); s++)
		{
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix39]   Slot[%d] Valid=%s WeaponSlotIndex=%d"),
				s, IsValid(EquippedSlots[s]) ? TEXT("Y") : TEXT("N"),
				IsValid(EquippedSlots[s]) ? EquippedSlots[s]->GetWeaponSlotIndex() : -99);
		}

		if (EquippedSlots.Num() > 0)
		{
			TArray<FInv_SavedItemData> SavedItems = LoadoutComp->CollectInventoryDataForSave();

			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix39] SavedItems=%d개"), SavedItems.Num());
			for (int32 d = 0; d < SavedItems.Num(); d++)
			{
				UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix39]   Item[%d] %s bEquipped=%s WeaponSlot=%d"),
					d, *SavedItems[d].ItemType.ToString(),
					SavedItems[d].bEquipped ? TEXT("Y") : TEXT("N"),
					SavedItems[d].WeaponSlotIndex);
			}

			TSet<UInv_InventoryItem*> ProcessedItems;
			int32 RestoredCount = 0;

			for (const FInv_SavedItemData& ItemData : SavedItems)
			{
				if (!ItemData.bEquipped || ItemData.WeaponSlotIndex < 0)
					continue;

				// WeaponSlotIndex에 맞는 EquippedGridSlot 찾기
				UInv_EquippedGridSlot* TargetSlot = nullptr;
				for (const TObjectPtr<UInv_EquippedGridSlot>& EquipSlot : EquippedSlots)
				{
					if (IsValid(EquipSlot) && EquipSlot->GetWeaponSlotIndex() == ItemData.WeaponSlotIndex)
					{
						TargetSlot = EquipSlot.Get();
						break;
					}
				}

				if (!TargetSlot)
				{
					UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] [Fix39] WeaponSlot %d 슬롯 없음 → 스킵"), ItemData.WeaponSlotIndex);
					continue;
				}

				UInv_InventoryItem* FoundItem = LoadoutComp->FindItemByTypeExcluding(ItemData.ItemType, ProcessedItems);
				if (!FoundItem)
				{
					UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] [Fix39] 아이템 못 찾음: %s"), *ItemData.ItemType.ToString());
					continue;
				}

				UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix39] RestoreEquippedItem 시도: Slot=%d Item=%s"),
					ItemData.WeaponSlotIndex, *ItemData.ItemType.ToString());

				UInv_EquippedSlottedItem* Result = LoadoutSpatialInventory->RestoreEquippedItem(TargetSlot, FoundItem);
				if (Result)
				{
					ProcessedItems.Add(FoundItem);
					RestoredCount++;
					UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix39] ✓ 복원 성공: Slot=%d"), ItemData.WeaponSlotIndex);
				}
				else
				{
					UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] [Fix39] ✗ RestoreEquippedItem 실패: Slot=%d"), ItemData.WeaponSlotIndex);
				}
			}

			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Fix39] 장착 아이템 복원 완료: %d개"), RestoredCount);
		}
		else
		{
			UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] [Fix39] EquippedSlots 비어있음 → 복원 스킵"));
		}
	}

	// ── [Phase 4 Fix] 우클릭 전송 모드 활성화 ──
	if (StashPanel)
	{
		StashPanel->EnableLobbyTransferMode();
		// [Fix45-H5] AddDynamic→AddUniqueDynamic (Remove+Add 패턴 유지하되 안전성 강화)
		StashPanel->OnPanelTransferRequested.RemoveDynamic(this, &ThisClass::OnStashItemTransferRequested);
		StashPanel->OnPanelTransferRequested.AddUniqueDynamic(this, &ThisClass::OnStashItemTransferRequested);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] StashPanel → 우클릭 전송 모드 ON (→ Loadout)"));
	}

	if (LoadoutSpatialInventory)
	{
		LoadoutSpatialInventory->EnableLobbyTransferMode();
		// [Fix45-H5] AddDynamic→AddUniqueDynamic
		LoadoutSpatialInventory->OnSpatialTransferRequested.RemoveDynamic(this, &ThisClass::OnLoadoutItemTransferRequested);
		LoadoutSpatialInventory->OnSpatialTransferRequested.AddUniqueDynamic(this, &ThisClass::OnLoadoutItemTransferRequested);
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

	// ── [Phase 12i] OnPartyChatReceived 바인딩 ──
	if (AHellunaLobbyController* LobbyPC = GetLobbyController())
	{
		LobbyPC->OnPartyChatReceived.AddUniqueDynamic(this, &ThisClass::HandlePlayChatReceived);
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase 12i] OnPartyChatReceived 바인딩 완료"));
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

	// ── 프리뷰 씬 Solo/Party 모드 연동 ──
	AHellunaLobbyController* LobbyPC = GetLobbyController();

	if (CachedPreviewScene.IsValid())
	{
		if (TabIndex == LobbyTab::Play)
		{
			// [Phase 12g-2] 파티 2명 이상이면 Party 프리뷰, 아니면 Solo
			if (LobbyPC && LobbyPC->CurrentPartyInfo.IsValid()
				&& LobbyPC->CurrentPartyInfo.Members.Num() >= 2)
			{
				LobbyPC->UpdatePartyPreview(LobbyPC->CurrentPartyInfo);
			}
			else
			{
				// Solo 모드: 선택된 캐릭터 (미선택이면 기본 Index 0)
				int32 HeroIndex = 0;
				if (LobbyPC && LobbyPC->GetSelectedHeroType() != EHellunaHeroType::None)
				{
					HeroIndex = HeroTypeToIndex(LobbyPC->GetSelectedHeroType());
				}
				CachedPreviewScene->SetSoloCharacter(HeroIndex);
			}
		}
		else if (TabIndex == LobbyTab::Character)
		{
			// CHARACTER 탭: Party/Solo 모두 해제, 3열 캐릭터 선택 표시
			if (CachedPreviewScene->IsPartyMode())
			{
				CachedPreviewScene->ClearPartyPreview();
			}
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

	// ── [Phase 12h] Play 탭 진입 시 START/READY 버튼 텍스트 갱신 ──
	if (TabIndex == LobbyTab::Play)
	{
		UpdateStartButtonForPartyState();

		// [Phase 12j] 네임태그 갱신
		UpdateNameTagOverlays();
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
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] START/READY 버튼 클릭! | Mode=%d | bInMatchmaking=%s"),
		static_cast<int32>(CurrentGameMode), bInMatchmaking ? TEXT("true") : TEXT("false"));

	// [Phase 15] 매칭 큐에 있으면 취소
	if (bInMatchmaking)
	{
		AHellunaLobbyController* LobbyPC = GetLobbyController();
		if (LobbyPC)
		{
			LobbyPC->Server_LeaveMatchmaking();
		}
		return;
	}

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

	if (CurrentGameMode != ELobbyGameMode::Solo)
	{
		// ── Duo/Squad 모드 ──
		if (LobbyPC->CurrentPartyInfo.IsValid()
			&& LobbyPC->CurrentPartyInfo.Members.Num() >= 2)
		{
			// 파티 가입 상태 → Ready 토글 (→ TryAutoDeployParty → 정원충족시 직접Deploy / 미달시 EnterQueue)
			bLocalPlayerReady = !bLocalPlayerReady;
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] → Server_SetPartyReady(%s)"),
				bLocalPlayerReady ? TEXT("true") : TEXT("false"));
			LobbyPC->Server_SetPartyReady(bLocalPlayerReady);
			UpdateStartButtonForPartyState();
		}
		else
		{
			// 파티 미가입 → 매칭 큐 진입
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] → Server_EnterMatchmaking RPC 호출 (매칭)"));
			LobbyPC->Server_EnterMatchmaking();
		}
	}
	else
	{
		// ── 솔로 모드 → 즉시 Deploy (기존) ──
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] → Server_Deploy RPC 호출 (START)"));
		LobbyPC->Server_Deploy();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// 중앙 프리뷰 설정 (ShowLobbyWidget에서 호출)
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::SetupCenterPreview(AHellunaCharacterSelectSceneV2* InPreviewScene)
{
	// 프리뷰 씬 캐시 (직접 뷰포트 모드 — RT/MID 불필요)
	CachedPreviewScene = InPreviewScene;

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] SetupCenterPreview: 씬 캐시 완료 (직접 뷰포트 모드)"));

	// 초기 모드 적용 (Play 탭이 기본이므로)
	if (CachedPreviewScene.IsValid() && CurrentTabIndex == LobbyTab::Play)
	{
		AHellunaLobbyController* LobbyPC = GetLobbyController();

		// [Phase 12g-2] 파티 상태에 따라 Party/Solo 분기
		if (LobbyPC && LobbyPC->CurrentPartyInfo.IsValid()
			&& LobbyPC->CurrentPartyInfo.Members.Num() >= 2)
		{
			LobbyPC->UpdatePartyPreview(LobbyPC->CurrentPartyInfo);
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] SetupCenterPreview: 초기 Party 모드 → %d명"),
				LobbyPC->CurrentPartyInfo.Members.Num());
		}
		else
		{
			int32 HeroIndex = 0;
			if (LobbyPC && LobbyPC->GetSelectedHeroType() != EHellunaHeroType::None)
			{
				HeroIndex = HeroTypeToIndex(LobbyPC->GetSelectedHeroType());
			}
			CachedPreviewScene->SetSoloCharacter(HeroIndex);
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] SetupCenterPreview: 초기 Solo 모드 → Index %d"), HeroIndex);
		}
	}

	// [Phase 12h] 초기 버튼 상태 설정
	UpdateStartButtonForPartyState();

	// [Phase 12j] 초기 네임태그
	UpdateNameTagOverlays();
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
// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12g] 파티 버튼 클릭
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::OnPartyClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 파티 버튼 클릭!"));

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		UE_LOG(LogHellunaLobby, Warning, TEXT("[StashWidget] OnPartyClicked: LobbyController 없음!"));
		return;
	}

	LobbyPC->TogglePartyWidget();
}

// ════════════════════════════════════════════════════════════════════════════════
// OnCharacterSelectedHandler — 캐릭터 선택 완료
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::OnCharacterSelectedHandler(EHellunaHeroType SelectedHero)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] OnCharacterSelectedHandler | Hero=%d"), static_cast<int32>(SelectedHero));

	// 경고 텍스트 숨김
	if (Text_NoCharWarning)
	{
		Text_NoCharWarning->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Play 탭에 있으면 프리뷰 업데이트
	if (CurrentTabIndex == LobbyTab::Play && CachedPreviewScene.IsValid())
	{
		AHellunaLobbyController* LobbyPC = GetLobbyController();

		// [Phase 12g-2] 파티 모드면 파티 프리뷰 갱신 (캐릭터 변경 반영은 서버 BroadcastPartyState로)
		// Solo 모드면 직접 Solo 캐릭터 업데이트
		if (LobbyPC && LobbyPC->CurrentPartyInfo.IsValid()
			&& LobbyPC->CurrentPartyInfo.Members.Num() >= 2)
		{
			// 파티 모드: 서버 BroadcastPartyState가 Client_UpdatePartyState를 통해 자동 갱신
			// 여기서는 로컬 프리뷰 반영만 (서버 응답 전 즉시 피드백)
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] Play 탭 Party 모드 — 서버 BroadcastPartyState 대기"));
		}
		else
		{
			const int32 HeroIndex = HeroTypeToIndex(SelectedHero);
			if (HeroIndex >= 0)
			{
				CachedPreviewScene->SetSoloCharacter(HeroIndex);
				UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] Play 탭 Solo 프리뷰 업데이트 → Index %d"), HeroIndex);
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12h] 파티 상태 변경 → START/READY 버튼 전환
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::OnPartyStateChangedHandler(const FHellunaPartyInfo& PartyInfo)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase 12h] 파티 상태 변경 수신 → 버튼 갱신"));

	// 파티 해산 시 Ready 상태 리셋
	if (!PartyInfo.IsValid() || PartyInfo.Members.Num() < 2)
	{
		bLocalPlayerReady = false;
	}

	// [Phase 18] 파티 인원 수에 따라 모드 버튼 갱신
	const int32 EffectiveSize = FMath::Max(1, PartyInfo.Members.Num());
	UpdateModeButtonsForPartySize(EffectiveSize);

	// [Phase 12i] 채팅 패널 표시/숨김
	UpdatePlayChatVisibility();

	// [Phase 12j] 네임태그 오버레이
	UpdateNameTagOverlays();
}

void UHellunaLobbyStashWidget::UpdateStartButtonForPartyState()
{
	// [Phase 15] 매칭 큐 중이면 CANCEL 유지 (HandleMatchmakingStatusChanged가 제어)
	if (bInMatchmaking)
	{
		return;
	}

	// Text_StartLabel 찾기: BindWidgetOptional 우선, 없으면 Button_Start의 자식 탐색
	UTextBlock* StartLabel = Text_StartLabel;
	if (!StartLabel && Button_Start && Button_Start->GetChildrenCount() > 0)
	{
		StartLabel = Cast<UTextBlock>(Button_Start->GetChildAt(0));
	}

	if (!StartLabel)
	{
		return;
	}

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	const bool bInParty = LobbyPC
		&& LobbyPC->CurrentPartyInfo.IsValid()
		&& LobbyPC->CurrentPartyInfo.Members.Num() >= 2;

	if (CurrentGameMode != ELobbyGameMode::Solo && bInParty)
	{
		// Duo/Squad 모드 + 파티 가입 → READY / CANCEL READY
		if (bLocalPlayerReady)
		{
			StartLabel->SetText(FText::FromString(TEXT("CANCEL READY")));
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 버튼 → CANCEL READY"));
		}
		else
		{
			StartLabel->SetText(FText::FromString(TEXT("READY")));
			UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 버튼 → READY"));
		}
	}
	else if (CurrentGameMode != ELobbyGameMode::Solo)
	{
		// Duo/Squad 모드 + 파티 미가입 → FIND MATCH
		StartLabel->SetText(FText::FromString(TEXT("FIND MATCH")));
		bLocalPlayerReady = false;
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 버튼 → FIND MATCH (매칭 모드)"));
	}
	else
	{
		// 솔로 모드
		StartLabel->SetText(FText::FromString(TEXT("START")));
		bLocalPlayerReady = false;
		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] 버튼 → START (솔로)"));
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12i] Play 탭 파티 채팅
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::HandlePlayChatReceived(const FHellunaPartyChatMessage& ChatMessage)
{
	AddPlayChatMessage(ChatMessage);
}

void UHellunaLobbyStashWidget::AddPlayChatMessage(const FHellunaPartyChatMessage& ChatMessage)
{
	if (!PlayChatScrollBox)
	{
		return;
	}

	// 메시지 누적 제한 (100개 초과 시 오래된 것 삭제)
	if (PlayChatScrollBox->GetChildrenCount() > 100)
	{
		PlayChatScrollBox->RemoveChildAt(0);
	}

	UTextBlock* MsgText = NewObject<UTextBlock>(this);
	if (!MsgText)
	{
		return;
	}

	const FString FormattedMsg = FString::Printf(TEXT("[%s] %s"),
		*ChatMessage.SenderName, *ChatMessage.Message);
	MsgText->SetText(FText::FromString(FormattedMsg));

	FSlateFontInfo FontInfo = MsgText->GetFont();
	FontInfo.Size = 11;
	MsgText->SetFont(FontInfo);

	// 발신자 색상: 본인이면 골드, 아니면 회색
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	const bool bIsSelf = LobbyPC && ChatMessage.SenderName == LobbyPC->GetPlayerId();
	MsgText->SetColorAndOpacity(FSlateColor(bIsSelf
		? FLinearColor(0.918f, 0.702f, 0.031f, 1.f)   // 골드
		: FLinearColor(0.63f, 0.63f, 0.67f, 1.f)));    // 회색

	PlayChatScrollBox->AddChild(MsgText);
	PlayChatScrollBox->ScrollToEnd();

	UE_LOG(LogHellunaLobby, Verbose, TEXT("[StashWidget] [Phase 12i] 채팅 표시: [%s] %s"),
		*ChatMessage.SenderName, *ChatMessage.Message);
}

void UHellunaLobbyStashWidget::OnPlayChatSendClicked()
{
	if (!PlayChatInput)
	{
		return;
	}

	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		return;
	}

	const FString Msg = PlayChatInput->GetText().ToString().TrimStartAndEnd();
	if (Msg.IsEmpty())
	{
		return;
	}

	LobbyPC->Server_SendPartyChatMessage(Msg);
	PlayChatInput->SetText(FText::GetEmpty());

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase 12i] 채팅 전송: %s"), *Msg);
}

void UHellunaLobbyStashWidget::OnPlayChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		OnPlayChatSendClicked();
	}
}

void UHellunaLobbyStashWidget::UpdatePlayChatVisibility()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	const bool bInParty = LobbyPC
		&& LobbyPC->CurrentPartyInfo.IsValid()
		&& LobbyPC->CurrentPartyInfo.Members.Num() >= 2;

	const ESlateVisibility ChatVis = bInParty
		? ESlateVisibility::Visible
		: ESlateVisibility::Collapsed;

	if (PlayChatBox)
	{
		PlayChatBox->SetVisibility(ChatVis);
	}
	if (Img_ChatBackground)
	{
		Img_ChatBackground->SetVisibility(ChatVis);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase 12i] 채팅 패널: %s"),
		bInParty ? TEXT("Visible") : TEXT("Collapsed"));
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 12j] 네임태그 오버레이
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::UpdateNameTagOverlays()
{
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (!LobbyPC)
	{
		HideAllNameTags();
		return;
	}

	const FHellunaPartyInfo& Info = LobbyPC->CurrentPartyInfo;
	const bool bInParty = Info.IsValid() && Info.Members.Num() >= 2;

	if (bInParty)
	{
		// 파티 모드 — 솔로 숨기고 슬롯 3개 표시
		if (NameTag_Solo)
		{
			NameTag_Solo->SetVisibility(ESlateVisibility::Collapsed);
		}

		// 슬롯 매핑: 리더→Slot1(중앙), 멤버1→Slot0(좌), 멤버2→Slot2(우)
		// (3D 프리뷰 HellunaCharacterSelectSceneV2::SetPartyPreview와 동일 배치)
		UVerticalBox* SlotWidgets[3] = { NameTag_Slot0, NameTag_Slot1, NameTag_Slot2 };

		// 모든 슬롯 초기화 (숨김)
		for (int32 i = 0; i < 3; ++i)
		{
			if (SlotWidgets[i]) SlotWidgets[i]->SetVisibility(ESlateVisibility::Collapsed);
		}

		// 리더 찾기
		int32 LeaderIdx = 0;
		for (int32 i = 0; i < Info.Members.Num(); ++i)
		{
			if (Info.Members[i].Role == EHellunaPartyRole::Leader)
			{
				LeaderIdx = i;
				break;
			}
		}

		// 멤버(리더 제외) 수집
		TArray<int32> MemberIndices;
		for (int32 i = 0; i < Info.Members.Num(); ++i)
		{
			if (i != LeaderIdx) MemberIndices.Add(i);
		}

		// 리더 → Slot1(중앙)
		if (SlotWidgets[1])
		{
			const auto& Leader = Info.Members[LeaderIdx];
			SetNameTagContent(SlotWidgets[1], Leader.DisplayName, Leader.bIsReady, true);
			SlotWidgets[1]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		// 멤버1 → Slot2(우) — 3D Slot0(Y=-200)이 화면 오른쪽이므로 위젯 Slot2에 매핑
		if (MemberIndices.Num() >= 1 && SlotWidgets[2])
		{
			const auto& M = Info.Members[MemberIndices[0]];
			SetNameTagContent(SlotWidgets[2], M.DisplayName, M.bIsReady, false);
			SlotWidgets[2]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		// 멤버2 → Slot0(좌) — 3D Slot2(Y=+200)이 화면 왼쪽이므로 위젯 Slot0에 매핑
		if (MemberIndices.Num() >= 2 && SlotWidgets[0])
		{
			const auto& M = Info.Members[MemberIndices[1]];
			SetNameTagContent(SlotWidgets[0], M.DisplayName, M.bIsReady, false);
			SlotWidgets[0]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase 12j] 파티 네임태그 %d명 표시"), Info.Members.Num());
	}
	else
	{
		// 솔로 모드 — 슬롯 숨기고 솔로 표시
		for (UVerticalBox* Tag : { NameTag_Slot0, NameTag_Slot1, NameTag_Slot2 })
		{
			if (Tag) Tag->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (NameTag_Solo)
		{
			const FString PlayerName = LobbyPC->GetPlayerId();
			SetNameTagContent(NameTag_Solo, PlayerName, false, false);
			NameTag_Solo->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase 12j] 솔로 네임태그 표시"));
	}
}

void UHellunaLobbyStashWidget::SetNameTagContent(UVerticalBox* NameTag, const FString& PlayerName, bool bIsReady, bool bIsLeader)
{
	if (!NameTag) return;

	// NameTag 내부 구조 (BP에서 생성):
	// VBox
	//   [0] HBox_NameRow
	//       [0] Image_LeaderStar (리더만 Visible)
	//       [1] Text_PlayerName
	//   [1] HBox_ReadyRow
	//       [0] Image_ReadyLED
	//       [1] Text_ReadyStatus
	if (NameTag->GetChildrenCount() < 2) return;

	UWidget* NameRow = NameTag->GetChildAt(0);
	UWidget* ReadyRow = NameTag->GetChildAt(1);

	// ── 닉네임 ──
	if (UHorizontalBox* HNameRow = Cast<UHorizontalBox>(NameRow))
	{
		if (HNameRow->GetChildrenCount() >= 2)
		{
			// [Phase 12j Fix] 리더 별 아이콘 — Image 대신 텍스트 접두사 사용
			UWidget* StarWidget = HNameRow->GetChildAt(0);
			if (StarWidget)
			{
				StarWidget->SetVisibility(ESlateVisibility::Collapsed); // Image 항상 숨김
			}

			// 닉네임 텍스트 — 리더는 "★ " 접두사
			if (UTextBlock* NameText = Cast<UTextBlock>(HNameRow->GetChildAt(1)))
			{
				const FString DisplayName = bIsLeader
					? FString::Printf(TEXT("\u2605 %s"), *PlayerName)
					: PlayerName;
				NameText->SetText(FText::FromString(DisplayName));

				// 리더 = 골드, 멤버 = 밝은 회색
				const FLinearColor NameColor = bIsLeader
					? FLinearColor(0.918f, 0.702f, 0.031f, 1.f)   // #EAB308
					: FLinearColor(0.63f, 0.63f, 0.67f, 1.f);     // #A1A1AA
				NameText->SetColorAndOpacity(FSlateColor(NameColor));
			}
		}
	}

	// ── Ready 상태 ──
	if (UHorizontalBox* HReadyRow = Cast<UHorizontalBox>(ReadyRow))
	{
		if (HReadyRow->GetChildrenCount() >= 2)
		{
			// Ready LED (Image)
			if (UImage* LED = Cast<UImage>(HReadyRow->GetChildAt(0)))
			{
				const FLinearColor LEDColor = bIsReady
					? FLinearColor(0.133f, 0.773f, 0.369f, 1.f)   // #22C55E
					: FLinearColor(0.322f, 0.322f, 0.353f, 1.f);  // #52525B
				LED->SetColorAndOpacity(LEDColor);
			}

			// Ready 텍스트
			if (UTextBlock* ReadyText = Cast<UTextBlock>(HReadyRow->GetChildAt(1)))
			{
				ReadyText->SetText(FText::FromString(bIsReady ? TEXT("READY") : TEXT("NOT READY")));

				const FLinearColor ReadyColor = bIsReady
					? FLinearColor(0.133f, 0.773f, 0.369f, 1.f)
					: FLinearColor(0.322f, 0.322f, 0.353f, 1.f);
				ReadyText->SetColorAndOpacity(FSlateColor(ReadyColor));
			}
		}
	}
}

void UHellunaLobbyStashWidget::HideAllNameTags()
{
	for (UVerticalBox* Tag : { NameTag_Slot0, NameTag_Slot1, NameTag_Slot2, NameTag_Solo })
	{
		if (Tag)
		{
			Tag->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// ============================================================================
// [Phase 15] 모드 토글 + 매칭 오버레이
// ============================================================================

void UHellunaLobbyStashWidget::OnSoloModeClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase18] Solo 모드 선택"));
	CurrentGameMode = ELobbyGameMode::Solo;
	UpdateModeButtonVisuals();
	UpdateStartButtonForPartyState();

	if (AHellunaLobbyController* LobbyPC = GetLobbyController())
	{
		LobbyPC->Server_SetGameMode(ELobbyGameMode::Solo);

		// 매칭 큐에 있으면 자동 취소
		if (bInMatchmaking)
		{
			LobbyPC->Server_LeaveMatchmaking();
		}
	}
}

void UHellunaLobbyStashWidget::OnDuoModeClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase18] Duo 모드 선택"));
	CurrentGameMode = ELobbyGameMode::Duo;
	UpdateModeButtonVisuals();
	UpdateStartButtonForPartyState();

	if (AHellunaLobbyController* LobbyPC = GetLobbyController())
	{
		LobbyPC->Server_SetGameMode(ELobbyGameMode::Duo);

		// 매칭 큐에 있으면 자동 취소
		if (bInMatchmaking)
		{
			LobbyPC->Server_LeaveMatchmaking();
		}
	}
}

void UHellunaLobbyStashWidget::OnPartyModeClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase18] Squad 모드 선택"));
	CurrentGameMode = ELobbyGameMode::Squad;
	UpdateModeButtonVisuals();
	UpdateStartButtonForPartyState();

	if (AHellunaLobbyController* LobbyPC = GetLobbyController())
	{
		LobbyPC->Server_SetGameMode(ELobbyGameMode::Squad);
	}
}

void UHellunaLobbyStashWidget::OnCancelMatchmakingClicked()
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase15] 매칭 취소 버튼 클릭"));
	AHellunaLobbyController* LobbyPC = GetLobbyController();
	if (LobbyPC)
	{
		LobbyPC->Server_LeaveMatchmaking();
	}
}

void UHellunaLobbyStashWidget::HandleMatchmakingStatusChanged(const FMatchmakingStatusInfo& StatusInfo)
{
	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase15] 매칭 상태 변경 | Status=%d | %.1fs | %d/%d"),
		static_cast<int32>(StatusInfo.Status), StatusInfo.ElapsedTime,
		StatusInfo.CurrentPlayerCount, StatusInfo.TargetPlayerCount);

	if (StatusInfo.Status == EMatchmakingStatus::Searching)
	{
		bInMatchmaking = true;

		if (MatchmakingOverlay)
		{
			MatchmakingOverlay->SetVisibility(ESlateVisibility::Visible);
			BP_OnMatchmakingOverlayShow();
			ConfigureSearchSpinnerVisuals();
			SetSearchSpinnerVisible(true);
		}
		if (Text_MatchmakingTimer)
		{
			const int32 Minutes = FMath::FloorToInt(StatusInfo.ElapsedTime / 60.f);
			const int32 Seconds = FMath::FloorToInt(FMath::Fmod(StatusInfo.ElapsedTime, 60.f));
			Text_MatchmakingTimer->SetText(FText::FromString(
				FString::Printf(TEXT("%02d:%02d"), Minutes, Seconds)));
		}
		if (Text_MatchmakingCount)
		{
			Text_MatchmakingCount->SetText(FText::FromString(
				FString::Printf(TEXT("%d/%d"), StatusInfo.CurrentPlayerCount, StatusInfo.TargetPlayerCount)));
		}

		// START 버튼 텍스트를 CANCEL로 변경
		UTextBlock* StartLabel = Text_StartLabel;
		if (!StartLabel && Button_Start && Button_Start->GetChildrenCount() > 0)
		{
			StartLabel = Cast<UTextBlock>(Button_Start->GetChildAt(0));
		}
		if (StartLabel)
		{
			StartLabel->SetText(FText::FromString(TEXT("CANCEL")));
		}
	}
	else
	{
		// None / Found / Deploying → 오버레이 숨김
		bInMatchmaking = false;

		if (MatchmakingOverlay)
		{
			MatchmakingOverlay->SetVisibility(ESlateVisibility::Collapsed);
			SetSearchSpinnerVisible(false);
		}

		// 버튼 텍스트 복원
		UpdateStartButtonForPartyState();
	}
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 17] 카운트다운 핸들러
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::HandleMatchmakingFound(const FMatchmakingFoundInfo& FoundInfo)
{
	// 매칭 오버레이 표시
	if (MatchmakingOverlay)
	{
		MatchmakingOverlay->SetVisibility(ESlateVisibility::Visible);
		BP_OnMatchmakingOverlayShow();
	}

	// 카운트다운 숫자 표시
	if (Text_Countdown)
	{
		Text_Countdown->SetVisibility(ESlateVisibility::Visible);
		Text_Countdown->SetText(FText::AsNumber(FoundInfo.CountdownSeconds));
		BP_OnCountdownTick(FoundInfo.CountdownSeconds);
	}

	// 매칭 타이머/카운트/취소 버튼 숨김 (카운트다운으로 대체)
	if (Text_MatchmakingTimer) Text_MatchmakingTimer->SetVisibility(ESlateVisibility::Collapsed);
	if (Text_MatchmakingCount) Text_MatchmakingCount->SetVisibility(ESlateVisibility::Collapsed);
	if (Button_CancelMatchmaking) Button_CancelMatchmaking->SetVisibility(ESlateVisibility::Collapsed);
	SetSearchSpinnerVisible(false);

	// 영웅 재배정 알림
	if (FoundInfo.bHeroWasReassigned && Text_HeroReassignNotice)
	{
		FString HeroName;
		switch (FoundInfo.AssignedHeroType)
		{
		case 0: HeroName = TEXT("루이 (Lui)"); break;
		case 1: HeroName = TEXT("루나 (Luna)"); break;
		case 2: HeroName = TEXT("리암 (Liam)"); break;
		default: HeroName = TEXT("알 수 없음"); break;
		}

		Text_HeroReassignNotice->SetText(
			FText::FromString(FString::Printf(
				TEXT("캐릭터가 중복되어 [%s](으)로 변경되었습니다"), *HeroName)));
		Text_HeroReassignNotice->SetVisibility(ESlateVisibility::Visible);
		BP_OnHeroReassignNotice();

		// 3초 후 자동 숨김
		if (UWorld* World = GetWorld())
		{
			FTimerHandle TempHandle;
			TWeakObjectPtr<UTextBlock> WeakNotice(Text_HeroReassignNotice);
			World->GetTimerManager().SetTimer(TempHandle,
				[WeakNotice]()
				{
					if (WeakNotice.IsValid())
					{
						WeakNotice->SetVisibility(ESlateVisibility::Collapsed);
					}
				},
				3.0f, false);
		}
	}

	bInMatchmaking = true;
}

void UHellunaLobbyStashWidget::HandleMatchmakingCountdown(int32 RemainingSeconds)
{
	if (Text_Countdown)
	{
		if (RemainingSeconds > 0)
		{
			Text_Countdown->SetText(FText::AsNumber(RemainingSeconds));
			BP_OnCountdownTick(RemainingSeconds);
		}
		else
		{
			Text_Countdown->SetText(FText::FromString(TEXT("서버 접속 중...")));
		}
	}
}

void UHellunaLobbyStashWidget::HandleMatchmakingCancelled(const FString& Reason)
{
	// 카운트다운 UI 숨김
	if (Text_Countdown) Text_Countdown->SetVisibility(ESlateVisibility::Collapsed);
	if (Text_HeroReassignNotice) Text_HeroReassignNotice->SetVisibility(ESlateVisibility::Collapsed);

	// Searching 상태 UI 복원
	if (Text_MatchmakingTimer) Text_MatchmakingTimer->SetVisibility(ESlateVisibility::Visible);
	if (Text_MatchmakingCount) Text_MatchmakingCount->SetVisibility(ESlateVisibility::Visible);
	if (Button_CancelMatchmaking) Button_CancelMatchmaking->SetVisibility(ESlateVisibility::Visible);
	SetSearchSpinnerVisible(false);

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase17] 카운트다운 취소 | %s"), *Reason);
}

void UHellunaLobbyStashWidget::UpdateModeButtonVisuals()
{
	if (Button_Mode_Solo)
	{
		Button_Mode_Solo->SetBackgroundColor(
			CurrentGameMode == ELobbyGameMode::Solo ? ActiveTabColor : InactiveTabColor);
	}
	if (Button_Mode_Duo)
	{
		Button_Mode_Duo->SetBackgroundColor(
			CurrentGameMode == ELobbyGameMode::Duo ? ActiveTabColor : InactiveTabColor);
	}
	if (Button_Mode_Squad)
	{
		Button_Mode_Squad->SetBackgroundColor(
			CurrentGameMode == ELobbyGameMode::Squad ? ActiveTabColor : InactiveTabColor);
	}
}

void UHellunaLobbyStashWidget::UpdateModeButtonsForPartySize(int32 PartySize)
{
	const ELobbyGameMode PrevMode = CurrentGameMode;

	if (PartySize <= 1)
	{
		// 솔로: [SOLO] [DUO] [SQUAD] 3등분
		if (Button_Mode_Solo)  Button_Mode_Solo->SetVisibility(ESlateVisibility::Visible);
		if (Button_Mode_Duo)   Button_Mode_Duo->SetVisibility(ESlateVisibility::Visible);
		if (Button_Mode_Squad) Button_Mode_Squad->SetVisibility(ESlateVisibility::Visible);
	}
	else if (PartySize == 2)
	{
		// 듀오: [DUO] [SQUAD] 반반 (SOLO 숨김)
		if (Button_Mode_Solo)  Button_Mode_Solo->SetVisibility(ESlateVisibility::Collapsed);
		if (Button_Mode_Duo)   Button_Mode_Duo->SetVisibility(ESlateVisibility::Visible);
		if (Button_Mode_Squad) Button_Mode_Squad->SetVisibility(ESlateVisibility::Visible);
		if (CurrentGameMode == ELobbyGameMode::Solo)
		{
			CurrentGameMode = ELobbyGameMode::Duo;
		}
	}
	else // PartySize >= 3
	{
		// 스쿼드: [SQUAD] 꽉참 (SOLO/DUO 숨김)
		if (Button_Mode_Solo)  Button_Mode_Solo->SetVisibility(ESlateVisibility::Collapsed);
		if (Button_Mode_Duo)   Button_Mode_Duo->SetVisibility(ESlateVisibility::Collapsed);
		if (Button_Mode_Squad) Button_Mode_Squad->SetVisibility(ESlateVisibility::Visible);
		CurrentGameMode = ELobbyGameMode::Squad;
	}

	// [Phase 18] 모드가 자동 변경되었으면 서버에 동기화
	if (CurrentGameMode != PrevMode)
	{
		if (AHellunaLobbyController* LobbyPC = GetLobbyController())
		{
			LobbyPC->Server_SetGameMode(CurrentGameMode);
		}
	}

	UpdateModeButtonVisuals();
	UpdateStartButtonForPartyState();

	UE_LOG(LogHellunaLobby, Log,
		TEXT("[StashWidget] [Phase18] 모드 버튼 갱신 | PartySize=%d → Mode=%d"),
		PartySize, static_cast<int32>(CurrentGameMode));
}

// ════════════════════════════════════════════════════════════════════════════════
// [Phase 17 + 17.1] PUBG식 맵 선택 카드 + 팝업
// ════════════════════════════════════════════════════════════════════════════════

void UHellunaLobbyStashWidget::InitializeMapSelector()
{
	// 팝업 내 화살표 버튼 바인딩
	if (Button_MapPrev)
	{
		Button_MapPrev->OnClicked.AddUniqueDynamic(this, &ThisClass::OnMapPrevClicked);
	}
	if (Button_MapNext)
	{
		Button_MapNext->OnClicked.AddUniqueDynamic(this, &ThisClass::OnMapNextClicked);
	}

	// LobbyGameMode에서 맵 목록 캐시 (서버)
	if (UWorld* World = GetWorld())
	{
		if (AHellunaLobbyGameMode* LobbyGM = Cast<AHellunaLobbyGameMode>(World->GetAuthGameMode()))
		{
			CachedMapConfigs = LobbyGM->AvailableMapConfigs;
			SelectedMapKey = LobbyGM->DefaultMapKey;
		}
	}

	// [Hotfix] 클라이언트 폴백: BP CDO에서 맵 목록 로드
	if (CachedMapConfigs.Num() == 0)
	{
		UClass* GMClass = StaticLoadClass(
			AHellunaLobbyGameMode::StaticClass(), nullptr,
			TEXT("/Game/Gihyeon/Lobby/BP_HellunaLobbyGameMode.BP_HellunaLobbyGameMode_C"));
		if (GMClass)
		{
			if (AHellunaLobbyGameMode* GMCDO = Cast<AHellunaLobbyGameMode>(GMClass->GetDefaultObject()))
			{
				CachedMapConfigs = GMCDO->AvailableMapConfigs;
				SelectedMapKey = GMCDO->DefaultMapKey;
				UE_LOG(LogHellunaLobby, Log,
					TEXT("[StashWidget] [Hotfix] 클라이언트 폴백: BP CDO에서 맵 %d개 로드"),
					CachedMapConfigs.Num());
			}
		}
	}

	// DefaultMapKey에 해당하는 인덱스 찾기
	for (int32 i = 0; i < CachedMapConfigs.Num(); ++i)
	{
		if (CachedMapConfigs[i].MapKey == SelectedMapKey)
		{
			CurrentMapIndex = i;
			break;
		}
	}

	// [Phase 17.1] 작은 카드 초기 표시
	UpdateSmallCardDisplay();

	// 초기 맵 선택 서버 동기화
	if (CachedMapConfigs.IsValidIndex(CurrentMapIndex))
	{
		SelectedMapKey = CachedMapConfigs[CurrentMapIndex].MapKey;
		if (AHellunaLobbyController* LobbyPC = GetLobbyController())
		{
			LobbyPC->Server_SetSelectedMap(SelectedMapKey);
		}
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase17] 맵 선택 카드 초기화 완료 | Maps=%d | Current=%s"),
		CachedMapConfigs.Num(), *SelectedMapKey);
}

// ── 팝업 내 화살표 (PopupBrowsingIndex 순환) ──

void UHellunaLobbyStashWidget::OnMapPrevClicked()
{
	if (CachedMapConfigs.Num() == 0) return;

	PopupBrowsingIndex = (PopupBrowsingIndex - 1 + CachedMapConfigs.Num()) % CachedMapConfigs.Num();
	UpdateMapDisplay();
}

void UHellunaLobbyStashWidget::OnMapNextClicked()
{
	if (CachedMapConfigs.Num() == 0) return;

	PopupBrowsingIndex = (PopupBrowsingIndex + 1) % CachedMapConfigs.Num();
	UpdateMapDisplay();
}

// ── 팝업 내부 맵 정보 업데이트 (Server RPC 호출 안 함) ──

void UHellunaLobbyStashWidget::UpdateMapDisplay()
{
	if (!CachedMapConfigs.IsValidIndex(PopupBrowsingIndex))
	{
		return;
	}

	const FHellunaGameMapInfo& MapInfo = CachedMapConfigs[PopupBrowsingIndex];

	// 팝업 썸네일 업데이트
	if (Popup_MapThumbnail)
	{
		if (!MapInfo.MapThumbnail.IsNull())
		{
			UTexture2D* LoadedTexture = MapInfo.MapThumbnail.LoadSynchronous();
			if (LoadedTexture)
			{
				Popup_MapThumbnail->SetBrushFromTexture(LoadedTexture);
				Popup_MapThumbnail->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			else
			{
				Popup_MapThumbnail->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else
		{
			Popup_MapThumbnail->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 팝업 맵 이름 업데이트
	if (Popup_MapName)
	{
		Popup_MapName->SetText(FText::FromString(MapInfo.DisplayName));
	}

	// 팝업 맵 설명 (선택적 — 추후 FHellunaGameMapInfo에 Description 추가 시 활용)
	if (Popup_MapDescription)
	{
		Popup_MapDescription->SetText(FText::GetEmpty());
	}

	// [Phase 17.1] 도트 인디케이터 업데이트
	if (Popup_MapIndicator)
	{
		FString Dots;
		for (int32 i = 0; i < CachedMapConfigs.Num(); ++i)
		{
			if (i > 0) Dots += TEXT("  ");
			Dots += (i == PopupBrowsingIndex) ? TEXT("\u25CF") : TEXT("\u25CB");
		}
		Popup_MapIndicator->SetText(FText::FromString(Dots));
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase17.1] 팝업 맵 탐색: [%d] %s (%s)"),
		PopupBrowsingIndex, *MapInfo.DisplayName, *MapInfo.MapKey);
}

// ── [Phase 17.1] 작은 카드 표시 ──

void UHellunaLobbyStashWidget::UpdateSmallCardDisplay()
{
	if (!CachedMapConfigs.IsValidIndex(CurrentMapIndex))
	{
		if (MapSelectContainer)
		{
			MapSelectContainer->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	const FHellunaGameMapInfo& MapInfo = CachedMapConfigs[CurrentMapIndex];

	// 작은 카드 썸네일
	if (Img_MapThumbnail)
	{
		if (!MapInfo.MapThumbnail.IsNull())
		{
			UTexture2D* LoadedTexture = MapInfo.MapThumbnail.LoadSynchronous();
			if (LoadedTexture)
			{
				Img_MapThumbnail->SetBrushFromTexture(LoadedTexture);
				Img_MapThumbnail->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
			else
			{
				Img_MapThumbnail->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		else
		{
			Img_MapThumbnail->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 작은 카드 맵 이름
	if (Text_MapName)
	{
		Text_MapName->SetText(FText::FromString(MapInfo.DisplayName));
	}

	// 컨테이너 표시
	if (MapSelectContainer)
	{
		MapSelectContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

// ── [Phase 17.1] 팝업 열기/닫기 ──

void UHellunaLobbyStashWidget::OnMapCardClicked()
{
	OpenMapSelectPopup();
}

void UHellunaLobbyStashWidget::OpenMapSelectPopup()
{
	// 팝업 열 때 현재 확정된 맵으로 브라우징 인덱스 초기화
	PopupBrowsingIndex = CurrentMapIndex;
	UpdateMapDisplay();

	if (MapSelectPopupOverlay)
	{
		MapSelectPopupOverlay->SetVisibility(ESlateVisibility::Visible);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase17.1] 맵 선택 팝업 열림 | CurrentMap=[%d] %s"),
		CurrentMapIndex, *SelectedMapKey);
}

void UHellunaLobbyStashWidget::CloseMapSelectPopup()
{
	if (MapSelectPopupOverlay)
	{
		MapSelectPopupOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase17.1] 맵 선택 팝업 닫힘"));
}

// ── [Phase 17.1] 팝업 확인/취소 ──

void UHellunaLobbyStashWidget::OnMapConfirmClicked()
{
	// 팝업에서 탐색한 인덱스를 확정
	CurrentMapIndex = PopupBrowsingIndex;

	if (CachedMapConfigs.IsValidIndex(CurrentMapIndex))
	{
		SelectedMapKey = CachedMapConfigs[CurrentMapIndex].MapKey;

		// 서버 RPC는 확정 시에만 호출
		if (AHellunaLobbyController* LobbyPC = GetLobbyController())
		{
			LobbyPC->Server_SetSelectedMap(SelectedMapKey);
		}

		UE_LOG(LogHellunaLobby, Log, TEXT("[StashWidget] [Phase17.1] 맵 선택 확정: [%d] %s (%s)"),
			CurrentMapIndex, *CachedMapConfigs[CurrentMapIndex].DisplayName, *SelectedMapKey);
	}

	// 작은 카드 업데이트
	UpdateSmallCardDisplay();

	// 팝업 닫기
	CloseMapSelectPopup();
}

void UHellunaLobbyStashWidget::OnCloseMapPopupClicked()
{
	// 취소 — 아무것도 변경하지 않고 닫기
	CloseMapSelectPopup();
}
