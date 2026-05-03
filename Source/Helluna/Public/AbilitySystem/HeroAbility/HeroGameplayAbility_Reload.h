// Capstone Project Helluna

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "HeroGameplayAbility_Reload.generated.h"

/**
 * 
 */
class UAbilityTask_PlayMontageAndWait;
class AHeroWeapon_GunBase;

UCLASS()
class HELLUNA_API UHeroGameplayAbility_Reload : public UHellunaHeroGameplayAbility
{
	GENERATED_BODY()
public:
	UHeroGameplayAbility_Reload();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnReloadFinished();

	UFUNCTION()
	void OnReloadInterrupted();

private:
	UPROPERTY()
	UAbilityTask_PlayMontageAndWait* ReloadTask = nullptr;

	UPROPERTY()
	AHeroWeapon_GunBase* Weapon = nullptr;

	// [Fix:reentry-guard 2026-05-02] 몽타주 콜백 이중 호출 차단 (GunParry 동일 패턴)
	bool bReloadEndCalled = false;
};
