#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/Inv_GridTypes.h"
#include "Player/Inv_PlayerController.h"
#include "Containers/BitArray.h"

#include "Inv_InventoryGrid.generated.h"

// ════════════════════════════════════════════════════════════════
// [Phase 4 Fix] 로비 전송 델리게이트 — 우클릭 시 상대 패널로 아이템 전송
// ════════════════════════════════════════════════════════════════
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyTransferRequested, int32, EntryIndex);

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
class UInv_LootContainerComponent;
class UInv_AttachmentPanel;
struct FGameplayTag;
enum class EInv_GridSlotState : uint8;

// ════════════════════════════════════════════════════════════════
// [Phase 9] Grid 소유자 타입 — 플레이어 인벤토리 vs 컨테이너
// ════════════════════════════════════════════════════════════════
UENUM()
enum class EGridOwnerType : uint8
{
	Player,      // 플레이어 인벤토리 Grid
	Container,   // 컨테이너 (상자/사체) Grid
};

// ════════════════════════════════════════════════════════════════════════
// TODO [Phase C - 데이터/뷰 분리] 상용화 리팩토링
// ════════════════════════════════════════════════════════════════════════
// 현재 이 클래스가 UI(위젯)와 데이터(점유 판단)를 모두 담당하고 있음.
// 상용화 시 아래 작업 필요:
//
// 1. GridModel 클래스 신설 (UObject, 서버+클라 공유)
//    - OccupiedMask (비트마스크) → 이미 구현됨, 여기서 이관
//    - ItemTypeIndex (타입별 인덱스) → FastArray에서 이관
//    - HasRoom(), FindSpace(), PlaceItem(), RemoveItem()
//
// 2. 이 클래스(Inv_InventoryGrid)는 UI만 담당하도록 축소
//    - GridModel을 읽어서 시각적으로 표시
//    - 슬롯 하이라이트, 드래그&드롭 등 UI 전용
//
// 3. Inv_InventoryComponent의 HasRoomInInventoryList() 제거
//    - GridModel.HasRoom()으로 대체 (서버/클라 동일 로직 1벌)
//
// 도입 시기: 루팅 상자 / 상점 / NPC 인벤토리 추가할 때
// 이유: 컨테이너마다 Grid가 필요 → GridModel을 공유하면 중복 제거
// 참고: inventory_optimization_guide.md 최적화 #6 항목
// ════════════════════════════════════════════════════════════════════════

/**
 *
 */
