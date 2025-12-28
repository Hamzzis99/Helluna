// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Run.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_Run : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;


private:

	bool IsRun();

	UPROPERTY()
	TObjectPtr<AHellunaHeroCharacter> Hero = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Run")
	float RunSpeed = 800.f;

	UPROPERTY()
	float CachedDefaultMaxWalkSpeed = 400.f;

	void CleanUp();


};
