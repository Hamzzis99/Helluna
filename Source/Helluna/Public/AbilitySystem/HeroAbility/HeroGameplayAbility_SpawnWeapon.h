// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_SpawnWeapon.generated.h"

class AHellunaHeroWeapon;
/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_SpawnWeapon : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	void SpawnWeapon();


	// 스폰할 무기(에디터에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "Helluna|Weapon")
	TSubclassOf<AHellunaHeroWeapon> WeaponClass;

	// 장착 소켓 이름(에디터에서 지정)
	UPROPERTY(EditDefaultsOnly, Category = "Helluna|Weapon")
	FName AttachSocketName = TEXT("WeaponSocket");


		
};
