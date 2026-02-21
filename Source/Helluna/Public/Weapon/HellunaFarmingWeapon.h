// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "HellunaFarmingWeapon.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API AHellunaFarmingWeapon : public AHellunaHeroWeapon
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "FarmingWeapon")
	float GetFarmingDamage() const { return Damage; }

	// ✅ 서버에서만 호출되게 GA에서 보장
	UFUNCTION(BlueprintCallable, Category = "FarmingWeapon")
	virtual void Farm(AController* InstigatorController, AActor* Target);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FarmingWeapon|FX")
	USoundBase* FarmHitSound = nullptr;

	
	
};
