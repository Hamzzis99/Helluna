// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "HellunaGameplayAbility.generated.h"

class UPawnCombatComponent;
class UHellunaAbilitySystemComponent;
/**
 * 
 */

UENUM(BlueprintType)
enum class EHellunaAbilityActivationPolicy : uint8
{
	OnTriggered,
	OnGiven
};


UCLASS()
class HELLUNA_API UHellunaGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

protected:
	//~ Begin UGameplayAbility Interface.
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec);
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled);
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo);
	//~ End UGameplayAbility Interface

	UPROPERTY(EditDefaultsOnly, Category = "HellunaAbility")
	EHellunaAbilityActivationPolicy AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;

	UFUNCTION(BlueprintPure, Category = "Warrior|Ability")
	UPawnCombatComponent* GetPawnCombatComponentFromActorInfo() const;

	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	UHellunaAbilitySystemComponent* GetHellunaAbilitySystemComponentFromActorInfo() const;
	
};
	