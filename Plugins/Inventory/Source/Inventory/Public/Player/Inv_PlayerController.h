#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Inv_PlayerController.generated.h"

class UInv_InventoryComponent;
class UInv_EquipmentComponent;
class UInputMappingContext;
class UInputAction;
class UInv_HUDWidget;

// ============================================
// 📦 인벤토리 저장용 순수 데이터 구조체
// ============================================
/**
 * 단일 아이템의 저장 데이터 (플러그인 전용, Helluna 의존성 없음)
 * 
 * ============================================
 * 📌 용도:
 * ============================================
 * - 클라이언트 UI 상태를 수집하여 서버로 전송할 때 사용
 * - Split된 스택도 개별 항목으로 저장됨
 *   예: 포션 20개를 9개+11개로 Split → 2개의 FInv_SavedItemData 생성
 * 
 * ============================================
 * 📌 데이터 흐름:
 * ============================================
 * [클라이언트]
 *   UInv_InventoryGrid::CollectGridState()
 *     → SlottedItems 순회
 *     → GridSlot에서 StackCount 읽기 (Split 반영!)
 *     → TArray<FInv_SavedItemData> 반환
 *       ↓
 *   AInv_PlayerController::CollectInventoryGridState()
 *     → 3개 Grid 수집 결과 합침
 *       ↓
 *   Server RPC로 서버에 전송 (Phase 4에서 구현)
 *       ↓
 * [서버]
 *   FInv_SavedItemData → FHellunaInventoryItemData 변환
 *     → SaveGame에 저장
 * 
 * ============================================
 * 📌 주의사항:
 * ============================================
 * - 이 구조체는 플러그인에 있으므로 Helluna 타입을 사용하면 안 됨!
 * - Helluna에서 FHellunaInventoryItemData로 변환하여 저장
 */
USTRUCT(BlueprintType)
struct INVENTORY_API FInv_SavedItemData
{
	GENERATED_BODY()

	FInv_SavedItemData()
		: ItemType(FGameplayTag::EmptyTag)
		, StackCount(0)
		, GridPosition(FIntPoint(-1, -1))
		, GridCategory(0)
	{
	}

	FInv_SavedItemData(const FGameplayTag& InItemType, int32 InStackCount, const FIntPoint& InGridPosition, uint8 InGridCategory)
		: ItemType(InItemType)
		, StackCount(InStackCount)
		, GridPosition(InGridPosition)
		, GridCategory(InGridCategory)
	{
	}

	/**
	 * 아이템 종류 (GameplayTag)
	 * 예: "GameItems.Consumables.Potions.Health"
	 * 
	 * 로드 시 이 태그로 DataTable에서 Actor 클래스를 조회함
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Save")
	FGameplayTag ItemType;

	/**
	 * 스택 수량 (Split된 개별 스택 수량)
	 * 
	 * ⭐ 중요: 서버의 TotalStackCount가 아니라 UI의 GridSlot->GetStackCount() 값!
	 * Split 시: 서버 Entry(20개) → UI 슬롯1(9개) + UI 슬롯2(11개)
	 *           → FInv_SavedItemData 2개 생성 (9, 11)
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Save")
	int32 StackCount;

	/**
	 * Grid 내 위치 (X=Column, Y=Row)
	 * 
	 * GridIndex → GridPosition 변환:
	 *   X = GridIndex % Columns
	 *   Y = GridIndex / Columns
	 * 
	 * 예: Columns=8, GridIndex=19 → X=3, Y=2 → (3, 2)
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Save")
	FIntPoint GridPosition;

	/** 
	 * Grid 카테고리 (어느 탭에 있는지)
	 * 
	 * 0 = Grid_Equippables (장비)   - EInv_ItemCategory::Equippable
	 * 1 = Grid_Consumables (소모품) - EInv_ItemCategory::Consumable
	 * 2 = Grid_Craftables (재료)    - EInv_ItemCategory::Craftable
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Save")
	uint8 GridCategory;

	/** 유효한 데이터인지 확인 */
	bool IsValid() const
	{
		return ItemType.IsValid() && StackCount > 0;
	}

	/** 카테고리 이름 반환 (디버그용) */
	FString GetCategoryName() const
	{
		switch (GridCategory)
		{
			case 0: return TEXT("장비");
			case 1: return TEXT("소모품");
			case 2: return TEXT("재료");
			default: return TEXT("???");
		}
	}

