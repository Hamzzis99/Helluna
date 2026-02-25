// ════════════════════════════════════════════════════════════════════════════════
// HellunaLobbyPanel.h
// ════════════════════════════════════════════════════════════════════════════════
//
// 로비 인벤토리 패널 (단일 패널) — Stash 또는 Loadout 한쪽을 담당
//
// 📌 구조:
//    ┌──────────────────────────────────┐
//    │ [장비] [소모품] [재료]  ← 탭 버튼 │
//    │ ┌──────────────────────────────┐ │
//    │ │                              │ │
//    │ │    Grid (탭별 WidgetSwitcher) │ │
//    │ │                              │ │
//    │ └──────────────────────────────┘ │
//    └──────────────────────────────────┘
//
// 📌 Inv_SpatialInventory와의 차이:
//    - 장착 슬롯(EquippedGridSlot) 없음 (로비에서는 장착 불가)
//    - 아이템 설명(ItemDescription) 없음 (간소화)
//    - SetInventoryComponent()로 외부 InvComp 바인딩 (bSkipAutoInit)
//
// 📌 BP 바인딩:
//    WBP_HellunaLobbyPanel에서 Grid 3개 + Switcher + 버튼 3개를 BindWidget으로 연결
//    각 Grid의 bSkipAutoInit = true로 설정해야 함!
//
// 작성자: Gihyeon (Claude Code 보조)
// ════════════════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaLobbyPanel.generated.h"

// 전방 선언
class UInv_InventoryGrid;
class UInv_InventoryComponent;
class UWidgetSwitcher;
class UButton;
class UTextBlock;
struct FInv_SavedItemData;

UCLASS()
class HELLUNA_API UHellunaLobbyPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	// ════════════════════════════════════════════════════════════════
	// 초기화
	// ════════════════════════════════════════════════════════════════

	/**
	 * 외부 InvComp와 3개 Grid를 바인딩
	 *
	 * @param InComp  바인딩할 InventoryComponent (StashComp 또는 LoadoutComp)
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|패널",
		meta = (DisplayName = "인벤토리 컴포넌트로 초기화"))
	void InitializeWithComponent(UInv_InventoryComponent* InComp);

	// ════════════════════════════════════════════════════════════════
	// 데이터 수집
	// ════════════════════════════════════════════════════════════════

	/**
	 * 3개 Grid의 모든 아이템 상태를 수집
	 * (저장 시 사용)
	 *
	 * @return 수집된 아이템 데이터 배열
	 */
	UFUNCTION(BlueprintCallable, Category = "로비|패널",
		meta = (DisplayName = "전체 Grid 아이템 수집"))
	TArray<FInv_SavedItemData> CollectAllGridItems() const;

	// ════════════════════════════════════════════════════════════════
	// Grid 접근
	// ════════════════════════════════════════════════════════════════

	UInv_InventoryGrid* GetGrid_Equippables() const { return Grid_Equippables; }
	UInv_InventoryGrid* GetGrid_Consumables() const { return Grid_Consumables; }
	UInv_InventoryGrid* GetGrid_Craftables() const { return Grid_Craftables; }

	/** 바인딩된 InvComp 반환 */
	UInv_InventoryComponent* GetBoundComponent() const { return BoundComponent.Get(); }

	// ════════════════════════════════════════════════════════════════
	// 패널 이름 설정
	// ════════════════════════════════════════════════════════════════

	/** 패널 제목 텍스트 설정 (Stash / Loadout) */
	UFUNCTION(BlueprintCallable, Category = "로비|패널",
		meta = (DisplayName = "패널 제목 설정"))
	void SetPanelTitle(const FText& InTitle);

protected:
	// ════════════════════════════════════════════════════════════════
	// BindWidget — BP에서 연결
	// ════════════════════════════════════════════════════════════════

	/** 장비 Grid */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> Grid_Equippables;

	/** 소모품 Grid */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> Grid_Consumables;

	/** 재료 Grid */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> Grid_Craftables;

	/** 탭 전환 WidgetSwitcher */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher;

	/** 장비 탭 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Equippables;

	/** 소모품 탭 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Consumables;

	/** 재료 탭 버튼 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Craftables;

	/** 패널 제목 텍스트 (선택적 — 없어도 동작) */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_PanelTitle;

private:
	// ════════════════════════════════════════════════════════════════
	// 탭 전환 콜백
	// ════════════════════════════════════════════════════════════════

	UFUNCTION()
	void ShowEquippables();

	UFUNCTION()
	void ShowConsumables();

	UFUNCTION()
	void ShowCraftables();

	/** 활성 탭 Grid 설정 + 버튼 비활성화 */
	void SetActiveGrid(UInv_InventoryGrid* Grid, UButton* ActiveButton);

	/** 버튼 비활성화 (현재 선택된 탭) */
	void DisableButton(UButton* Button);

	// ════════════════════════════════════════════════════════════════
	// 바인딩된 컴포넌트 캐시
	// ════════════════════════════════════════════════════════════════
	TWeakObjectPtr<UInv_InventoryComponent> BoundComponent;

	// 현재 활성 Grid
	TWeakObjectPtr<UInv_InventoryGrid> ActiveGrid;
};
