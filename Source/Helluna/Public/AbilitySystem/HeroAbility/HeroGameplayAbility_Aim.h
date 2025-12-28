// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Aim.generated.h"



/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_Aim : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
private:


	void ReadAimMovement();
	void ReadAimCamera();

	void ResetAimMovement();
	void ResetAimCamera();

	UPROPERTY(EditDefaultsOnly, Category = "Aim")
	float AimMaxWalkSpeed = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aim")
	float AimFov = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aim")
	float Aimlocation = 150.f;

	UPROPERTY()
	float CachedDefaultMaxWalkSpeed = 400.f;

	UPROPERTY()
	float CachedDefaultAimFov = 120.f;


	
};