	/** 디버그 문자열 */
	FString ToString() const
	{
		return FString::Printf(TEXT("[%s x%d @ Grid%d(%s) Pos(%d,%d)]"),
			*ItemType.ToString(), 
			StackCount, 
			GridCategory,
			*GetCategoryName(),
			GridPosition.X, GridPosition.Y);
	}
};

UCLASS()
class INVENTORY_API AInv_PlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	AInv_PlayerController();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleInventory();

	// ============================================
	// 📌 인벤토리 저장/로드용 함수 (Phase 3)
	// ============================================
	
	/**
	 * 현재 클라이언트 UI의 인벤토리 Grid 상태를 수집
	 * 
	 * ============================================
	 * 📌 호출 시점:
	 * ============================================
	 * - 서버에서 Client_RequestInventoryState() RPC 수신 시
	 * - 자동저장(300초) / 로그아웃 / 맵이동 전에 호출됨
	 * 
	 * ============================================
	 * 📌 수집 과정:
	 * ============================================
	 * 1. InventoryComponent → InventoryMenu(SpatialInventory) 접근
	 * 2. 3개 Grid 순회 (Equippables, Consumables, Craftables)
	 * 3. 각 Grid의 SlottedItems 맵 순회
	 * 4. GridSlot에서 StackCount 읽기 (⭐ Split 반영!)
	 * 5. GridIndex → GridPosition 변환
	 * 
	 * ============================================
	 * 📌 Split 처리:
	 * ============================================
	 * 서버: Entry 1개 (TotalStackCount=20)
	 * UI:   슬롯1(9개) + 슬롯2(11개)
	 * 결과: FInv_SavedItemData 2개 생성!
	 * 
	 * @return 모든 Grid의 아이템 데이터 배열 (Split 스택 포함)
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Save")
	TArray<FInv_SavedItemData> CollectInventoryGridState();

	/**
	 * 저장된 상태로 인벤토리 Grid 복원
	 * 
	 * ============================================
	 * 📌 호출 시점:
	 * ============================================
	 * - 서버에서 아이템 생성 완료 후 (FastArray 리플리케이션 후)
	 * - Client_RestoreGridPositions() RPC 수신 시
	 * 
	 * ============================================
	 * 📌 복원 과정 (Phase 5에서 구현):
	 * ============================================
	 * 1. 이미 UI에 아이템이 자동 배치된 상태
	 * 2. 각 SavedItem의 GridCategory로 해당 Grid 선택
	 * 3. 아이템을 저장된 GridPosition으로 이동
	 * 4. Split 상태 복원 (같은 ItemType 여러 위치)
	 * 
	 * @param SavedItems - 복원할 아이템 데이터 배열
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Save")
	void RestoreInventoryFromState(const TArray<FInv_SavedItemData>& SavedItems);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void PrimaryInteract();
	void CreateHUDWidget();
	void TraceForInteractables();
	
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<UInv_EquipmentComponent> EquipmentComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	TArray<TObjectPtr<UInputMappingContext>> DefaultIMCs;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "상호작용 액션"))
	TObjectPtr<UInputAction> PrimaryInteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "인벤토리 토글 액션"))
	TObjectPtr<UInputAction> ToggleInventoryAction;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Weapon", meta = (DisplayName = "주무기 전환 액션"))
	TObjectPtr<UInputAction> PrimaryWeaponAction;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Weapon", meta = (DisplayName = "보조무기 전환 액션"))
	TObjectPtr<UInputAction> SecondaryWeaponAction;

	void HandlePrimaryWeapon();
	void HandleSecondaryWeapon();

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "HUD 위젯 클래스"))
	TSubclassOf<UInv_HUDWidget> HUDWidgetClass;

	UPROPERTY()
	TObjectPtr<UInv_HUDWidget> HUDWidget;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "추적 길이"))	
	double TraceLength;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory", meta = (DisplayName = "아이템 추적 채널"))	
	TEnumAsByte<ECollisionChannel> ItemTraceChannel;

	UFUNCTION(Server, Reliable)
	void Server_Interact(AActor* TargetActor);

	TWeakObjectPtr<AActor> ThisActor;
	TWeakObjectPtr<AActor> LastActor;
	TWeakObjectPtr<AActor> CurrentCraftingStation;
};
