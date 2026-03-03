// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Widgets/Inventory/Spatial/Inv_InventoryGrid.h" // FOnLobbyTransferRequested 델리게이트 사용
#include "Inv_SpatialInventory.generated.h"

struct FGameplayTag;
class UInv_ItemDescription;
class UWidgetSwitcher;
class UButton;
class UCanvasPanel;
class UInv_HoverItem;
class UInv_EquippedGridSlot;
class UInv_InventoryComponent;

// UI 연동 부분들

/**
 * 
 */
UCLASS()
class INVENTORY_API UInv_SpatialInventory : public UInv_InventoryBase
{
	GENERATED_BODY()
	
public:
	virtual void NativeOnInitialized() override; // NativeOnInitialized 이게 뭐지?
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override; // 마우스 버튼 다운 이벤트 처리 인벤토리 아이템 드롭
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override; // 마우스 반응하는지 틱 계산하는 부분
	
	//실제 공간이 있는지 확인하는 곳.
	virtual FInv_SlotAvailabilityResult HasRoomForItem(UInv_ItemComponent* ItemComponent) const override;
	//아래 호버 부분들은 전부 아이템 설명 (마우스를 위에 올려놨을 때의 가상 함수들)
	virtual void OnItemHovered(UInv_InventoryItem* Item) override;
	virtual void OnItemUnHovered() override;
	virtual bool HasHoverItem() const override;
	virtual UInv_HoverItem* GetHoverItem() const override;
	virtual float GetTileSize() const override;

	// Grid Getter 함수들 (Building 재료 차감용)
	UInv_InventoryGrid* GetGrid_Equippables() const { return Grid_Equippables; }
	UInv_InventoryGrid* GetGrid_Consumables() const { return Grid_Consumables; }
	UInv_InventoryGrid* GetGrid_Craftables() const { return Grid_Craftables; }

	// ⭐ UI 기반 재료 개수 세기 (Split된 스택도 정확히 계산!)
	int32 GetTotalMaterialCountFromUI(const FGameplayTag& MaterialTag) const;

	// ⭐ [Phase 6] 장착 슬롯 배열 Getter (저장 시 장착된 아이템 수집용)
	const TArray<TObjectPtr<UInv_EquippedGridSlot>>& GetEquippedGridSlots() const { return EquippedGridSlots; }

	// 🆕 [Phase 6] 장착 아이템 복원 (델리게이트 바인딩 포함)
	UInv_EquippedSlottedItem* RestoreEquippedItem(UInv_EquippedGridSlot* EquippedGridSlot, UInv_InventoryItem* ItemToEquip);

	// 🆕 [Phase 7] EquippedGridSlots 수집 (복원 시 재호출 가능)
	void CollectEquippedGridSlots();

	// 🆕 [Phase 8] 인벤토리 열릴 때 장착 슬롯 레이아웃 갱신
	void RefreshEquippedSlotLayouts();

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 4 Lobby] 외부 InvComp 수동 바인딩
	// ════════════════════════════════════════════════════════════════
	//
	// 로비에서 LoadoutComp를 수동 지정할 때 사용.
	// 호출 시 내부 3개 Grid에도 SetInventoryComponent를 전파한다.
	// 인게임에서는 호출하지 않으므로 기존 동작에 영향 없음.
	//
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
	// ════════════════════════════════════════════════════════════════

	UFUNCTION(BlueprintCallable, Category = "인벤토리|로비",
		meta = (DisplayName = "인벤토리 컴포넌트 수동 설정 (SpatialInventory)"))
	void SetInventoryComponent(UInv_InventoryComponent* InComp);

	// ════════════════════════════════════════════════════════════════
	// [Phase 4 Fix] 로비 전송 모드 — 3개 Grid에 일괄 활성화
	// ════════════════════════════════════════════════════════════════

	/** 3개 Grid에 로비 전송 모드 활성화 + 통합 델리게이트 바인딩 */
	void EnableLobbyTransferMode();

	/** 통합 전송 요청 델리게이트 — 어느 Grid에서든 우클릭 시 EntryIndex 전달 */
	UPROPERTY(BlueprintAssignable, Category = "인벤토리|로비")
	FOnLobbyTransferRequested OnSpatialTransferRequested;

	// ════════════════════════════════════════════════════════════════
	// [Phase 9] 컨테이너 Grid 연결 — SpatialInventory ↔ ContainerGrid 크로스 링크
	// ════════════════════════════════════════════════════════════════

	/** 컨테이너 Grid 연결 — 3개 Grid 모두에 LinkedContainerGrid 설정 + 활성 Grid 역방향 연결 */
	void LinkContainerGrid(UInv_InventoryGrid* ContainerGrid);

	/** 컨테이너 Grid 연결 해제 */
	void UnlinkContainerGrid();

	virtual void NativeDestruct() override; // [Phase 11] Quick Equip 델리게이트 해제

