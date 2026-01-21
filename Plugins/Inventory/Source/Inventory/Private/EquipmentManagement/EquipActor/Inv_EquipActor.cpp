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
	
	// ⭐ [WeaponBridge] bIsWeaponHidden 리플리케이트
	DOREPLIFETIME(AInv_EquipActor, bIsWeaponHidden);
}

// ⭐ [WeaponBridge] 무기 숨김/표시 설정 (서버에서 호출)
void AInv_EquipActor::SetWeaponHidden(bool bHidden)
{
	bIsWeaponHidden = bHidden;
	SetActorHiddenInGame(bHidden);
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [Inv_EquipActor] SetWeaponHidden: %s"), bHidden ? TEXT("Hidden") : TEXT("Visible"));
}

// ⭐ [WeaponBridge] 클라이언트에서 리플리케이션 수신 시 호출
void AInv_EquipActor::OnRep_IsWeaponHidden()
{
	SetActorHiddenInGame(bIsWeaponHidden);
	
	UE_LOG(LogTemp, Warning, TEXT("⭐ [Inv_EquipActor] OnRep_IsWeaponHidden: %s"), bIsWeaponHidden ? TEXT("Hidden") : TEXT("Visible"));
}
