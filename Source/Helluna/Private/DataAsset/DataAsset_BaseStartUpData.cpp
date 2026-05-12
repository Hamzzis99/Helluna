	// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/DataAsset_BaseStartUpData.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HellunaGameplayAbility.h"

void UDataAsset_BaseStartUpData::GiveToAbilitySystemComponent(UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	// [Fix:check-removal 2026-05-02] check() 다운그레이드.
	// 비동기 로드 콜백/리플 타이밍으로 InASCToGive nullptr 가능 → safe return.
	if (!InASCToGive) return;

	GrantAbilities(ActivateOnGivenAbilities, InASCToGive, ApplyLevel);
	GrantAbilities(ReactiveAbilities, InASCToGive, ApplyLevel);
}

void UDataAsset_BaseStartUpData::GrantAbilities(const TArray<TSubclassOf<UHellunaGameplayAbility>>& InAbilitiesToGive, UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
	if (InAbilitiesToGive.IsEmpty())
	{
		return;
	}

	for (const TSubclassOf<UHellunaGameplayAbility>& Ability : InAbilitiesToGive)
	{
		if (!Ability) continue;

		FGameplayAbilitySpec AbilitySpec(Ability);
		AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
		AbilitySpec.Level = ApplyLevel;

		InASCToGive->GiveAbility(AbilitySpec);
	}
}