// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/DataAsset_HeroStartUpData.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Block.h"
#include "HellunaGameplayTags.h"

bool FHellunaHeroAbilitySet::IsValid() const
{
    return InputTag.IsValid() && AbilityToGrant;
}

void UDataAsset_HeroStartUpData::GiveToAbilitySystemComponent(UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
    Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);

    for (const FHellunaHeroAbilitySet& AbilitySet : HeroStartUpAbilitySets)
    {
        if (!AbilitySet.IsValid()) continue;

        FGameplayAbilitySpec AbilitySpec(AbilitySet.AbilityToGrant);
        AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
        AbilitySpec.Level = ApplyLevel;
        AbilitySpec.DynamicAbilityTags.AddTag(AbilitySet.InputTag);

        InASCToGive->GiveAbility(AbilitySpec);
    }

    bool bHasBlockAbility = false;
    for (const FGameplayAbilitySpec& AbilitySpec : InASCToGive->GetActivatableAbilities())
    {
        if (AbilitySpec.DynamicAbilityTags.HasTagExact(HellunaGameplayTags::InputTag_Block)
            || (AbilitySpec.Ability && AbilitySpec.Ability->IsA<UHeroGameplayAbility_Block>()))
        {
            bHasBlockAbility = true;
            break;
        }
    }

    if (!bHasBlockAbility)
    {
        FGameplayAbilitySpec BlockSpec(UHeroGameplayAbility_Block::StaticClass());
        BlockSpec.SourceObject = InASCToGive->GetAvatarActor();
        BlockSpec.Level = ApplyLevel;
        BlockSpec.DynamicAbilityTags.AddTag(HellunaGameplayTags::InputTag_Block);

        InASCToGive->GiveAbility(BlockSpec);
    }
}