private:
	// [Phase 4 Fix] Grid → SpatialInventory 통합 전달 콜백
	UFUNCTION()
	void OnGridTransferRequested(int32 EntryIndex, int32 TargetGridIndex);

	// ════════════════════════════════════════════════════════════════
	// [Phase 11] Alt+LMB 빠른 장착 핸들러
	// ════════════════════════════════════════════════════════════════
	UFUNCTION()
	void OnGridQuickEquipRequested(UInv_InventoryItem* Item, int32 EntryIndex);

	// ──────────────────────────────────────────────────────────────── 
	// 여기 있는 UPROPERTY와 위젯과의 이름이 동일해야만함.
	
	//장착 슬롯 늘리는 부분
	UPROPERTY()
	TArray<TObjectPtr<UInv_EquippedGridSlot>> EquippedGridSlots;
	
	//어떤 캔버스 패널일까? 아 아이템 튤팁 공간
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;
	
	//스위치
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> Switcher;
	
	//메뉴 등록
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> Grid_Equippables;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> Grid_Consumables;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInv_InventoryGrid> Grid_Craftables;


	//버튼 등록
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Equippables;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Consumables;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> Button_Craftables;
	
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "아이템 설명 클래스", Tooltip = "인벤토리 아이템 호버 시 표시되는 설명 위젯의 블루프린트 클래스입니다."))
	TSubclassOf<UInv_ItemDescription> ItemDescriptionClass;

	UPROPERTY()
	TObjectPtr<UInv_ItemDescription> ItemDescription;

	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "장착 아이템 설명 클래스", Tooltip = "장착 슬롯의 아이템 호버 시 표시되는 설명 위젯의 블루프린트 클래스입니다. 비교 표시에 사용됩니다."))
	TSubclassOf<UInv_ItemDescription> EquippedItemDescriptionClass;

	UPROPERTY()
	TObjectPtr<UInv_ItemDescription> EquippedItemDescription;
	
	FTimerHandle DescriptionTimer;
	FTimerHandle EquippedDescriptionTimer;

	UFUNCTION()
	void ShowEquippedItemDescription(UInv_InventoryItem* Item);
	
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "설명 표시 지연 시간", Tooltip = "아이템 위에 마우스를 올린 후 설명 위젯이 표시되기까지의 지연 시간(초)입니다."))
	float DescriptionTimerDelay = 0.5f; // 설명 타이머 지연 시간 설정

	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "장착 설명 표시 지연 시간", Tooltip = "장착 슬롯의 아이템 위에 마우스를 올린 후 설명 위젯이 표시되기까지의 지연 시간(초)입니다."))
	float EquippedDescriptionTimerDelay = 0.5f;
	
	UInv_ItemDescription* GetItemDescription();
	UInv_ItemDescription* GetEquippedItemDescription();
	
	UFUNCTION()
	void ShowEquippables();

	UFUNCTION()
	void ShowConsumables();

	UFUNCTION()
	void ShowCraftables();
	
	UFUNCTION()
	void EquippedGridSlotClicked(UInv_EquippedGridSlot* EquippedGridSlot, const FGameplayTag& EquipmentTypeTag); // 마우스를 갖다댈 시 백함수 
	
	UFUNCTION()
	void EquippedSlottedItemClicked(UInv_EquippedSlottedItem* EquippedSlottedItem); // 장착된 슬롯 아이템 클릭 시 호출되는 함수
	
	void DisableButton(UButton* Button);
	void SetActiveGrid(UInv_InventoryGrid* Grid, UButton* Button);
	void SetItemDescriptionSizeAndPosition(UInv_ItemDescription* Description, UCanvasPanel* Canvas) const; // 아이템 설명 크기 및 위치 설정
	void SetEquippedItemDescriptionSizeAndPosition(UInv_ItemDescription* Description, UInv_ItemDescription* EquippedDescription, UCanvasPanel* Canvas) const; // 장착된 아이템 설명 크기 및 위치 설정 (비교하는 함수)
	bool CanEquipHoverItem(UInv_EquippedGridSlot* EquippedGridSlot, const FGameplayTag& EquipmentTypeTag) const; // 호버 아이템 장착 가능 여부 확인 게임태그도 참조해야 낄 수 있게.
	UInv_EquippedGridSlot* FindSlotWithEquippedItem(UInv_InventoryItem* EquippedItem) const; // 캡처한 포인터와 동일한 인벤토리 항목에 있는지 확인하는 것.
	void ClearSlotOfItem(UInv_EquippedGridSlot* EquippedGridSlot); // 장착된 아이템을 그리드 슬롯에서 제거
	void RemoveEquippedSlottedItem(UInv_EquippedSlottedItem* EquippedSlottedItem); // 장착된 슬롯 아이템 제거
	void MakeEquippedSlottedItem(UInv_EquippedSlottedItem* EquippedSlottedItem, UInv_EquippedGridSlot* EquippedGridSlot, UInv_InventoryItem* ItemToEquip); // 장착된 슬롯 아이템 만들기
	void BroadcastSlotClickedDelegates(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex = -1) const; // 슬롯 클릭 델리게이트 방송
	
	TWeakObjectPtr<UInv_InventoryGrid> ActiveGrid; // 활성 그리드가 생기면 늘 활성해주는 포인터.

	/** [Phase 9] 연결된 컨테이너 Grid 참조 (탭 전환 시 역방향 업데이트용) */
	TWeakObjectPtr<UInv_InventoryGrid> LinkedContainerGridRef;

	// ════════════════════════════════════════════════════════════════
	// [Phase 4 Lobby] 수동 바인딩된 InvComp 캐시
	// ════════════════════════════════════════════════════════════════
	// 로비에서 LoadoutComp를 지정할 때 사용.
	// 비어있으면 기존 자동 탐색(UInv_InventoryStatics::GetInventoryComponent) 사용.
	TWeakObjectPtr<UInv_InventoryComponent> BoundInventoryComponent;

	// 캐시된 InvComp 우선 반환 (없으면 자동 탐색 폴백)
	UInv_InventoryComponent* GetBoundInventoryComponent() const;
};
