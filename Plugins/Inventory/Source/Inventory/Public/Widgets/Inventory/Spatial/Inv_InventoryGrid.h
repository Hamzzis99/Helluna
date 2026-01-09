#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Inv_GridTypes.h"

#include "Inv_InventoryGrid.generated.h"

class UInv_ItemPopUp;
class UInv_HoverItem;
struct FInv_ImageFragment;
struct FInv_GridFragment;
class UInv_SlottedItem;
class UInv_ItemComponent;
struct FInv_ItemManifest;
class UCanvasPanel;
class UInv_GridSlot;
class UInv_InventoryComponent;
struct FGameplayTag;
enum class EInv_GridSlotState : uint8;

/**
 *
 */
UCLASS()
class INVENTORY_API UInv_InventoryGrid : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override; // Viewport를 동시에 생성하는 것이 NativeConstruct?
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override; // 매 프레임마다 호출되는 틱 함수 (마우스 Hover에 사용)

	EInv_ItemCategory GetItemCategory() const { return ItemCategory; }

	void ShowCursor();
	void HideCursor();
	void SetOwningCanvas(UCanvasPanel* OwningCanvas); // 장비 튤팁 캔버스 설정 부분
	void DropItem(); // 아이템 버리기 함수
	bool HasHoverItem() const; // 호버 아이템이 있는지 확인하는 함수
	UInv_HoverItem* GetHoverItem() const; // 호버 아이템 가져오기 함수
	float GetTileSize() const{return TileSize;}; // 타일 크기 가져오기 함수
	void ClearHoverItem(); // 호버 아이템 지우기
	void AssignHoverItem(UInv_InventoryItem* InventoryItem); // 장착 아이템 기반 호버 아이템 할당
	void OnHide(); // 인벤토리 숨기기 처리 함수
	
	UFUNCTION()
	void AddItem(UInv_InventoryItem* Item); // 아이템 추가

	UFUNCTION()
	void RemoveItem(UInv_InventoryItem* Item); // 아이템 제거 (UI에서 삭제)

	UFUNCTION()
	void UpdateMaterialStacksByTag(const FGameplayTag& MaterialTag); // GameplayTag로 모든 스택 업데이트 (Building용)

	// GridSlot을 직접 순회하며 재료 차감 (Split된 스택 처리)
	void ConsumeItemsByTag(const FGameplayTag& MaterialTag, int32 AmountToConsume);

	// ⭐ UI GridSlots 기반 재료 개수 세기 (Split 대응!)
	int32 GetTotalMaterialCountFromSlots(const FGameplayTag& MaterialTag) const;
	
	// ⭐ Grid 크기 정보 가져오기 (공간 체크용)
	FORCEINLINE int32 GetMaxSlots() const { return Rows * Columns; }
	FORCEINLINE int32 GetRows() const { return Rows; }
	FORCEINLINE int32 GetColumns() const { return Columns; }
	
	// ⭐ 공간 체크 함수 (public - InventoryComponent에서 사용)
	FInv_SlotAvailabilityResult HasRoomForItem(const UInv_ItemComponent* ItemComponent);
	FInv_SlotAvailabilityResult HasRoomForItem(const UInv_InventoryItem* Item, const int32 StackAmountOverride = -1);
	FInv_SlotAvailabilityResult HasRoomForItem(const FInv_ItemManifest& Manifest, const int32 StackAmountOverride = -1);

	// ⭐ 실제 UI Grid 상태 확인 (크래프팅 공간 체크용)
	bool HasRoomInActualGrid(const FInv_ItemManifest& Manifest) const;
	
