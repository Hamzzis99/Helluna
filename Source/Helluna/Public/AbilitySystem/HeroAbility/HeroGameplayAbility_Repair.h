// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Repair.generated.h"

class URepairMaterialWidget;

/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_Repair : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()
	
protected:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	void Repair(const FGameplayAbilityActorInfo* ActorInfo);

	// ⭐ Blueprint에서 설정할 Widget 클래스
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Repair|Widget", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<URepairMaterialWidget> RepairMaterialWidgetClass;

	// ⭐ 현재 열려있는 Widget 참조 (F키 토글용)
	UPROPERTY()
	TObjectPtr<URepairMaterialWidget> CurrentWidget;
	
};
