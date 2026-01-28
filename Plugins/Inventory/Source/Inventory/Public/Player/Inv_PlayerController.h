#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Inv_PlayerController.generated.h"

class UInv_InventoryComponent;
class UInv_EquipmentComponent;
class UInputMappingContext;
class UInputAction;
class UInv_HUDWidget;

UCLASS()
class INVENTORY_API AInv_PlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	AInv_PlayerController();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleInventory();

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
