// Gihyeon's MeshDeformation Project
// File: Source/MeshDeformation/Weapon/MDF_RifleWeapon.h

#pragma once

#include "CoreMinimal.h"
#include "Weapons/MDF_BaseWeapon.h"
#include "MDF_RifleWeapon.generated.h"

/**
 * [Step 5 Test] 슈터용 라이플
 * - 레이저 대신 즉발(HitScan) 사격을 합니다.
 * - 언리얼 표준 데미지 시스템(ApplyPointDamage)을 사용하여
 *   MDF_DeformableComponent의 HandlePointDamage가 정상 호출됩니다.
 */
UCLASS()
class MESHDEFORMATION_API AMDF_RifleWeapon : public AMDF_BaseWeapon
{
	GENERATED_BODY()
    
public:
	AMDF_RifleWeapon();

protected:
	virtual void Fire() override;

protected:
	// [설정] 한 발당 데미지
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Stats", meta = (DisplayName = "발당 데미지"))
	float DamagePerShot;

	// [NEW] 원거리 데미지 타입 - MDF_DeformableComponent에서 무기 종류 판별용
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MDF|Stats", meta = (DisplayName = "데미지 타입 (원거리)"))
	TSubclassOf<UDamageType> RifleRangedDamageType;
};