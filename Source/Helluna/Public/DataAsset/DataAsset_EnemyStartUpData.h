// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DataAsset/DataAsset_BaseStartUpData.h"
#include "DataAsset_EnemyStartUpData.generated.h"

class UHellunaEnemyGameplayAbility;
/**
 * 
 */
UCLASS()
class HELLUNA_API UDataAsset_EnemyStartUpData : public UDataAsset_BaseStartUpData
{
	GENERATED_BODY()

public:
	virtual void GiveToAbilitySystemComponent(UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel = 1) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "StartUpData")
	TArray< TSubclassOf < UHellunaEnemyGameplayAbility > > EnemyCombatAbilities;
};
