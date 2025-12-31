// Gihyeon's Inventory Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rendering/RenderCommandPipes.h"
#include "Inv_ProxyMesh.generated.h"

class UInv_EquipmentComponent;

UCLASS()
class INVENTORY_API AInv_ProxyMesh : public AActor
{
	GENERATED_BODY()

public:
	AInv_ProxyMesh();
	USkeletalMeshComponent* GetMesh() const { return Mesh; } // 캐릭터 디스플레이뜨게해주는 것.

protected:
	virtual void BeginPlay() override;

private:

	// This is the mesh on the player-controlled Character.
	TWeakObjectPtr<USkeletalMeshComponent> SourceMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInv_EquipmentComponent> EquipmentComponent;

	// This is the proxy mesh we will see in the Inventory Menu.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	FTimerHandle TimerForNextTick;
	void DelayedInitializeOwner();
	void DelayedInitialization();
};