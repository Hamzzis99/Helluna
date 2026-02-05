// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HeroWeapon_GunBase.h"
#include "HeroWeapon_Launcher.generated.h"

/**
 * 
 */
class AHellunaProjectile_Launcher;

UCLASS()
class HELLUNA_API AHeroWeapon_Launcher : public AHeroWeapon_GunBase
{
	GENERATED_BODY()
	
public:
	virtual void Fire(AController* InstigatorController) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (DisplayName = "투사체 클래스"))
	TSubclassOf<AHellunaProjectile_Launcher> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (DisplayName = "투사체 속도"))
	float ProjectileSpeed = 9000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (DisplayName = "폭발 반경"))
	float ProjectileExplosionRadius = 350.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (DisplayName = "머즐 소켓"))
	FName MuzzleSocketName = TEXT("Muzzle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Projectile", meta = (DisplayName = "대체 스폰 오프셋"))
	float FallbackSpawnOffset = 80.f;
	
	
};
