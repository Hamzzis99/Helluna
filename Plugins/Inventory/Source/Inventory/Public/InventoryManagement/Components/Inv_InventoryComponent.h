// Gihyeon's Inventory Project

//인벤토리 베이스 자식 클래스

#pragma once

#include "CoreMinimal.h"
//#include "Widgets/Inventory/InventoryBase/Inv_InventoryBase.h"
#include "Components/ActorComponent.h"
#include "InventoryManagement/FastArray/Inv_FastArray.h"
#include "Inv_InventoryComponent.generated.h"

class UInv_ItemComponent;
class UInv_InventoryItem;
class UInv_InventoryBase;

//델리게이트
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInventoryItemChange, UInv_InventoryItem*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoRoomInInventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStackChange, const FInv_SlotAvailabilityResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FItemEquipStatusChanged, UInv_InventoryItem*, Item);
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

	UFUNCTION(Server, Reliable) // 크래프팅: 서버에서 아이템 생성 및 인벤토리 추가
	void Server_CraftItem(TSubclassOf<AActor> ItemActorClass);

	UFUNCTION(NetMulticast, Reliable) // 모든 클라이언트의 UI 업데이트 (Building 재료 차감)
	void Multicast_ConsumeMaterialsUI(const FGameplayTag& MaterialTag, int32 Amount);

	// 같은 타입의 모든 스택 개수 합산 (Building UI용)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 GetTotalMaterialCount(const FGameplayTag& MaterialTag) const;
	
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip);
	
	UFUNCTION(NetMulticast, Reliable) // 멀티캐스트 함수 (서버에서 모든 클라이언트로 호출)
	void Multicast_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip);
	
	
	//서버 RPC 전송하는 부분 함수들
	
	void ToggleInventoryMenu(); //인벤토리 메뉴 토글 함수
	void AddRepSubObj(UObject* SubObj); //복제 하위 객체 추가 함수
	void SpawnDroppedItem(UInv_InventoryItem* Item, int32 StackCount); // 떨어진 아이템 생성 함수
	UInv_InventoryBase* GetInventoryMenu() const {return InventoryMenu;};
	bool IsMenuOpen() const { return bInventoryMenuOpen; }

	// InventoryList 접근용 (재료 체크 등에 사용)
	const FInv_InventoryFastArray& GetInventoryList() const { return InventoryList; }
	FInv_InventoryFastArray& GetInventoryList() { return InventoryList; } // non-const 오버로드
	
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

private:

	TWeakObjectPtr<APlayerController> OwningController;

	void ConstructInventory();

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
