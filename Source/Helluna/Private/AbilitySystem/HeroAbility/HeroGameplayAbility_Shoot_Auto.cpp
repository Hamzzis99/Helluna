// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Shoot_Auto.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"

#include "DebugHelper.h"

void UHeroGameplayAbility_Shoot_Auto::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{

	StartAutoShoot();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);


}

void UHeroGameplayAbility_Shoot_Auto::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{

	StopAutoShoot();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_Shoot_Auto::StartAutoShoot()
{
	if (!GetWorld())
		return;

	// 중복 방지
	if (GetWorld()->GetTimerManager().IsTimerActive(ShootTimerHandle))
		return;

	Shoot();

	GetWorld()->GetTimerManager().SetTimer(
		ShootTimerHandle,
		this,
		&ThisClass::Shoot,
		ShootInterval,
		true
	);
}

void UHeroGameplayAbility_Shoot_Auto::StopAutoShoot()
{
	if (!GetWorld())
		return;

	GetWorld()->GetTimerManager().ClearTimer(ShootTimerHandle);
}
