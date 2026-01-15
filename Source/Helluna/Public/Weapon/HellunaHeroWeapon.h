// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Weapon/HellunaWeaponBase.h"
#include "HellunaHeroWeapon.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct FWeaponAnimationSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	UAnimMontage* Equip;

};


UCLASS()
class HELLUNA_API AHellunaHeroWeapon : public AHellunaWeaponBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float AttackSpeed = 0.1f; // 연사속도

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float ReboundUp = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float ReboundLeftRight = 0.5f;

	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	virtual void Fire(AController* InstigatorController);

	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Animation")
	FWeaponAnimationSet AnimSet;

	const FWeaponAnimationSet& GetAnimSet() const { return AnimSet; }
	
};
