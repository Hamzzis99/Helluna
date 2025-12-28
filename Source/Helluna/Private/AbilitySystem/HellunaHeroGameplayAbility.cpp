// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "Character/HellunaHeroCharacter.h"
#include "Controller/HellunaHeroController.h"

AHellunaHeroCharacter* UHellunaHeroGameplayAbility::GetHeroCharacterFromActorInfo()
{
	if (!CachedHellunaHeroCharacter.IsValid())
	{
		CachedHellunaHeroCharacter = Cast<AHellunaHeroCharacter>(CurrentActorInfo->AvatarActor);
	}

	return CachedHellunaHeroCharacter.IsValid() ? CachedHellunaHeroCharacter.Get() : nullptr;
}

AHellunaHeroController* UHellunaHeroGameplayAbility::GetHeroControllerFromActorInfo()
{
	if (!CachedHellunaHeroController.IsValid())
	{
		CachedHellunaHeroController = Cast<AHellunaHeroController>(CurrentActorInfo->PlayerController);
	}

	return CachedHellunaHeroController.IsValid() ? CachedHellunaHeroController.Get() : nullptr;
}

UHeroCombatComponent* UHellunaHeroGameplayAbility::GetHeroCombatComponentFromActorInfo()
{
	return GetHeroCharacterFromActorInfo()->GetHeroCombatComponent();
}
