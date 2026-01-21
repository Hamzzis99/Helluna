// Gihyeon's Inventory Project


#include "EquipmentManagement/EquipActor/Inv_EquipActor.h"
#include "Net/UnrealNetwork.h"


AInv_EquipActor::AInv_EquipActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // 서버하고 교환해야 하니 RPC를 켜야겠지?
}

void AInv_EquipActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// ⭐ [WeaponBridge] WeaponSlotIndex 리플리케이트
	DOREPLIFETIME(AInv_EquipActor, WeaponSlotIndex);
}

