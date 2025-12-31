// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/Manifest/Inv_ItemManifest.h"
#include "Inv_ItemComponent.generated.h"


//Blueprintable: 블루프린트 클래스로 만들 수 있게 하는 것.
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable )
class INVENTORY_API UInv_ItemComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UInv_ItemComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitItemManifest(FInv_ItemManifest CopyOfManifest); // 아이템 매니페스트 초기화 함수
	FInv_ItemManifest GetItemManifest() const { return ItemManifest; } // 아이템 매니페스트 반환 함수
	FInv_ItemManifest GetItemManifestMutable() { return ItemManifest; } 
	FString GetPickupMessage() const { return PickupMessage;  }
	void PickedUp(); // 아이템이 줍혔을 때 호출되는 함수

protected:

	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void OnPickedUp(); // 아이템이 줍혔을 때 호출되는 보호된 함수

private:
	UPROPERTY(Replicated, EditAnywhere, Category = "Inventory")
	FInv_ItemManifest ItemManifest;


	UPROPERTY(EditAnywhere, Category = "Inventory")
	FString PickupMessage;
};
