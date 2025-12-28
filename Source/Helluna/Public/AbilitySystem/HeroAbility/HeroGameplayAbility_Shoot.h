// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Shoot.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_Shoot : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()
	
protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	void Shoot();

private:

	UPROPERTY(EditDefaultsOnly, Category = "Shoot")
	float ReboundUp = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot")
	float ReboundLeftRight = 0.2f;


};
