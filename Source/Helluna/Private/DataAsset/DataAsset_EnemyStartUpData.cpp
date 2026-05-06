// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/DataAsset_EnemyStartUpData.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaEnemyGameplayAbility.h"

void UDataAsset_EnemyStartUpData::GiveToAbilitySystemComponent(UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	// [StreamableSafetyV1] PIE 종료 후 streamable callback 이 destroyed ASC 에 호출되어
	//   GiveAbility 시 access violation 사고. 진입 시 ASC valid 가드.
	if (!IsValid(InASCToGive)) return;

	Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);

	if (!EnemyCombatAbilities.IsEmpty())
	{
		for (const TSubclassOf < UHellunaEnemyGameplayAbility >& AbilityClass : EnemyCombatAbilities)
		{
			if (!AbilityClass) continue;

			FGameplayAbilitySpec AbilitySpec(AbilityClass);
			AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
			AbilitySpec.Level = ApplyLevel;

			InASCToGive->GiveAbility(AbilitySpec);
		}
	}
}