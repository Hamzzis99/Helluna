// Gihyeon's MeshDeformation Project
// File: Source/MeshDeformation/Weapon/MDF_RifleWeapon.cpp

#include "Weapons/MDF_RifleWeapon.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

AMDF_RifleWeapon::AMDF_RifleWeapon()
{
    FireRate = 0.1f;
    FireRange = 5000.0f;
    MaxAmmo = 300.0f;
    CurrentAmmo = MaxAmmo;
    DamagePerShot = 10.0f;
    RifleRangedDamageType = nullptr; // 블루프린트에서 설정하거나 기본 UDamageType 사용
}

void AMDF_RifleWeapon::Fire()
{
    Super::Fire();

    if (!MuzzleLocation) return;

    FVector Start = MuzzleLocation->GetComponentLocation();
    FVector Forward = MuzzleLocation->GetForwardVector();
    FVector End = Start + (Forward * FireRange);

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);
    Params.AddIgnoredActor(GetOwner());

    bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);

    if (bHit && HitResult.GetActor())
    {
        // [시각화] 총알 궤적
        DrawDebugLine(GetWorld(), Start, HitResult.Location, FColor::Yellow, false, 0.05f, 0, 1.0f);
        DrawDebugPoint(GetWorld(), HitResult.Location, 10.0f, FColor::Yellow, false, 0.05f);

        // ---------------------------------------------------------------------
        // [핵심 변경] 언리얼 표준 데미지 시스템 사용
        // 이제 MDF_DeformableComponent::HandlePointDamage가 정상 호출됨
        // MDF_MiniGameComponent도 부모의 델리게이트를 통해 자동으로 받음
        // ---------------------------------------------------------------------
        TSubclassOf<UDamageType> DamageTypeToUse = RifleRangedDamageType ? RifleRangedDamageType : TSubclassOf<UDamageType>(UDamageType::StaticClass());
        
        UGameplayStatics::ApplyPointDamage(
            HitResult.GetActor(),                                    // 맞은 액터
            DamagePerShot,                                           // 데미지량
            Forward,                                                 // 발사 방향
            HitResult,                                               // 히트 정보 (위치, 본 등)
            GetInstigatorController(),                               // 공격자 컨트롤러
            this,                                                    // 데미지 유발자 (무기)
            DamageTypeToUse                                          // 데미지 타입
        );

        UE_LOG(LogTemp, Log, TEXT("[Rifle] 타격! Actor: %s, Damage: %.1f"), *HitResult.GetActor()->GetName(), DamagePerShot);
    }
    else
    {
        DrawDebugLine(GetWorld(), Start, End, FColor::Red, false, 0.05f, 0, 1.0f);
    }
}