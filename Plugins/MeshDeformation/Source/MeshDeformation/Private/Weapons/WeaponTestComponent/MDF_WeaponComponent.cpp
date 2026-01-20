// Gihyeon's MeshDeformation Project
// File: Source/MeshDeformation/Weapons/WeaponTestComponent/MDF_WeaponComponent.cpp

#include "Weapons/WeaponTestComponent/MDF_WeaponComponent.h" // 경로 확인하세요
#include "Weapons/MDF_BaseWeapon.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

UMDF_WeaponComponent::UMDF_WeaponComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    WeaponAttachSocketName = FName("WeaponSocket"); // 기본값
}

void UMDF_WeaponComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UMDF_WeaponComponent::EquipWeaponByIndex(int32 SlotIndex)
{
    // 유효성 검사
    if (!WeaponSlots.IsValidIndex(SlotIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("[WeaponComp] 유효하지 않은 슬롯 인덱스: %d"), SlotIndex);
        return;
    }

    // 이미 같은 무기를 들고 있다면 패스
    if (CurrentWeaponActor && CurrentWeaponIndex == SlotIndex) return;

    // 1. 기존 무기 제거 (UnEquip)
    UnEquipWeapon();

    // 2. 새 무기 스폰
    if (WeaponSlots[SlotIndex])
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = GetOwner(); // 무기의 주인은 캐릭터
        SpawnParams.Instigator = Cast<APawn>(GetOwner());

        CurrentWeaponActor = GetWorld()->SpawnActor<AMDF_BaseWeapon>(WeaponSlots[SlotIndex], GetOwner()->GetActorTransform(), SpawnParams);
        
        if (CurrentWeaponActor)
        {
            // 캐릭터 손에 부착
            ACharacter* Char = Cast<ACharacter>(GetOwner());
            if (Char)
            {
                CurrentWeaponActor->AttachToComponent(Char->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponAttachSocketName);
            }
            
            CurrentWeaponIndex = SlotIndex; // 인덱스 갱신
            UE_LOG(LogTemp, Log, TEXT("[WeaponComp] 무기 장착 완료: %s (Slot: %d)"), *CurrentWeaponActor->GetName(), SlotIndex);
        }
    }
}

void UMDF_WeaponComponent::UnEquipWeapon()
{
    if (CurrentWeaponActor)
    {
        CurrentWeaponActor->Destroy();
        CurrentWeaponActor = nullptr;
    }
    CurrentWeaponIndex = -1; // -1은 '무기 없음' 상태
}

void UMDF_WeaponComponent::StartFire()
{
    if (CurrentWeaponActor)
    {
        CurrentWeaponActor->StartFire();
    }
}

void UMDF_WeaponComponent::StopFire()
{
    if (CurrentWeaponActor)
    {
        CurrentWeaponActor->StopFire();
    }
}