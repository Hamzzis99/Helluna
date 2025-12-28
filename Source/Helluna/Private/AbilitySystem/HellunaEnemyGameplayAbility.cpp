// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "Character/HellunaEnemyCharacter.h"

AHellunaEnemyCharacter* UHellunaEnemyGameplayAbility::GetEnemyCharacterFromActorInfo()
{
	if (!CachedHellunaEnemyCharacter.IsValid())
	{
		CachedHellunaEnemyCharacter = Cast<AHellunaEnemyCharacter>(CurrentActorInfo->AvatarActor);
	}

	return CachedHellunaEnemyCharacter.IsValid() ? CachedHellunaEnemyCharacter.Get() : nullptr;
}

UEnemyCombatComponent* UHellunaEnemyGameplayAbility::GetEnemyCombatComponentFromActorInfo()
{
	return GetEnemyCharacterFromActorInfo()->GetEnemyCombatComponent();
}
