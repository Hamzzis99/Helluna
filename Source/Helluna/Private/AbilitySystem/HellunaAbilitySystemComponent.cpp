// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"


bool UHellunaAbilitySystemComponent::TryActivateAbilityByTag(FGameplayTag AbilityTagToActivate)
{
	check(AbilityTagToActivate.IsValid());

	TArray<FGameplayAbilitySpec*> FoundAbilitySpecs;
	GetActivatableGameplayAbilitySpecsByAllMatchingTags(AbilityTagToActivate.GetSingleTagContainer(), FoundAbilitySpecs);

	if (!FoundAbilitySpecs.IsEmpty())
	{
		for (FGameplayAbilitySpec* SpecToActivate : FoundAbilitySpecs)
		{
			if (!SpecToActivate) continue;

			// 이미 켜져있으면 스킵 (취향)
			if (SpecToActivate->IsActive()) continue;

			// 이게 성공하는 놈(=상태태그 조건 만족하는 놈) 나오면 바로 끝
			if (TryActivateAbility(SpecToActivate->Handle))
			{
				return true;
			}
		}
	}

	return false;
}

void UHellunaAbilitySystemComponent::OnAbilityInputPressed(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (!AbilitySpec.DynamicAbilityTags.HasTagExact(InInputTag)) continue;

		const UHellunaHeroGameplayAbility* HellunaGA = Cast<UHellunaHeroGameplayAbility>(AbilitySpec.Ability);
		const EHellunaInputActionPolicy Policy = HellunaGA ? HellunaGA->InputActionPolicy : EHellunaInputActionPolicy::Trigger;

		if (Policy == EHellunaInputActionPolicy::Toggle)
		{
			if (AbilitySpec.IsActive()) 
				CancelAbilityHandle(AbilitySpec.Handle);
			else 
				TryActivateAbility(AbilitySpec.Handle);
		}
		else // Trigger
		{
			TryActivateAbility(AbilitySpec.Handle);
		}
	}
}

void UHellunaAbilitySystemComponent::OnAbilityInputReleased(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (!AbilitySpec.DynamicAbilityTags.HasTagExact(InInputTag))
		{
			continue;
		}

		const UHellunaHeroGameplayAbility* HellunaGA = Cast<UHellunaHeroGameplayAbility>(AbilitySpec.Ability);
		const EHellunaInputActionPolicy Policy =
			HellunaGA ? HellunaGA->InputActionPolicy : EHellunaInputActionPolicy::Trigger;

		if (Policy == EHellunaInputActionPolicy::Hold)
		{
			if (AbilitySpec.IsActive())
			{
				CancelAbilityHandle(AbilitySpec.Handle);  // -> EndAbility
			}
			continue; 
		}

		if (Policy == EHellunaInputActionPolicy::Toggle)
		{
			continue;
		}

		if (AbilitySpec.IsActive())
		{
			AbilitySpec.Ability->InputReleased(
				AbilitySpec.Handle,
				AbilityActorInfo.Get(),
				AbilitySpec.ActivationInfo
			);
		}
	}
}