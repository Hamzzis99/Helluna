// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Shoot.generated.h"

class AHeroWeapon_GunBase;
/**
 * 
 */
UCLASS()
class HELLUNA_API UHeroGameplayAbility_Shoot : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()
	
protected:

	UHeroGameplayAbility_Shoot();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	void Shoot();

	// [AimFix] 클라이언트 카메라 기준 조준점(AimPoint) 계산
	static FVector ComputeAimPointFromCamera(const AHellunaHeroCharacter* Hero);

private:

	void TickAutoFire();
	FTimerHandle AutoFireTimerHandle;
	TWeakObjectPtr<AHeroWeapon_GunBase> FiringWeapon;
	double LastAutoFireTime = 0.0;
	double AutoFireTimeRemaining = 0.0;

};
