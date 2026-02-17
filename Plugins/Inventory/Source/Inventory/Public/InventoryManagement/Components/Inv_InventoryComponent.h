// Gihyeon's Inventory Project

//인벤토리 베이스 자식 클래스

#pragma once

#include "CoreMinimal.h"
//#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Components/ActorComponent.h"
#include "InventoryManagement/FastArray/Inv_FastArray.h"
#include "Items/Fragments/Inv_ItemFragment.h"
#include "Player/Inv_PlayerController.h"  // FInv_SavedItemData 사용
#include "Inv_InventoryComponent.generated.h"

class UInv_ItemComponent;
class UInv_InventoryItem;
class UInv_InventoryBase;
class UInv_InventoryGrid;
struct FInv_ItemManifest;

//델리게이트
// ⭐ TwoParams로 변경: Item + EntryIndex (서버-클라이언트 포인터 불일치 해결용)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInventoryItemChange, UInv_InventoryItem*, Item, int32, EntryIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoRoomInInventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStackChange, const FInv_SlotAvailabilityResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FItemEquipStatusChanged, UInv_InventoryItem*, Item, int32, WeaponSlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInventoryMenuToggled, bool, bOpen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMaterialStacksChanged, const FGameplayTag&, MaterialTag); // Building 시스템용

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable ) // Blueprintable : 블루프린트에서 상속
class INVENTORY_API UInv_InventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UInv_InventoryComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	void TryAddItem(UInv_ItemComponent* ItemComponent);

	//서버 부분 RPC로 만들 것
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_AddNewItem(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder);

	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_AddStacksToItem(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder);

	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_DropItem(UInv_InventoryItem* Item, int32 StackCount); // 아이템을 서버에다 어떻게 버릴지
	
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_ConsumeItem(UInv_InventoryItem* Item);

	UFUNCTION(Server, Reliable) // 재료 소비 (Building 시스템용)
	void Server_ConsumeMaterials(const FGameplayTag& MaterialTag, int32 Amount);

	UFUNCTION(Server, Reliable) // 재료 소비 - 여러 스택에서 차감 (Building 시스템용)
	void Server_ConsumeMaterialsMultiStack(const FGameplayTag& MaterialTag, int32 Amount);

	UFUNCTION(Server, Reliable) // Split 시 서버의 TotalStackCount 업데이트
	void Server_UpdateItemStackCount(UInv_InventoryItem* Item, int32 NewStackCount);

	UFUNCTION(Server, Reliable) // ⭐ Phase 8: Split 시 서버에서 새 Entry 생성 (포인터 분리)
	void Server_SplitItemEntry(UInv_InventoryItem* OriginalItem, int32 OriginalNewStackCount, int32 SplitStackCount, int32 TargetGridIndex = INDEX_NONE);

	// ⭐ [Phase 4 방법2] 클라이언트 Grid 위치를 서버 Entry에 동기화
	// 클라이언트에서 아이템을 Grid에 배치/이동할 때 호출
	UFUNCTION(Server, Reliable)
	void Server_UpdateItemGridPosition(UInv_InventoryItem* Item, int32 GridIndex, uint8 GridCategory);

	UFUNCTION(Server, Reliable) // 크래프팅: 서버에서 아이템 생성 및 인벤토리 추가
	void Server_CraftItem(TSubclassOf<AActor> ItemActorClass);

	// ⭐ 크래프팅 통합 RPC: 공간 체크 → 재료 차감 → 아이템 생성
	UFUNCTION(Server, Reliable)
	void Server_CraftItemWithMaterials(TSubclassOf<AActor> ItemActorClass,
		const FGameplayTag& MaterialTag1, int32 Amount1,
		const FGameplayTag& MaterialTag2, int32 Amount2,
		const FGameplayTag& MaterialTag3, int32 Amount3,
		int32 CraftedAmount = 1);  // ⭐ 제작 개수 (기본값 1)

	UFUNCTION(NetMulticast, Reliable) // 모든 클라이언트의 UI 업데이트 (Building 재료 차감)
	void Multicast_ConsumeMaterialsUI(const FGameplayTag& MaterialTag, int32 Amount);

	// 같은 타입의 모든 스택 개수 합산 (Building UI용)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 GetTotalMaterialCount(const FGameplayTag& MaterialTag) const;
	
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex = -1);

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 2] 부착/분리 Server RPC
	// ════════════════════════════════════════════════════════════════

	// 부착물 장착: 인벤토리 Grid에서 부착물을 무기 슬롯에 장착
	UFUNCTION(Server, Reliable)
	void Server_AttachItemToWeapon(int32 WeaponEntryIndex, int32 AttachmentEntryIndex, int32 SlotIndex);

	// 부착물 분리: 무기 슬롯에서 부착물을 분리하여 인벤토리 Grid로 복귀
	UFUNCTION(Server, Reliable)
	void Server_DetachItemFromWeapon(int32 WeaponEntryIndex, int32 SlotIndex);

	// 호환성 체크 (UI에서 드래그 중 슬롯 하이라이트용, 읽기 전용)
	UFUNCTION(BlueprintCallable, Category = "Inventory|Attachment")
	bool CanAttachToWeapon(int32 WeaponEntryIndex, int32 AttachmentEntryIndex, int32 SlotIndex) const;
	
	UFUNCTION(NetMulticast, Reliable) // 멀티캐스트 함수 (서버에서 모든 클라이언트로 호출)
	void Multicast_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip, int32 WeaponSlotIndex = -1);
	
	
	//서버 RPC 전송하는 부분 함수들
	
	void ToggleInventoryMenu(); //인벤토리 메뉴 토글 함수
	void AddRepSubObj(UObject* SubObj); //복제 하위 객체 추가 함수
	void SpawnDroppedItem(UInv_InventoryItem* Item, int32 StackCount); // 떨어진 아이템 생성 함수
	UInv_InventoryBase* GetInventoryMenu() const {return InventoryMenu;};
	bool IsMenuOpen() const { return bInventoryMenuOpen; }

	// InventoryList 접근용 (재료 체크 등에 사용)
	const FInv_InventoryFastArray& GetInventoryList() const { return InventoryList; }
	FInv_InventoryFastArray& GetInventoryList() { return InventoryList; } // non-const 오버로드

	// ============================================
	// 🆕 [Phase 6] ItemType으로 아이템 찾기
	// ============================================
	UInv_InventoryItem* FindItemByType(const FGameplayTag& ItemType);
	
	// 🆕 [Phase 6] 제외 목록을 사용한 아이템 검색 (같은 타입 다중 장착 지원)
	UInv_InventoryItem* FindItemByTypeExcluding(const FGameplayTag& ItemType, const TSet<UInv_InventoryItem*>& ExcludeItems);

	// ⭐ [Phase 5 Fix] 마지막으로 추가된 Entry의 Grid 위치 설정 (로드 시 사용)
	void SetLastEntryGridPosition(int32 GridIndex, uint8 GridCategory);

	// ════════════════════════════════════════════════════════════════
	// 📌 [부착물 시스템 Phase 3] Entry Index 검색 헬퍼
	// ════════════════════════════════════════════════════════════════
	// 아이템 포인터로 현재 InventoryList의 Entry Index를 찾는다.
	// Entry가 추가/제거되면 인덱스가 변하므로, 캐시된 값 대신 이 함수를 사용할 것.
	int32 FindEntryIndexForItem(const UInv_InventoryItem* Item) const;

	// ⭐ [Phase 4 개선] 서버에서 직접 인벤토리 데이터 수집 (Logout 시 저장용)
	// RPC 없이 서버의 FastArray에서 직접 읽어서 반환
	TArray<FInv_SavedItemData> CollectInventoryDataForSave() const;
	
	// 서버 브로드캐스트 함수들.
	FInventoryItemChange OnItemAdded;
	FInventoryItemChange OnItemRemoved;
	FNoRoomInInventory NoRoomInInventory;
	FStackChange OnStackChange;
	FItemEquipStatusChanged OnItemEquipped;
	FItemEquipStatusChanged OnItemUnequipped;
	FInventoryMenuToggled OnInventoryMenuToggled;
	FMaterialStacksChanged OnMaterialStacksChanged; // Building 시스템용
	
	
protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// ⭐ Blueprint에서 인벤토리 Grid 크기 참조 (모든 카테고리 공통 사용)
	// WBP_SpatialInventory의 Grid_Equippables를 선택하면 Rows/Columns 자동 참조!
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "인벤토리|그리드 설정", meta = (DisplayName = "인벤토리 그리드 (크기 자동 참조)"))
	TObjectPtr<UInv_InventoryGrid> InventoryGridReference = nullptr;

private:

	TWeakObjectPtr<APlayerController> OwningController;

	void ConstructInventory();
	
	// ⭐ Blueprint Widget의 Grid 크기를 Component 설정으로 동기화
	void SyncGridSizesFromWidget();

	// ⭐ 서버 전용: InventoryList 기반 공간 체크 (UI 없이 작동!)
	bool HasRoomInInventoryList(const FInv_ItemManifest& Manifest) const;

	// ⭐ [SERVER-ONLY] 서버의 InventoryList를 기준으로 실제 재료 보유 여부를 확인합니다.
	bool HasRequiredMaterialsOnServer(const FGameplayTag& MaterialTag, int32 RequiredAmount) const;

	/**
	 * 리슨서버 호스트 또는 스탠드얼론인지 확인
	 *
	 * 📌 용도:
	 *    FastArray 리플리케이션이 자기 자신에게 안 되는 환경에서
	 *    직접 UI 갱신이 필요한지 판단
	 *
	 * @return true = 리슨서버 호스트 또는 스탠드얼론 (직접 UI 갱신 필요)
	 */
	bool IsListenServerOrStandalone() const;

	// ⭐ Grid 크기 (BeginPlay 시 Widget에서 자동 설정됨 - 모든 카테고리 공통 사용)
	int32 GridRows = 6;
	int32 GridColumns = 8;

	UPROPERTY(Replicated)
	FInv_InventoryFastArray InventoryList; // 인벤토리 

	UPROPERTY() // 이거는 소유권을 확보하는 것. 소유권을 잃지 않게 해주는 것.
	TObjectPtr<UInv_InventoryBase> InventoryMenu; //인벤토리 메뉴 위젯 인스턴스

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UInv_InventoryBase> InventoryMenuClass;

	bool bInventoryMenuOpen; //인벤토리 메뉴 열림 여부
	void OpenInventoryMenu();
	void CloseInventoryMenu();
	void CloseOtherMenus(); // BuildMenu와 CraftingMenu 닫기
	
	//아이템 드롭 시 빙글빙글 돌아요
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnAngleMin = -85.f;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnAngleMax = 85.f;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnDistanceMin = 10.f;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnDistanceMax = 50.f;
	
	UPROPERTY(EditAnywhere, Category = "Inventory")
	float RelativeSpawnElevation = 70.f; // 스폰위치를 아래로 밀고싶다? 뭔 소리야?
};
