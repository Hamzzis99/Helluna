// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inv_EquipmentComponent.generated.h"

struct FGameplayTag;
struct FInv_ItemManifest;
class AInv_EquipActor;
struct FInv_EquipmentFragment;
class UInv_InventoryItem;
class UInv_InventoryComponent;
class APlayerController;
class USkeletalMeshComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable)
class INVENTORY_API UInv_EquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	void SetOwningSkeletalMesh(USkeletalMeshComponent* OwningMesh);
	void SetIsProxy(bool bProxy) { bIsProxy = bProxy; }
	void InitializeOwner(APlayerController* PlayerController);
	
protected:
	
	virtual void BeginPlay() override;

private:
	TWeakObjectPtr<UInv_InventoryComponent> InventoryComponent;
	TWeakObjectPtr<APlayerController> OwningPlayerController;
	TWeakObjectPtr<USkeletalMeshComponent> OwningSkeletalMesh; // 아이템 장착 골격
	
	// 델리게이트 바인딩을 대비하기 위한 함수들 콜백 함수들 
	UFUNCTION()
	void OnItemEquipped(UInv_InventoryItem* EquippedItem);

	UFUNCTION()
	void OnItemUnequipped(UInv_InventoryItem* UnequippedItem);
	
	void InitPlayerController(); //멀티플레이
	void InitInventoryComponent();
	AInv_EquipActor* SpawnEquippedActor(FInv_EquipmentFragment* EquipmentFragment, const FInv_ItemManifest& Manifest, USkeletalMeshComponent* AttachMesh);
	
	UPROPERTY()
	TArray<TObjectPtr<AInv_EquipActor>> EquippedActors;
	
	AInv_EquipActor* FindEquippedActor(const FGameplayTag& EquipmentTypeTag); 
	void RemoveEquippedActor(const FGameplayTag& EquipmentTypeTag);
	
	UFUNCTION()
	void OnPossessedPawnChange(APawn* OldPawn, APawn* NewPawn); // 멀티플레이 장착 아이템 변경 할 떄 폰 변경 시 호출되는 함수
	
	bool bIsProxy{false};
};
