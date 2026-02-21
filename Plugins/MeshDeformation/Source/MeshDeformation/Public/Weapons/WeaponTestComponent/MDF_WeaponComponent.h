// Gihyeon's MeshDeformation Project
// File: Source/MeshDeformation/Weapons/WeaponTestComponent/MDF_WeaponComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MDF_WeaponComponent.generated.h"

class AMDF_BaseWeapon;

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class MESHDEFORMATION_API UMDF_WeaponComponent : public UActorComponent
{
    GENERATED_BODY()

public: 
    UMDF_WeaponComponent();

protected:
    virtual void BeginPlay() override;

public: 
    // -------------------------------------------------------------------------
    // [핵심 기능] 캐릭터가 호출하는 함수들
    // -------------------------------------------------------------------------

    // 슬롯 번호(0=레이저, 1=총)로 무기 교체
    UFUNCTION(BlueprintCallable, Category = "MDF|Weapon")
    void EquipWeaponByIndex(int32 SlotIndex);

    // [New] 무기 해제 (맨손)
    UFUNCTION(BlueprintCallable, Category = "MDF|Weapon")
    void UnEquipWeapon();

    // 현재 들고 있는 무기 발사/중지
    void StartFire();
    void StopFire();

    // [New] 현재 무기 액터 반환 (없으면 nullptr)
    UFUNCTION(BlueprintCallable, Category = "MDF|Weapon")
    AMDF_BaseWeapon* GetCurrentWeapon() const { return CurrentWeaponActor; }

    // [New] 현재 무기의 슬롯 인덱스 반환 (없으면 -1)
    UFUNCTION(BlueprintCallable, Category = "MDF|Weapon")
    int32 GetCurrentWeaponIndex() const { return CurrentWeaponIndex; }

protected:
    // -------------------------------------------------------------------------
    // [설정] 블루프린트에서 무기 목록을 등록합니다.
    // -------------------------------------------------------------------------
    
    // 예: Index 0 = LaserGun, Index 1 = BreakerGun
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Setup", meta = (DisplayName = "무기 슬롯 목록"))
    TArray<TSubclassOf<AMDF_BaseWeapon>> WeaponSlots;

    // 소켓 이름 (예: WeaponSocket)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Setup")
    FName WeaponAttachSocketName;

private:
    // 현재 소환되어 손에 들린 무기
    UPROPERTY()
    TObjectPtr<AMDF_BaseWeapon> CurrentWeaponActor;

    // 현재 장착 중인 무기의 인덱스 (없으면 -1)
    int32 CurrentWeaponIndex = -1; 
};