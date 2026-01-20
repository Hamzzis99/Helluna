// Gihyeon's MeshDeformation Project

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDF_BaseWeapon.generated.h"

/**
 * [Step 1] 모든 무기의 기본 클래스
 * - 탄약(배터리) 관리
 * - 발사 타이머 관리 (연사 속도)
 * - [New] 무기 외형(StaticMesh) 기본 포함
 */
UCLASS()
class MESHDEFORMATION_API AMDF_BaseWeapon : public AActor
{
    GENERATED_BODY()
    
public: 
    AMDF_BaseWeapon();

protected:
    virtual void BeginPlay() override;

public:
    // -------------------------------------------------------------------------
    // [핵심 동작] 캐릭터가 이 함수들을 호출합니다.
    // -------------------------------------------------------------------------
    
    UFUNCTION(BlueprintCallable, Category = "MDF|Weapon", meta = (DisplayName = "발사 시작 (Start Fire)"))
    virtual void StartFire();

    UFUNCTION(BlueprintCallable, Category = "MDF|Weapon", meta = (DisplayName = "발사 중지 (Stop Fire)"))
    virtual void StopFire();

protected:
    // -------------------------------------------------------------------------
    // [내부 로직] 자식 클래스(레이저/총)가 오버라이드 할 함수
    // -------------------------------------------------------------------------

    // 실제로 총알이 나가는 순간 (타이머에 의해 반복 호출됨)
    virtual void Fire();

    // 탄약 소비 처리
    void ConsumeAmmo();

protected:
    // -------------------------------------------------------------------------
    // [설정 변수] 블루프린트 디테일 패널에서 한글로 보입니다.
    // -------------------------------------------------------------------------

    // [New] 무기의 외형을 담당하는 스태틱 메시 (이제 이게 루트 컴포넌트가 됩니다)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF|Visual", meta = (DisplayName = "무기 외형 (Static Mesh)"))
    TObjectPtr<UStaticMeshComponent> WeaponMesh;

    // 총구 위치 (WeaponMesh의 자식으로 붙습니다)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF|Visual", meta = (DisplayName = "총구 위치 (Muzzle)"))
    TObjectPtr<USceneComponent> MuzzleLocation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Stats", meta = (DisplayName = "최대 탄약/배터리 (Max Ammo)"))
    float MaxAmmo;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MDF|Stats", meta = (DisplayName = "현재 탄약 (Current Ammo)"))
    float CurrentAmmo;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Stats", meta = (DisplayName = "연사 속도 (Fire Rate, 초 단위)"))
    float FireRate;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Stats", meta = (DisplayName = "사거리 (Range)"))
    float FireRange;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Effects", meta = (DisplayName = "발사 효과음 (Sound)"))
    TObjectPtr<USoundBase> FireSound;

private:
    // 연사를 위한 타이머 핸들
    FTimerHandle FireTimerHandle;
};