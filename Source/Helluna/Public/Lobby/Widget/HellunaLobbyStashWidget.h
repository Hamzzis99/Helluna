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
	void TransferItemToLoadout(int32 ItemEntryIndex);

	/**
	 * Loadout → Stash 아이템 전송
	 * Server RPC를 통해 서버에서 실행
	 *
	 * @param ItemEntryIndex  전송할 아이템의 Entry 인덱스
	 *
	 * TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|창고위젯",
		meta = (DisplayName = "창고로 아이템 전송"))
	void TransferItemToStash(int32 ItemEntryIndex);

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
	// BindWidget — BP에서 연결
	// ════════════════════════════════════════════════════════════════

	/** 메인 WidgetSwitcher — Page0=Play, Page1=Loadout, Page2=Character */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> MainSwitcher;

	// ── 탑 네비게이션 탭 버튼 ──
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Tab_Play;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Tab_Loadout;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Tab_Character;

	// ── Play 탭 (Page 0) ──
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Start;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_NoCharWarning;

	// ── Loadout 탭 (Page 1) — 기존 ──
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHellunaLobbyPanel> StashPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_SpatialInventory> LoadoutSpatialInventory;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Deploy;

	// ── Character 탭 (Page 2) — 기존 ──
	UPROPERTY(meta = (BindWidget))
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
	void OnStashItemTransferRequested(int32 EntryIndex);

	/** Loadout Grid에서 우클릭 → Stash로 전송 */
	UFUNCTION()
	void OnLoadoutItemTransferRequested(int32 EntryIndex);

	/** [CrossSwap] 크로스 Grid Swap 핸들러 */
	UFUNCTION()
	void OnCrossSwapRequested(int32 RepID_A, int32 RepID_B);

	// ════════════════════════════════════════════════════════════════
	// 내부 헬퍼
	// ════════════════════════════════════════════════════════════════

	/** 현재 LobbyController 가져오기 */
	AHellunaLobbyController* GetLobbyController() const;

	/** 캐릭터 선택 완료 핸들러 */
	UFUNCTION()
	void OnCharacterSelectedHandler(EHellunaHeroType SelectedHero);

	// ════════════════════════════════════════════════════════════════
	// 내부 상태
	// ════════════════════════════════════════════════════════════════

	// 현재 활성 탭 인덱스
	int32 CurrentTabIndex = LobbyTab::Play;

	// 프리뷰 씬 캐시 (Solo 모드 전환용)
	TWeakObjectPtr<AHellunaCharacterSelectSceneV2> CachedPreviewScene;

	// 바인딩된 컴포넌트 캐시
	TWeakObjectPtr<UInv_InventoryComponent> CachedStashComp;
	TWeakObjectPtr<UInv_InventoryComponent> CachedLoadoutComp;
};
