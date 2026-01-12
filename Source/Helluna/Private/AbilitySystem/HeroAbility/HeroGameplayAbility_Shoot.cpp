// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Shoot.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HellunaHeroWeapon.h"

#include "DebugHelper.h"


void UHeroGameplayAbility_Shoot::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{

	Shoot();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

}

void UHeroGameplayAbility_Shoot::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_Shoot::Shoot()
{

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHellunaHeroWeapon* Weapon = Hero->GetCurrentWeapon();
	if (!Weapon) return;

	AController* Controller = Hero->GetController();
	if (Controller)
	{
		Weapon->Fire(Controller);
	}

	// 위로 튀는 반동
	const float PitchKick = Weapon->ReboundUp;
	const float YawKick = FMath::RandRange(-Weapon->ReboundLeftRight, Weapon->ReboundLeftRight);

	Character->AddControllerPitchInput(-PitchKick);
	Character->AddControllerYawInput(YawKick);	
}
