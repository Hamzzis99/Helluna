// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "HellunaEnemyGameplayAbility.generated.h"

class AHellunaEnemyCharacter;
class UEnemyCombatComponent;
/**
 * 
 */
UCLASS()
class HELLUNA_API UHellunaEnemyGameplayAbility : public UHellunaGameplayAbility
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	AHellunaEnemyCharacter* GetEnemyCharacterFromActorInfo();

	UFUNCTION(BlueprintPure, Category = "Helluna|Ability")
	UEnemyCombatComponent* GetEnemyCombatComponentFromActorInfo();

private:
	TWeakObjectPtr<AHellunaEnemyCharacter> CachedHellunaEnemyCharacter;
	
};
