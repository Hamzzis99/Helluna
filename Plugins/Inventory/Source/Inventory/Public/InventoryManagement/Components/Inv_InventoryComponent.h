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
	void Server_AddNewItem(UInv_ItemComponent* ItemComponent, int32 StackCount);

	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_AddStacksToItem(UInv_ItemComponent* ItemComponent, int32 StackCount, int32 Remainder);

	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_DropItem(UInv_InventoryItem* Item, int32 StackCount); // 아이템을 서버에다 어떻게 버릴지
	
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_ConsumeItem(UInv_InventoryItem* Item);
	
	UFUNCTION(Server, Reliable) // 신뢰하는 것? 서버에 전달하는 것?
	void Server_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip);
	
	UFUNCTION(NetMulticast, Reliable) // 멀티캐스트 함수 (서버에서 모든 클라이언트로 호출)
	void Multicast_EquipSlotClicked(UInv_InventoryItem* ItemToEquip, UInv_InventoryItem* ItemToUnequip);
	
	
	//서버 RPC 전송하는 부분 함수들
	
	void ToggleInventoryMenu(); //인벤토리 메뉴 토글 함수
	void AddRepSubObj(UObject* SubObj); //복제 하위 객체 추가 함수
	void SpawnDroppedItem(UInv_InventoryItem* Item, int32 StackCount); // 떨어진 아이템 생성 함수
	UInv_InventoryBase* GetInventoryMenu() const {return InventoryMenu;};
	
	// 서버 브로드캐스트 함수들.
	FInventoryItemChange OnItemAdded;
	FInventoryItemChange OnItemRemoved;
	FNoRoomInInventory NoRoomInInventory;
	FStackChange OnStackChange;
	FItemEquipStatusChanged OnItemEquipped;
	FItemEquipStatusChanged OnItemUnequipped;
	FInventoryMenuToggled OnInventoryMenuToggled;
	
	
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