UCLASS()
class INVENTORY_API UInv_InventoryGrid : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override; // Viewport를 동시에 생성하는 것이 NativeConstruct?
	virtual void NativeDestruct() override; // U19: 위젯 파괴 시 InvComp 델리게이트 해제
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override; // 매 프레임마다 호출되는 틱 함수 (마우스 Hover에 사용)
	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override; // R키 아이템 회전

	EInv_ItemCategory GetItemCategory() const { return ItemCategory; }

	// ════════════════════════════════════════════════════════════════
	// 📌 [Phase 4 Lobby] 외부 InvComp 수동 바인딩 (로비 듀얼 Grid용)
	// ════════════════════════════════════════════════════════════════
	//
	// 로비에서는 플레이어에 StashComp + LoadoutComp 2개가 붙어있으므로
	// 기존 자동 바인딩(GetInventoryComponent)은 첫 번째 것만 잡음.
	// → 이 함수로 원하는 InvComp를 수동 지정한 뒤 Grid 델리게이트를 바인딩.
	//
	// 사용법:
	//   Grid->SetSkipAutoInit(true);  // BP WBP 디자이너에서 체크, 또는 C++에서 호출
	//   Grid->SetInventoryComponent(StashComp);  // NativeOnInitialized 이후 호출
	//
	// 기존 인게임 Grid는 영향 없음 (bSkipAutoInit 기본값 false)
	// TODO: [DragDrop] 추후 드래그앤드롭 크로스 패널 구현 시 여기에 연결
	// ════════════════════════════════════════════════════════════════

	/**
	 * 외부에서 InvComp를 수동 지정 + 델리게이트 바인딩
	 * NativeOnInitialized에서 자동 바인딩을 건너뛰고 (bSkipAutoInit=true)
	 * 이 함수로 원하는 InventoryComponent에 연결한다.
	 *
	 * @param InComp  바인딩할 InventoryComponent
	 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리|로비",
		meta = (DisplayName = "인벤토리 컴포넌트 수동 설정"))
	void SetInventoryComponent(UInv_InventoryComponent* InComp);

	/** [Phase 9] 컨테이너 Grid용 — InvComp 참조만 저장 (델리게이트/Sync 없음, RPC 호출용) */
	void SetInventoryComponentForRPC(UInv_InventoryComponent* InComp);

	/** [Phase 9] 컨테이너의 기존 아이템을 Grid에 동기화 */
	void SyncContainerItems(UInv_LootContainerComponent* ContainerComp);

	/** NativeOnInitialized에서 자동 바인딩을 건너뛸지 여부 */
	UFUNCTION(BlueprintCallable, Category = "인벤토리|로비",
		meta = (DisplayName = "자동 초기화 스킵 설정"))
	void SetSkipAutoInit(bool bSkip) { bSkipAutoInit = bSkip; }

	/** 현재 바인딩된 InventoryComponent 반환 */
	UInv_InventoryComponent* GetInventoryComponent() const { return InventoryComponent.Get(); }

	// ════════════════════════════════════════════════════════════════
	// [Phase 4 Fix] 로비 전송 모드 — 우클릭 시 팝업 대신 전송 델리게이트 발동
	// ════════════════════════════════════════════════════════════════

	/** 로비 전송 모드 활성화/비활성화 */
	void SetLobbyTransferMode(bool bEnable) { bLobbyTransferMode = bEnable; }

	// ════════════════════════════════════════════════════════════════
	// [Fix19] 로비 전송 대상 Grid — 전송 전 용량 사전 체크용
	// ════════════════════════════════════════════════════════════════
	// Stash Grid → Loadout Grid (같은 카테고리)
	// Loadout Grid → Stash Grid (같은 카테고리)
	// InitializePanels에서 교차 설정됨
	// ════════════════════════════════════════════════════════════════

	/** 전송 대상 Grid 설정 (같은 카테고리 Grid 교차 연결) */
	void SetLobbyTargetGrid(UInv_InventoryGrid* InTargetGrid) { LobbyTargetGrid = InTargetGrid; }

	/** 로비 전송 요청 델리게이트 — 우클릭 시 EntryIndex를 전달 */
	UPROPERTY(BlueprintAssignable, Category = "인벤토리|로비")
	FOnLobbyTransferRequested OnLobbyTransferRequested;

	// ════════════════════════════════════════════════════════════════
	// [Phase 9] 컨테이너 Grid 크로스 드래그 & 드롭
	// ════════════════════════════════════════════════════════════════

	/** Grid 소유자 타입 설정 (Player/Container) */
	void SetOwnerType(EGridOwnerType InType) { OwnerType = InType; }
	EGridOwnerType GetOwnerType() const { return OwnerType; }

	/** 크로스 Grid 드래그용: 반대편 Grid 참조 */
	void SetLinkedContainerGrid(UInv_InventoryGrid* OtherGrid);

	/** 크로스 Grid 드래그: 연결된 Grid에 HoverItem이 있는지 */
	bool HasLinkedHoverItem() const;
	UInv_HoverItem* GetLinkedHoverItem() const;

	/** 컨테이너 컴포넌트 참조 설정 (컨테이너 Grid용) */
	void SetContainerComponent(UInv_LootContainerComponent* InContainerComp);
	UInv_LootContainerComponent* GetContainerComponent() const;

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
	void AddItem(UInv_InventoryItem* Item, int32 EntryIndex); // 아이템 추가 (EntryIndex 포함)

	UFUNCTION()
	void RemoveItem(UInv_InventoryItem* Item, int32 EntryIndex); // 아이템 제거 (EntryIndex로 정확히 매칭)
	
	// 🆕 [Phase 6] 포인터만으로 아이템 제거 (장착 복원 시 Grid에서 제거용)
	bool RemoveSlottedItemByPointer(UInv_InventoryItem* Item);

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

	// ⭐ Grid 상태 수집 (저장용) - Split된 스택도 개별 수집
	// @param ItemsToSkip 수집에서 제외할 아이템 포인터 Set (장착 아이템 중복 수집 방지용, nullptr이면 필터 없음)
	TArray<FInv_SavedItemData> CollectGridState(const TSet<UInv_InventoryItem*>* ItemsToSkip = nullptr) const;

	// 🔍 [진단] SlottedItems 개수 조회 (디버그용)
	FORCEINLINE int32 GetSlottedItemCount() const { return SlottedItems.Num(); }

	// ============================================
	// 📦 [Phase 5] Grid 위치 복원 함수
	// ============================================

	/**
	 * 저장된 Grid 위치로 아이템 재배치
	 *
	 * @param SavedItems - 복원할 아이템 데이터 배열
	 * @return 복원 성공한 아이템 수
	 */
	int32 RestoreItemPositions(const TArray<FInv_SavedItemData>& SavedItems);

	/**
	 * 특정 아이템을 지정된 위치로 이동
	 *
	 * @param ItemType - 이동할 아이템의 GameplayTag
	 * @param TargetPosition - 목표 Grid 위치
	 * @param StackCount - 해당 스택의 수량
	 * @return 이동 성공 여부
	 */
	bool MoveItemToPosition(const FGameplayTag& ItemType, const FIntPoint& TargetPosition, int32 StackCount);
	
	// [Phase 5] 현재 GridIndex 기반으로 아이템을 목표 위치로 이동 (순서 기반 복원용)
	// ⭐ Phase 5: SavedStackCount 파라미터 추가 - 로드 시 저장된 StackCount를 전달받음
	bool MoveItemByCurrentIndex(int32 CurrentIndex, const FIntPoint& TargetPosition, int32 SavedStackCount = -1);

	// ============================================
	// ⭐ [Phase 4 방법2 Fix] 인벤토리 로드 시 RPC 스킵 플래그
	// ============================================
	
	/**
	 * 로드 중 Server_UpdateItemGridPosition RPC 전송 억제
	 * true일 때 UpdateGridSlots에서 RPC를 보내지 않음
	 */
	void SetSuppressServerSync(bool bSuppress) { bSuppressServerSync = bSuppress; }
	bool IsSuppressServerSync() const { return bSuppressServerSync; }
	
	/**
	 * 현재 Grid의 모든 아이템 위치를 서버에 전송
	 * 복원 완료 후 호출하여 올바른 위치로 동기화
	 */
	void SendAllItemPositionsToServer();

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 3] 부착물 패널 관련
	// ════════════════════════════════════════════════════════════════

	// 부착물 패널 열기/닫기
	void OpenAttachmentPanel(UInv_InventoryItem* WeaponItem, int32 WeaponEntryIndex);
	void CloseAttachmentPanel();
	bool IsAttachmentPanelOpen() const;

