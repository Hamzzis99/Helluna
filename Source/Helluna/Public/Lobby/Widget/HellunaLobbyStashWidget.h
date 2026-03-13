// ════════════════════════════════════════════════════════════════════════════════
// File: Source/Helluna/Public/Lobby/Widget/HellunaLobbyStashWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 메인 위젯 — 탑 네비게이션 바 + 3탭 (Play / Loadout / Character)
//
// 📌 레이아웃 (Phase 번외 리팩토링):
//    ┌─────────────────────────────────────────────────────────┐
//    │  [PLAY]  [LOADOUT]  [CHARACTER]           TopNavBar     │
//    ├─────────────────────────────────────────────────────────┤
//    │  Page 0: PlayPage      — 캐릭터 프리뷰 + 맵 카드 + START│
//    │  Page 1: LoadoutPage   — Stash + Loadout + Deploy (기존) │
//    │  Page 2: CharacterPage — 캐릭터 선택 (기존)              │
//    └─────────────────────────────────────────────────────────┘
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Lobby/Party/HellunaPartyTypes.h"
#include "Lobby/Party/HellunaMatchmakingTypes.h"
#include "HellunaLobbyStashWidget.generated.h"

// 전방 선언
class UHellunaLobbyPanel;
class UInv_SpatialInventory;
class UInv_InventoryComponent;
class UInv_InventoryItem;
class UButton;
class UWidgetSwitcher;
class UHellunaLobbyCharSelectWidget;
class AHellunaLobbyController;
class AHellunaCharacterSelectSceneV2;
class UImage;
class UTextBlock;
class UScrollBox;
class UEditableTextBox;
class UVerticalBox;
class UWidgetAnimation;
enum class EHellunaHeroType : uint8;

// 탭 인덱스 상수
namespace LobbyTab
{
	constexpr int32 Play      = 0;
	constexpr int32 Loadout   = 1;
	constexpr int32 Character = 2;
}

UCLASS()
class HELLUNA_API UHellunaLobbyStashWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	// ════════════════════════════════════════════════════════════════
	// 초기화
	// ════════════════════════════════════════════════════════════════

	/**
	 * 양쪽 패널을 각각의 InvComp와 바인딩
	 *
	 * @param StashComp    Stash InventoryComponent
	 * @param LoadoutComp  Loadout InventoryComponent
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|창고위젯",
		meta = (DisplayName = "패널 초기화"))
	void InitializePanels(UInv_InventoryComponent* StashComp, UInv_InventoryComponent* LoadoutComp);

	// ════════════════════════════════════════════════════════════════
	// 아이템 전송 (1차: 버튼 기반)
	// ════════════════════════════════════════════════════════════════

	/**
	 * Stash → Loadout 아이템 전송
	 * Server RPC를 통해 서버에서 실행
	 *
	 * @param ItemEntryIndex  전송할 아이템의 Entry 인덱스
	 *
	 * TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|창고위젯",
		meta = (DisplayName = "출격장비로 아이템 전송"))
	void TransferItemToLoadout(int32 ItemEntryIndex, int32 TargetGridIndex = -1);

	/**
	 * Loadout → Stash 아이템 전송
	 * Server RPC를 통해 서버에서 실행
	 *
	 * @param ItemEntryIndex   전송할 아이템의 Entry 인덱스
	 * @param TargetGridIndex  대상 Grid 위치 (INDEX_NONE이면 서버 자동 배치)
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|창고위젯",
		meta = (DisplayName = "창고로 아이템 전송"))
	void TransferItemToStash(int32 ItemEntryIndex, int32 TargetGridIndex = -1);

	// ════════════════════════════════════════════════════════════════
	// 패널 접근
	// ════════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|창고위젯")
	UHellunaLobbyPanel* GetStashPanel() const { return StashPanel; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|창고위젯")
	UInv_SpatialInventory* GetLoadoutSpatialInventory() const { return LoadoutSpatialInventory; }

	/** 캐릭터 선택 패널 접근 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "로비|창고위젯")
	UHellunaLobbyCharSelectWidget* GetCharacterSelectPanel() const { return CharacterSelectPanel; }

	/** 인벤토리 페이지로 전환 (하위호환 — 내부적으로 SwitchToTab(Loadout) 호출) */
	UFUNCTION(BlueprintCallable, Category = "로비|창고위젯",
		meta = (DisplayName = "인벤토리 페이지로 전환"))
	void SwitchToInventoryPage();

	// ════════════════════════════════════════════════════════════════
	// 탭 네비게이션
	// ════════════════════════════════════════════════════════════════

	/** 탭 전환 (LobbyTab::Play=0, Loadout=1, Character=2) */
	UFUNCTION(BlueprintCallable, Category = "로비|네비게이션",
		meta = (DisplayName = "Switch To Tab (탭 전환)"))
	void SwitchToTab(int32 TabIndex);

	// ════════════════════════════════════════════════════════════════
	// 중앙 프리뷰 설정
	// ════════════════════════════════════════════════════════════════

	/** Play 탭의 캐릭터 프리뷰 씬 캐시 설정 (ShowLobbyWidget에서 호출, 직접 뷰포트 모드) */
	UFUNCTION(BlueprintCallable, Category = "로비|프리뷰",
		meta = (DisplayName = "Setup Center Preview (중앙 프리뷰 설정)"))
	void SetupCenterPreview(AHellunaCharacterSelectSceneV2* InPreviewScene);

	/** 캐릭터 선택 여부 */
	bool IsCharacterSelected() const;

