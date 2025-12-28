// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Shoot.h"
#include "HeroGameplayAbility_Shoot_Auto.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_Shoot_Auto : public UHeroGameplayAbility_Shoot
{
	GENERATED_BODY()
	
protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void StartAutoShoot();
	void StopAutoShoot();

	FTimerHandle ShootTimerHandle;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot")
	float ShootInterval = 0.1f;

};