private:
	// ⭐ 로드 중 RPC 억제 플래그
	bool bSuppressServerSync = false;

	// [Phase 4 Fix] 로비 전송 모드 플래그 — true이면 우클릭 시 팝업 대신 전송 델리게이트
	bool bLobbyTransferMode = false;

	// [Fix19] 로비 전송 대상 Grid (같은 카테고리, 교차 연결)
	TWeakObjectPtr<UInv_InventoryGrid> LobbyTargetGrid;

	// [Fix20] 상대 Grid의 HoverItem을 이쪽으로 전송 (패널 간 드래그 앤 드롭)
	bool TryTransferFromTargetGrid();

	// ════════════════════════════════════════════════════════════════
	// [Phase 9] 컨테이너 Grid 관련 private 멤버
	// ════════════════════════════════════════════════════════════════

	// Grid 소유자 타입 (기본: Player)
	EGridOwnerType OwnerType = EGridOwnerType::Player;

	// 크로스 Grid 드래그용: 반대편 Grid 참조 (LobbyTargetGrid와 별도)
	TWeakObjectPtr<UInv_InventoryGrid> LinkedContainerGrid;

	// 컨테이너 컴포넌트 참조 (Container Grid에서만 유효)
	TWeakObjectPtr<UInv_LootContainerComponent> ContainerComp;

	// 크로스 Grid에서 아이템 전송 시도 (OnGridSlotClicked에서 호출)
	bool TryTransferFromLinkedContainerGrid(int32 GridIndex);

	// [Phase 4 Fix] 기존 아이템 동기화 — SetInventoryComponent 후 이미 InvComp에 있는 아이템을 Grid에 표시
	void SyncExistingItems();

	// ⭐ [Phase 4 Lobby] true이면 NativeOnInitialized에서 자동 바인딩 스킵
	// 로비 듀얼 Grid에서 SetInventoryComponent()로 수동 바인딩할 때 사용
	UPROPERTY(EditAnywhere, Category = "인벤토리|로비",
		meta = (DisplayName = "자동 초기화 스킵", Tooltip = "true이면 NativeOnInitialized에서 InventoryComponent 자동 바인딩을 건너뜁니다. 로비 Grid에서 수동 바인딩할 때 사용합니다."))
	bool bSkipAutoInit = false;

	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<UCanvasPanel> OwningCanvasPanel;
	
	void ConstructGrid();
	void AddItemToIndices(const FInv_SlotAvailabilityResult& Result, UInv_InventoryItem* NewItem, bool bRotated = false); // 아이템을 인덱스에 추가
	bool MatchesCategory(const UInv_InventoryItem* Item) const; // 카테고리 일치 여부 확인
	FVector2D GetDrawSize(const FInv_GridFragment* GridFragment) const; // 그리드 조각의 그리기 크기 가져오기
	void SetSlottedItemImage(const UInv_SlottedItem* SlottedItem, const FInv_GridFragment* GridFragment, const FInv_ImageFragment* ImageFragment, bool bRotated = false) const; // 슬로티드 아이템 이미지 설정
	void AddItemAtIndex(UInv_InventoryItem* Item, const int32 Index, const bool bStackable, const int32 StackAmount, const int32 EntryIndex, bool bRotated = false); // 인덱스에 아이템 추가
	UInv_SlottedItem* CreateSlottedItem(UInv_InventoryItem* Item,
		const bool bStackable,
		const int32 StackAmount,
		const FInv_GridFragment* GridFragment,
		const FInv_ImageFragment* ImageFragment,
		const int32 Index,
		const int32 EntryIndex,
		bool bRotated = false); // ⭐ EntryIndex 추가 + 회전 상태
	void AddSlottedItemToCanvas(const int32 Index, const FInv_GridFragment* GridFragment, UInv_SlottedItem* SlottedItem, bool bRotated = false) const;
	void UpdateGridSlots(UInv_InventoryItem* NewItem, const int32 Index, bool bStackableItem, const int32 StackAmount, bool bRotated = false); // 그리드 슬롯 업데이트
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
	
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "아이템 팝업 클래스", Tooltip = "아이템 우클릭 시 표시되는 팝업 메뉴 위젯의 블루프린트 클래스입니다."))
	TSubclassOf<UInv_ItemPopUp> ItemPopUpClass; // 아이템 팝업 클래스
	
	UPROPERTY() // 팝업 아이템 가비지 콜렉션 부분
	TObjectPtr<UInv_ItemPopUp> ItemPopUp;
	
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "보이는 커서 위젯 클래스", Tooltip = "아이템을 들고 있지 않을 때 표시되는 마우스 커서 위젯 클래스입니다."))
	TSubclassOf<UUserWidget> VisibleCursorWidgetClass;

	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "숨겨진 커서 위젯 클래스", Tooltip = "아이템을 들고 있을 때 사용되는 숨겨진 마우스 커서 위젯 클래스입니다."))
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

	// 부착물 관리 버튼 상호작용
	UFUNCTION()
	void OnPopUpMenuAttachment(int32 Index);

	// 로비 전송 버튼 상호작용 (PopupMenu에서 Transfer 클릭)
	UFUNCTION()
	void OnPopUpMenuTransfer(int32 Index);

	UFUNCTION()
	void OnInventoryMenuToggled(bool bOpen); // 인벤토리 메뉴 토글 (내가 뭔가 들 때 bool 값 반환하는 함수)
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true", DisplayName = "아이템 카테고리", Tooltip = "이 그리드가 담당하는 아이템 카테고리입니다. (장비, 소모품, 제작 재료 등)"), Category = "인벤토리")
	EInv_ItemCategory ItemCategory;
	UUserWidget* GetVisibleCursorWidget(); // 마우스 커서 보이게 하는 함수

	//2차원 격자를 만드는 것 Tarray로
	UPROPERTY()
	TArray<TObjectPtr<UInv_GridSlot>> GridSlots;

	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "그리드 슬롯 클래스", Tooltip = "그리드를 구성하는 개별 슬롯 위젯의 블루프린트 클래스입니다."))
	TSubclassOf<UInv_GridSlot> GridSlotClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;

	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "슬롯 아이템 클래스", Tooltip = "그리드에 배치된 아이템을 표시하는 위젯의 블루프린트 클래스입니다."))
	TSubclassOf<UInv_SlottedItem> SlottedItemClass;

	UPROPERTY()
	TMap<int32, TObjectPtr<UInv_SlottedItem>> SlottedItems; // 인덱스와 슬로티드 아이템 매핑 아이템을 등록할 때마다 이 것을 사용할 것.

	
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "아이템 팝업 오프셋", Tooltip = "아이템 우클릭 팝업의 표시 위치 오프셋(X, Y)입니다."))
	FVector2D ItemPopUpOffset; // 마우스 우클릭 팝업 위치 조정하기 (누르자마자 뜨는 부분)
	
	// 왜 굳이 int32로?
	// U18: 안전한 기본값 설정 (TileSize=0이면 나눗셈 NaN/Inf 발생)
	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "행 수", Tooltip = "그리드의 행(세로) 개수입니다."))
	int32 Rows = 5;
	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "열 수", Tooltip = "그리드의 열(가로) 개수입니다."))
	int32 Columns = 10;

	UPROPERTY(EditAnywhere, Category = "인벤토리|그리드", meta = (DisplayName = "타일 크기", Tooltip = "그리드 슬롯 한 칸의 크기(픽셀)입니다."))
	float TileSize = 50.f;

	//포인터를 생성하기 위한 보조 클래스
	UPROPERTY(EditAnywhere, Category = "인벤토리", meta = (DisplayName = "호버 아이템 클래스", Tooltip = "마우스로 아이템을 집었을 때 표시되는 호버 위젯의 블루프린트 클래스입니다."))
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
	// [최적화] HoverItem을 들고 있을 때만 true → NativeTick에서 계산 수행
	bool bShouldTickForHover = false;

	// ⭐ [최적화 #6] SlottedItem 위젯 풀 (CreateWidget 호출 최소화)
	UPROPERTY()
	TArray<TObjectPtr<UInv_SlottedItem>> SlottedItemPool;

	// ⭐ [최적화 #6] 풀에서 SlottedItem 획득 (없으면 새로 생성)
	UInv_SlottedItem* AcquireSlottedItem();

	// ⭐ [최적화 #6] SlottedItem을 풀에 반환 (RemoveFromParent 후 보관)
	void ReleaseSlottedItem(UInv_SlottedItem* SlottedItem);

	// ⭐ [최적화 #5] 비트마스크 점유 맵 (O(n) GridSlot 순회 → O(1) 비트 검사)
	// Index = Row * Columns + Col, true = 점유됨
	TBitArray<> OccupiedMask;

	// ⭐ [최적화 #5] 비트마스크 점유 상태 일괄 설정
	void SetOccupiedBits(int32 StartIndex, const FIntPoint& Dimensions, bool bOccupied);

	// ⭐ [최적화 #5] 영역이 비어있는지 비트마스크로 빠르게 확인
	bool IsAreaFree(int32 StartIndex, const FIntPoint& Dimensions) const;
	// [Fix21] HoverItem 브러시의 현재 TileSize 추적 (크로스 Grid 리사이즈용)
	float HoverItemCurrentTileSize = 0.f;

	// [Fix21] HoverItem 브러시를 TargetTileSize에 맞게 리사이즈
	void RefreshHoverItemBrushSize(float TargetTileSize);

	// R키 회전: 회전 적용된 실효 크기 (Dimensions XY 교환)
	static FIntPoint GetEffectiveDimensions(const FInv_GridFragment* GridFragment, bool bRotated);

	// R키 회전: 회전 상태에 따른 DrawSize 계산
	FVector2D GetDrawSizeRotated(const FInv_GridFragment* GridFragment, bool bRotated) const;

	int32 LastHighlightedIndex;
	FIntPoint LastHighlightedDimensions;

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 3] 부착물 패널 위젯
	// ════════════════════════════════════════════════════════════════

	UPROPERTY(EditAnywhere, Category = "인벤토리|부착물", meta = (DisplayName = "부착물 패널 클래스", Tooltip = "무기 부착물 관리 패널의 위젯 블루프린트 클래스입니다."))
	TSubclassOf<UInv_AttachmentPanel> AttachmentPanelClass;

	UPROPERTY()
	TObjectPtr<UInv_AttachmentPanel> AttachmentPanel;

	// 부착물 패널 닫힘 콜백
	UFUNCTION()
	void OnAttachmentPanelClosed();
};

