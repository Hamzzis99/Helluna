// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyStashWidget.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 메인 듀얼 위젯 — Stash + Loadout 양쪽 패널 + 출격 버튼
//
// 📌 레이아웃:
//    ┌─── Stash Panel ──────┐  ┌── Loadout (SpatialInventory) ──┐
//    │ [장비][소모품][재료]  │  │  [장착슬롯: 무기/방어구/...]   │
//    │ ┌──────────────────┐ │  │  ┌──────────────────────────┐  │
//    │ │                  │ │  │  │ [장비][소모품][재료]      │  │
//    │ │   Grid (탭별)    │ │  │  │   Grid (탭별) + 장착슬롯  │  │
//    │ │                  │ │  │  └──────────────────────────┘  │
//    │ └──────────────────┘ │  │  → LoadoutComp 바인딩          │
//    └──────────────────────┘  └────────────────────────────────┘
//                        [ 출격 버튼 ]
//
// 📌 아이템 전송:
//    1차: 버튼 전송 (우클릭 → "Loadout으로 보내기" / "Stash로 보내기")
//    2차: 드래그앤드롭 크로스 패널 (Phase 4-5)
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
enum class EHellunaHeroType : uint8;

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

	/** 인벤토리 페이지로 전환 (캐릭터 선택 완료 후) */
	UFUNCTION(BlueprintCallable, Category = "로비|창고위젯",
		meta = (DisplayName = "인벤토리 페이지로 전환"))
	void SwitchToInventoryPage();

protected:
	// ════════════════════════════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ════════════════════════════════════════════════════════════════

	/** 메인 WidgetSwitcher — Page0=캐릭터선택, Page1=인벤토리 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> MainSwitcher;

	/** 캐릭터 선택 패널 (Page 0) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHellunaLobbyCharSelectWidget> CharacterSelectPanel;

	/** Stash 패널 (좌측, Page 1 내부) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHellunaLobbyPanel> StashPanel;

	/** Loadout SpatialInventory (우측) — 인게임과 동일한 장착 슬롯 + 3탭 Grid */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_SpatialInventory> LoadoutSpatialInventory;

	/** 출격 버튼 (하단) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Deploy;

private:
	// ════════════════════════════════════════════════════════════════
	// 출격 버튼 콜백
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

	// ════════════════════════════════════════════════════════════════
	// 내부 헬퍼
	// ════════════════════════════════════════════════════════════════

	/** 현재 LobbyController 가져오기 */
	AHellunaLobbyController* GetLobbyController() const;

	/** 캐릭터 선택 완료 핸들러 */
	UFUNCTION()
	void OnCharacterSelectedHandler(EHellunaHeroType SelectedHero);

	// 바인딩된 컴포넌트 캐시
	TWeakObjectPtr<UInv_InventoryComponent> CachedStashComp;
	TWeakObjectPtr<UInv_InventoryComponent> CachedLoadoutComp;
};
