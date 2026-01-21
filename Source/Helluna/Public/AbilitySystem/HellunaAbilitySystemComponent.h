// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "HellunaAbilitySystemComponent.generated.h"


/**
 * 
 */
UCLASS()
class HELLUNA_API UHellunaAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Helluna|Ability")
	bool TryActivateAbilityByTag(FGameplayTag AbilityTagToActivate);
	bool CancelAbilityByTag(const FGameplayTag AbilityTagToCancel);
	
	void OnAbilityInputPressed(const FGameplayTag& InInputTag);
	void OnAbilityInputReleased(const FGameplayTag& InInputTag);

	void AddStateTag(const FGameplayTag& Tag);
	void RemoveStateTag(const FGameplayTag& Tag);
	bool HasStateTag(const FGameplayTag& Tag) const;

};
