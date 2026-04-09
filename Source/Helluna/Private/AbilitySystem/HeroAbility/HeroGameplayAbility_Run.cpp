// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Run.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HellunaGameplayTags.h"

#include "DebugHelper.h"

void UHeroGameplayAbility_Run::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	AHellunaHeroCharacter* RunHero = GetHeroCharacterFromActorInfo();
	RunHero->GetCharacterMovement()->MaxWalkSpeed = RunSpeed * RunHero->GetMoveSpeedMultiplier();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
}

void UHeroGameplayAbility_Run::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	CleanUp();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

bool UHeroGameplayAbility_Run::IsRun()
{
	return GetAbilitySystemComponentFromActorInfo()
		->HasMatchingGameplayTag(HellunaGameplayTags::Player_status_Run);
}

void UHeroGameplayAbility_Run::CleanUp()
{
	if (CachedDefaultMaxWalkSpeed > 0.f)
	{
		AHellunaHeroCharacter* RunHero = GetHeroCharacterFromActorInfo();
		RunHero->GetCharacterMovement()->MaxWalkSpeed = 400.f * RunHero->GetMoveSpeedMultiplier();
	}
}