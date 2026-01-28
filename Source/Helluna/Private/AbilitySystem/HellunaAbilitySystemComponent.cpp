// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"
#include "AbilitySystem/HellunaHeroGameplayAbility.h"

#include "DebugHelper.h"


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

			if (SpecToActivate->IsActive()) continue;

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
			if (AbilitySpec.IsActive()) continue;  // 이미 활성화된 어빌리티는 무시

			TryActivateAbility(AbilitySpec.Handle);
			return;
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

bool UHellunaAbilitySystemComponent::CancelAbilityByTag(const FGameplayTag AbilityTagToCancel)  //어빌리티 취소 
{
	check(AbilityTagToCancel.IsValid());

	TArray<FGameplayAbilitySpec*> FoundAbilitySpecs;
	GetActivatableGameplayAbilitySpecsByAllMatchingTags(
		AbilityTagToCancel.GetSingleTagContainer(),
		FoundAbilitySpecs
	);

	bool bCanceledAny = false;

	for (FGameplayAbilitySpec* SpecToCancel : FoundAbilitySpecs)
	{
		if (!SpecToCancel) continue;
		if (!SpecToCancel->IsActive()) continue;

		CancelAbilityHandle(SpecToCancel->Handle);
		bCanceledAny = true;
	}

	return bCanceledAny;
}

void UHellunaAbilitySystemComponent::AddStateTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return;
	AddLooseGameplayTag(Tag);
}

void UHellunaAbilitySystemComponent::RemoveStateTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid()) return;
	RemoveLooseGameplayTag(Tag);
}

bool UHellunaAbilitySystemComponent::HasStateTag(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid()) return false;
	return HasMatchingGameplayTag(Tag);
}