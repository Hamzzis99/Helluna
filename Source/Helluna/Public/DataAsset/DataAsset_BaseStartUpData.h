// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DataAsset_BaseStartUpData.generated.h"

class UHellunaGameplayAbility;
class UHellunaAbilitySystemComponent;
class UGameplayEffect;
/**
 * 
 */
UCLASS()
class HELLUNA_API UDataAsset_BaseStartUpData : public UDataAsset
{
	GENERATED_BODY()

public:
	virtual void GiveToAbilitySystemComponent(UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "StartUpData")
	TArray< TSubclassOf < UHellunaGameplayAbility > > ActivateOnGivenAbilities;

	UPROPERTY(EditDefaultsOnly, Category = "StartUpData")
	TArray< TSubclassOf < UHellunaGameplayAbility > > ReactiveAbilities;

	void GrantAbilities(const TArray< TSubclassOf < UHellunaGameplayAbility > >& InAbilitiesToGive, UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1);

};