private:

	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<UCanvasPanel> OwningCanvasPanel;
	
	void ConstructGrid();
	void AddItemToIndices(const FInv_SlotAvailabilityResult& Result, UInv_InventoryItem* NewItem); // 아이템을 인덱스에 추가
	bool MatchesCategory(const UInv_InventoryItem* Item) const; // 카테고리 일치 여부 확인
	FVector2D GetDrawSize(const FInv_GridFragment* GridFragment) const; // 그리드 조각의 그리기 크기 가져오기
	void SetSlottedItemImage(const UInv_SlottedItem* SlottedItem, const FInv_GridFragment* GridFragment, const FInv_ImageFragment* ImageFragment) const; // 슬로티드 아이템 이미지 설정
	void AddItemAtIndex(UInv_InventoryItem* Item, const int32 Index, const bool bStackable, const int32 StackAmount); // 인덱스에 아이템 추가
	UInv_SlottedItem* CreateSlottedItem(UInv_InventoryItem* Item,
		const bool bStackable,
		const int32 StackAmount,
		const FInv_GridFragment* GridFragment,
		const FInv_ImageFragment* ImageFragment,
		const int32 Index); // 슬로티드 아이템 생성
	void AddSlottedItemToCanvas(const int32 Index, const FInv_GridFragment* GridFragment, UInv_SlottedItem* SlottedItem) const;
	void UpdateGridSlots(UInv_InventoryItem* NewItem, const int32 Index, bool bStackableItem, const int32 StackAmount); // 그리드 슬롯 업데이트
	bool IsIndexClaimed(const TSet<int32>& CheckedIndices, const int32 Index) const; // 인덱스가 이미 점유되었는지 확인
	bool HasRoomAtIndex(const UInv_GridSlot* GridSlot,
		const FIntPoint& Dimensions,
		const TSet<int32>& CheckedIndices,
		TSet<int32>& OutTentativelyClaimed,
		const FGameplayTag& ItemType,
		const int32 MaxStackSize);

	bool CheckSlotConstraints(const UInv_GridSlot* GridSlot,
		const UInv_GridSlot* SubGridSlot,
		const TSet<int32>& CheckedIndices,
		TSet<int32>& OutTentativelyClaimed,
		const FGameplayTag& ItemType,
		const int32 MaxStackSize) const;
	FIntPoint GetItemDimensions(const FInv_ItemManifest& Manifest) const; // 아이템 치수 가져오기
	bool HasValidItem(const UInv_GridSlot* GridSlot) const; // 그리드 슬롯에 유효한 아이템이 있는지 확인
	bool IsUpperLeftSlot(const UInv_GridSlot* GridSlot, const UInv_GridSlot* SubGridSlot) const; // 그리드 슬롯이 왼쪽 위 슬롯인지 확인
	bool DoesItemTypeMatch(const UInv_InventoryItem* SubItem, const FGameplayTag& ItemType) const; // 아이템 유형이 일치하는지 확인
	bool IsInGridBounds(const int32 StartIndex, const FIntPoint& ItemDimensions) const; // 그리드 경계 내에 있는지 확인
	int32 DetermineFillAmountForSlot(const bool bStackable, const int32 MaxStackSize, const int32 AmountToFill, const UInv_GridSlot* GridSlot) const;
	int32 GetStackAmount(const UInv_GridSlot* GridSlot) const;
	
	/* 아이템 마우스 클릭 판단*/
	bool IsRightClick(const FPointerEvent& MouseEvent) const;
	bool IsLeftClick(const FPointerEvent& MouseEvent) const;
	void PickUp(UInv_InventoryItem* ClickedInventoryItem, const int32 GridIndex); // 이 픽업은 마우스로 아이템을 잡을 때
	void AssignHoverItem(UInv_InventoryItem* InventoryItem, const int32 GridIndex, const int32 PreviousGridIndex); // 인덱스 기반 호버 아이템 할당
	void RemoveItemFromGrid(UInv_InventoryItem* InventoryItem, const int32 GridIndex); // 그리드에서 아이템 제거
	void UpdateTileParameters(const FVector2D& CanvasPosition, const FVector2D& MousePosition); // 타일 매개변수 업데이트
	FIntPoint CalculateHoveredCoordinates(const FVector2D& CanvasPosition, const FVector2D& MousePosition) const; // 호버된 좌표 계산
	EInv_TileQuadrant CalculateTileQuadrant(const FVector2D& CanvasPosition, const FVector2D& MousePosition) const; // 타일 사분면 계산
	void OnTileParametersUpdated(const FInv_TileParameters& Parameters); // 타일 매개변수 업데이트시 호출되는 함수
	FIntPoint CalculateStartingCoordinate(const FIntPoint& Coordinate, const FIntPoint& Dimensions, const EInv_TileQuadrant Quadrant) const; // 문턱을 얼마나 넘을 수 있는지.
	FInv_SpaceQueryResult CheckHoverPosition(const FIntPoint& Position, const FIntPoint& Dimensions); // 호버 위치 확인
	bool CursorExitedCanvas(const FVector2D& BoundaryPos, const FVector2D& BoundarySize, const FVector2D& Location); // 커서가 캔버스를 벗어났는지 확인
	void HighlightSlots(const int32 Index, const FIntPoint& Dimensions); // 슬롯 보이기
	void UnHighlightSlots(const int32 Index, const FIntPoint& Dimensions); // 슬롯 숨기기
	void ChangeHoverType(const int32 Index, const FIntPoint& Dimensions, EInv_GridSlotState GridSlotState);
	void PutDownOnIndex(const int32 Index); // 인덱스에 내려놓기
	UUserWidget* GetHiddenCursorWidget(); // 마우스 커서 비활성화 하는 함수
	bool IsSameStackable(const UInv_InventoryItem* ClickedInventoryItem) const; // 같은 아이템이라 스택 가능한지 확인하는 함수
	void SwapWithHoverItem(UInv_InventoryItem* ClickedInventoryItem, const int32 GridIndex); // 호버 아이템과 교체하는 함수
	bool ShouldSwapStackCounts(const int32 RoomInClickedSlot, const int32 HoveredStackCount, const int32 MaxStackSize) const; // 스택 수를 교체해야 하는지 확인하는 함수
	void SwapStackCounts(const int32 ClickedStackCount, const int32 HoveredStackCount, const int32 Index);
	bool ShouldConsumeHoverItemStacks(const int32 HoveredStackCount, const int32 RoomInClickedSlot) const; // 호버 아이템 스택을 소모해야 하는지 확인하는 함수
	void ConsumeHoverItemStacks(const int32 ClickedStackCount, const int32 HoveredStackCount, const int32 Index); // 호버 아이템 스택 소모 함수
	bool ShouldFillInStack(const int32 RoomInClickedSlot, const int32 HoveredStackCount) const; // 클릭된 아이템의 스택을 채워야 하는지 확인하는 함수
	void FillInStack(const int32 FillAmount, const int32 Remainder, const int32 Index); // 스택 채우기 함수
	void CreateItemPopUp(const int32 GridIndex); // 아이템 팝업 생성 함수
	void PutHoverItemBack(); // 호버 아이템 다시 놓기 함수
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UInv_ItemPopUp> ItemPopUpClass; // 아이템 팝업 클래스
	
	UPROPERTY() // 팝업 아이템 가비지 콜렉션 부분
	TObjectPtr<UInv_ItemPopUp> ItemPopUp;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UUserWidget> VisibleCursorWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UUserWidget> HiddenCursorWidgetClass;
	
	UPROPERTY()
	TObjectPtr<UUserWidget> VisibleCursorWidget; // 마우스 커서 위젯
	
	UPROPERTY()
	TObjectPtr<UUserWidget> HiddenCursorWidget; // 마우스 커서 숨겨진 것
	
	UFUNCTION()
	void AddStacks(const FInv_SlotAvailabilityResult& Result);

	UFUNCTION()
	void OnSlottedItemClicked(int32 GridIndex, const FPointerEvent& MouseEvent); // 슬롯 아이템 클릭시 호출되는 함수
	UFUNCTION()
	void OnGridSlotClicked(int32 GridIndex, const FPointerEvent& MouseEvent);
	UFUNCTION()
	void OnGridSlotHovered(int32 GridIndex, const FPointerEvent& MouseEvent);
	
	UFUNCTION()
	void OnGridSlotUnhovered(int32 GridIndex, const FPointerEvent& MouseEvent); 

	// 나누기 버튼 상호작용
	UFUNCTION()
	void OnPopUpMenuSplit(int32 SplitAmount, int32 Index);
	
	// 버리기 버튼 상호작용
	UFUNCTION()
	void OnPopUpMenuDrop(int32 Index);
	
	// 사용하기 버튼 상호작용
	UFUNCTION()
	void OnPopUpMenuConsume(int32 Index);
	
	UFUNCTION()
	void OnInventoryMenuToggled(bool bOpen); // 인벤토리 메뉴 토글 (내가 뭔가 들 때 bool 값 반환하는 함수)
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category = "Inventory")
	EInv_ItemCategory ItemCategory;
	UUserWidget* GetVisibleCursorWidget(); // 마우스 커서 보이게 하는 함수

	//2차원 격자를 만드는 것 Tarray로
	UPROPERTY()
	TArray<TObjectPtr<UInv_GridSlot>> GridSlots;

	UPROPERTY(EditAnywhere, Category = "Inventory")	
	TSubclassOf<UInv_GridSlot> GridSlotClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UInv_SlottedItem> SlottedItemClass; 

	UPROPERTY()
	TMap<int32, TObjectPtr<UInv_SlottedItem>> SlottedItems; // 인덱스와 슬로티드 아이템 매핑 아이템을 등록할 때마다 이 것을 사용할 것.

	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FVector2D ItemPopUpOffset; // 마우스 우클릭 팝업 위치 조정하기 (누르자마자 뜨는 부분)
	
	// 왜 굳이 int32로?
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 Rows;
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 Columns;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float TileSize;

	//포인터를 생성하기 위한 보조 클래스
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UInv_HoverItem> HoverItemClass;

	UPROPERTY()
	TObjectPtr<UInv_HoverItem> HoverItem;

	//아이템이 프레임마다 매개변수를 어떻게 받을지 계산.
	FInv_TileParameters TileParameters;
	FInv_TileParameters LastTileParameters;

	// Index where an item would be placed if we click on the grid at a valid location
	// 아이템이 유효한 위치에 그리드를 클릭하면 배치될 인덱스
	int32 ItemDropIndex{ INDEX_NONE };
	FInv_SpaceQueryResult CurrentQueryResult; // 현재 쿼리 결과
	bool bMouseWithinCanvas;
	bool bLastMouseWithinCanvas;
	int32 LastHighlightedIndex;
	FIntPoint LastHighlightedDimensions;
	
};