protected:
	// ════════════════════════════════════════════════════════════════
	// BP 이벤트 — 애니메이션 재생용
	// ════════════════════════════════════════════════════════════════

	/** 매칭 오버레이가 표시될 때 호출 (BP에서 페이드인 애니메이션 재생) */
	UFUNCTION(BlueprintImplementableEvent, Category = "로비|매칭",
		meta = (DisplayName = "On Matchmaking Overlay Show (매칭 오버레이 표시)"))
	void BP_OnMatchmakingOverlayShow();

	/** 카운트다운 숫자가 변경될 때 호출 (BP에서 바운스 애니메이션 재생) */
	UFUNCTION(BlueprintImplementableEvent, Category = "로비|매칭",
		meta = (DisplayName = "On Countdown Tick (카운트다운 틱)"))
	void BP_OnCountdownTick(int32 RemainingSeconds);

	/** 히어로 재배정 알림이 표시될 때 호출 (BP에서 슬라이드업 애니메이션 재생) */
	UFUNCTION(BlueprintImplementableEvent, Category = "로비|매칭",
		meta = (DisplayName = "On Hero Reassign Notice (히어로 재배정 알림)"))
	void BP_OnHeroReassignNotice();

	// ════════════════════════════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ════════════════════════════════════════════════════════════════

	/** 메인 WidgetSwitcher — Page0=Play, Page1=Loadout, Page2=Character */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> MainSwitcher;

	// ── 탑 네비게이션 탭 버튼 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Tab_Play;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Tab_Loadout;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Tab_Character;

	// ── Play 탭 (Page 0) ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Start;

	/** [Phase 12g] 파티 팝업 열기 버튼 (선택적 — WBP에 없으면 무시) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Party;

	/** [Phase 12h] START 버튼 자식 TextBlock — 없으면 GetChildAt(0)으로 탐색 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_StartLabel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_NoCharWarning;

	// ── [Phase 12i] Play 탭 파티 채팅 ──

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> PlayChatScrollBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> PlayChatInput;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayChatSendButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_ChatBackground;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> PlayChatBox;

	// ── [Phase 12j] 캐릭터 네임태그 오버레이 ──
	// 3개의 네임태그 컨테이너 (파티 슬롯 0=좌, 1=중앙(리더), 2=우)

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> NameTag_Slot0;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> NameTag_Slot1;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> NameTag_Slot2;

	// 솔로 모드 네임태그
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> NameTag_Solo;

	// ── [Phase 15] 모드 토글 + 매칭 오버레이 ──

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Mode_Solo;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Mode_Duo;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Mode_Squad;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> MatchmakingOverlay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Overlay_SearchSpinner;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_SearchRingBackdrop;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_SearchRingOuter;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Image_SearchRingInner;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_MatchmakingTimer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_MatchmakingCount;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_CancelMatchmaking;

	// ── [Phase 17] 위젯 애니메이션 바인딩 ──

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetAnimOptional), Transient)
	TObjectPtr<UWidgetAnimation> Anim_MatchmakingFadeIn;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetAnimOptional), Transient)
	TObjectPtr<UWidgetAnimation> Anim_CountdownBounce;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetAnimOptional), Transient)
	TObjectPtr<UWidgetAnimation> Anim_ReassignNotice;

	// ── [Phase 17] 카운트다운 UI ──

	/** 카운트다운 숫자 텍스트 ("5", "4", "3", "2", "1") */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Countdown;

	/** 영웅 재배정 알림 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_HeroReassignNotice;

	// ── [Phase 17] PUBG식 맵 선택 카드 ──

	/** 맵 썸네일 이미지 (크게) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_MapThumbnail;

	/** 맵 이름 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_MapName;

	/** 왼쪽 화살표 버튼 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_MapPrev;

	/** 오른쪽 화살표 버튼 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_MapNext;

	/** 맵 선택 컨테이너 (왼쪽 아래 배치용) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> MapSelectContainer;

	// ── [Phase 17.1] 맵 카드 클릭 → 팝업 ──

	/** MapCardPanel 안의 클릭 가능 버튼 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_MapCard;

	/** 맵 선택 팝업 오버레이 (전체 화면 반투명 배경 + 중앙 카드) — 초기 Collapsed */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> MapSelectPopupOverlay;

	/** 팝업 내 큰 썸네일 이미지 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> Popup_MapThumbnail;

	/** 팝업 내 맵 이름 텍스트 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Popup_MapName;

	/** 팝업 내 맵 설명 텍스트 (선택적) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Popup_MapDescription;

	/** 선택 확정 버튼 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_MapConfirm;

	/** 팝업 닫기 버튼 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_CloseMapPopup;

	/** [Phase 17.1] 맵 도트 인디케이터 (● ○ 형태) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Popup_MapIndicator;

	// ── Loadout 탭 (Page 1) — 기존 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHellunaLobbyPanel> StashPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UInv_SpatialInventory> LoadoutSpatialInventory;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Deploy;

	// ── Character 탭 (Page 2) — 기존 ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHellunaLobbyCharSelectWidget> CharacterSelectPanel;

	// ════════════════════════════════════════════════════════════════
	// 탭 스타일 (BP Class Defaults에서 지정)
	// ════════════════════════════════════════════════════════════════

	UPROPERTY(EditDefaultsOnly, Category = "로비|탭 스타일",
		meta = (DisplayName = "Active Tab Color (활성 탭 색상)"))
	FLinearColor ActiveTabColor = FLinearColor(1.f, 0.8f, 0.f, 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "로비|탭 스타일",
		meta = (DisplayName = "Inactive Tab Color (비활성 탭 색상)"))
	FLinearColor InactiveTabColor = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);

private:
	// ════════════════════════════════════════════════════════════════
	// 탭 네비게이션 콜백
	// ════════════════════════════════════════════════════════════════

	UFUNCTION()
	void OnTabPlayClicked();

	UFUNCTION()
	void OnTabLoadoutClicked();

	UFUNCTION()
	void OnTabCharacterClicked();

	/** 탭 버튼 비주얼 업데이트 (활성/비활성 색상) */
	void UpdateTabVisuals(int32 ActiveTabIndex);

	// ════════════════════════════════════════════════════════════════
	// Play 탭 — START 버튼 콜백
	// ════════════════════════════════════════════════════════════════

	UFUNCTION()
	void OnStartClicked();

	// ════════════════════════════════════════════════════════════════
	// 출격 버튼 콜백 (Loadout 탭)
	// ════════════════════════════════════════════════════════════════

	UFUNCTION()
	void OnDeployClicked();

	// ════════════════════════════════════════════════════════════════
	// [Phase 4 Fix] 우클릭 전송 핸들러
	// ════════════════════════════════════════════════════════════════

	/** Stash Grid에서 우클릭 → Loadout으로 전송 */
	UFUNCTION()
	void OnStashItemTransferRequested(int32 EntryIndex, int32 TargetGridIndex);

	/** Loadout Grid에서 우클릭 → Stash로 전송 */
	UFUNCTION()
	void OnLoadoutItemTransferRequested(int32 EntryIndex, int32 TargetGridIndex);

	/** [CrossSwap] 크로스 Grid Swap 핸들러 */
	UFUNCTION()
	void OnCrossSwapRequested(int32 RepID_A, int32 RepID_B, int32 TargetGridIndex);

	// ════════════════════════════════════════════════════════════════
	// 내부 헬퍼
	// ════════════════════════════════════════════════════════════════

	/** 현재 LobbyController 가져오기 */
	AHellunaLobbyController* GetLobbyController() const;

	/** 캐릭터 선택 완료 핸들러 */
	UFUNCTION()
	void OnCharacterSelectedHandler(EHellunaHeroType SelectedHero);

	/** [Phase 12g] 파티 버튼 클릭 콜백 */
	UFUNCTION()
	void OnPartyClicked();

	// ════════════════════════════════════════════════════════════════
	// [Phase 12h] START/READY 버튼 전환
	// ════════════════════════════════════════════════════════════════

	/** 파티 상태 변경 시 버튼 텍스트 갱신 */
	UFUNCTION()
	void OnPartyStateChangedHandler(const FHellunaPartyInfo& PartyInfo);

	/** START/READY 버튼 텍스트 업데이트 */
	void UpdateStartButtonForPartyState();

	// ════════════════════════════════════════════════════════════════
	// [Phase 12i] Play 탭 파티 채팅
	// ════════════════════════════════════════════════════════════════

	/** 파티 채팅 메시지 수신 핸들러 */
	UFUNCTION()
	void HandlePlayChatReceived(const FHellunaPartyChatMessage& ChatMessage);

	/** Play 탭 채팅 전송 */
	UFUNCTION()
	void OnPlayChatSendClicked();

	/** Play 탭 채팅 Enter 키 전송 */
	UFUNCTION()
	void OnPlayChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** Play 탭 채팅 메시지를 ScrollBox에 추가 */
	void AddPlayChatMessage(const FHellunaPartyChatMessage& ChatMessage);

	/** 파티 상태에 따라 채팅 패널 표시/숨김 */
	void UpdatePlayChatVisibility();

	// ════════════════════════════════════════════════════════════════
	// [Phase 12j] 네임태그 갱신
	// ════════════════════════════════════════════════════════════════

	/** 파티 상태에 따라 네임태그 업데이트 */
	void UpdateNameTagOverlays();

	/** 개별 슬롯 네임태그 설정 */
	void SetNameTagContent(UVerticalBox* NameTag, const FString& PlayerName, bool bIsReady, bool bIsLeader);

	/** 네임태그 전부 숨기기 */
	void HideAllNameTags();

	// ════════════════════════════════════════════════════════════════
	// [Phase 15] 모드 토글 + 매칭 오버레이
	// ════════════════════════════════════════════════════════════════

	UFUNCTION()
	void OnSoloModeClicked();

	UFUNCTION()
	void OnDuoModeClicked();

	UFUNCTION()
	void OnPartyModeClicked();

	UFUNCTION()
	void OnCancelMatchmakingClicked();

	/** [Phase 18] 파티 인원 수에 따라 모드 버튼 Visible/Collapsed + 자동 모드 전환 */
	void UpdateModeButtonsForPartySize(int32 PartySize);

	/** 매칭 상태 변경 핸들러 */
	UFUNCTION()
	void HandleMatchmakingStatusChanged(const FMatchmakingStatusInfo& StatusInfo);

	void ConfigureSearchSpinnerVisuals();
	void SetSearchSpinnerVisible(bool bVisible);
	bool ShouldAnimateSearchSpinner() const;

	// ── [Phase 17] 카운트다운 핸들러 ──

	/** 매칭 완료 핸들러 — 카운트다운 시작 + 프리뷰 전환 */
	UFUNCTION()
	void HandleMatchmakingFound(const FMatchmakingFoundInfo& FoundInfo);

	/** 카운트다운 틱 핸들러 — 숫자 갱신 */
	UFUNCTION()
	void HandleMatchmakingCountdown(int32 RemainingSeconds);

	/** 카운트다운 취소 핸들러 — UI 복원 */
	UFUNCTION()
	void HandleMatchmakingCancelled(const FString& Reason);

	/** 모드 버튼 비주얼 업데이트 */
	void UpdateModeButtonVisuals();

	// ── [Phase 17] 맵 선택 카드 ──

	/** 맵 목록 캐시 + 기본맵 표시 */
	void InitializeMapSelector();

	/** 왼쪽 화살표 클릭 — 팝업 내 맵 순환 */
	UFUNCTION()
	void OnMapPrevClicked();

	/** 오른쪽 화살표 클릭 — 팝업 내 맵 순환 */
	UFUNCTION()
	void OnMapNextClicked();

	/** 팝업 내부 맵 정보 업데이트 (Server RPC 호출 안 함) */
	void UpdateMapDisplay();

	// ── [Phase 17.1] 맵 선택 팝업 ──

	/** 작은 카드(MapCardPanel)의 썸네일/이름 업데이트 */
	void UpdateSmallCardDisplay();

	/** 맵 카드 클릭 → 팝업 열기 */
	UFUNCTION()
	void OnMapCardClicked();

	void OpenMapSelectPopup();
	void CloseMapSelectPopup();

	/** 팝업 확인 → 맵 확정 + Server RPC + 작은 카드 갱신 */
	UFUNCTION()
	void OnMapConfirmClicked();

	/** 팝업 닫기(취소) */
	UFUNCTION()
	void OnCloseMapPopupClicked();

	// ════════════════════════════════════════════════════════════════
	// 내부 상태
	// ════════════════════════════════════════════════════════════════

	// 현재 활성 탭 인덱스
	int32 CurrentTabIndex = LobbyTab::Play;

	/** [Phase 12h] 로컬 플레이어의 현재 Ready 상태 캐시 */
	bool bLocalPlayerReady = false;

	/** [Phase 18] 현재 게임 모드 */
	ELobbyGameMode CurrentGameMode = ELobbyGameMode::Solo;

	/** [Phase 15] 현재 매칭 큐에 있는지 */
	bool bInMatchmaking = false;

	/** [Phase 16] 현재 선택된 맵 키 */
	FString SelectedMapKey;

	/** [Phase 17] 현재 확정된 맵 인덱스 */
	int32 CurrentMapIndex = 0;

	/** [Phase 17.1] 팝업에서 탐색 중인 임시 인덱스 (확인 전까지 SelectedMapKey에 영향 없음) */
	int32 PopupBrowsingIndex = 0;

	bool bSearchSpinnerConfigured = false;
	float SearchRingOuterAngle = 0.0f;
	float SearchRingInnerAngle = 0.0f;

	/** [Phase 17] 맵 목록 로컬 캐시 */
	TArray<FHellunaGameMapInfo> CachedMapConfigs;

	// 프리뷰 씬 캐시 (Solo 모드 전환용)
	TWeakObjectPtr<AHellunaCharacterSelectSceneV2> CachedPreviewScene;

	// 바인딩된 컴포넌트 캐시
	TWeakObjectPtr<UInv_InventoryComponent> CachedStashComp;
	TWeakObjectPtr<UInv_InventoryComponent> CachedLoadoutComp;
};
