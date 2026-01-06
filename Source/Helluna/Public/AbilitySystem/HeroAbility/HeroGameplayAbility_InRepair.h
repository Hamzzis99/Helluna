// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_InRepair.generated.h"

/**
 * 
 */
class UUserWidget;
class AResourceUsingObject_SpaceShip;

UCLASS()
class HELLUNA_API UHeroGameplayAbility_InRepair : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()
	
protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> RepairWidgetClass;
	
	UPROPERTY()
	TObjectPtr<UUserWidget> RepairWidgetInstance;

	void ShowRepairUI(const FGameplayAbilityActorInfo* ActorInfo);
	void RemoveRepairUI();
	AResourceUsingObject_SpaceShip* GetSpaceShip() const;
	
};
