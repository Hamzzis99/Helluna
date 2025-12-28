// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Aim.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "HellunaGameplayTags.h"
#include "GameFramework/SpringArmComponent.h"

#include "DebugHelper.h"

void UHeroGameplayAbility_Aim::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{

	ReadAimMovement();
	ReadAimCamera();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UHeroGameplayAbility_Aim::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ResetAimMovement();
	ResetAimCamera();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_Aim::ReadAimMovement()
{

	GetHeroCharacterFromActorInfo()->GetCharacterMovement()->MaxWalkSpeed = AimMaxWalkSpeed;

}

void UHeroGameplayAbility_Aim::ReadAimCamera()
{
	GetHeroCharacterFromActorInfo()->GetFollowCamera()->SetFieldOfView(AimFov);

	USpringArmComponent* CameraBoom = GetHeroCharacterFromActorInfo()->GetCameraBoom();
	CameraBoom->TargetArmLength -= Aimlocation;

}

void UHeroGameplayAbility_Aim::ResetAimMovement()
{
	if (CachedDefaultMaxWalkSpeed > 0.f)
	{
		GetHeroCharacterFromActorInfo()->GetCharacterMovement()->MaxWalkSpeed = CachedDefaultMaxWalkSpeed;
	}
}

void UHeroGameplayAbility_Aim::ResetAimCamera()
{
	if (CachedDefaultAimFov > 0.f)
	{
		GetHeroCharacterFromActorInfo()->GetFollowCamera()->SetFieldOfView(CachedDefaultAimFov);

		USpringArmComponent* CameraBoom = GetHeroCharacterFromActorInfo()->GetCameraBoom();
		CameraBoom->TargetArmLength += Aimlocation;
	}
}

