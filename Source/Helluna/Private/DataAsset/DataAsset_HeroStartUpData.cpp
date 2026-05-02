// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAsset/DataAsset_HeroStartUpData.h"
#include "AbilitySystem/HellunaGameplayAbility.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogHellunaHeroStartUpData, Log, All);

UDataAsset_HeroStartUpData::UDataAsset_HeroStartUpData()
{
    BlockAbilityToGrant = TSoftClassPtr<UHellunaGameplayAbility>(
        FSoftObjectPath(TEXT("/Game/Hero/HeroGameplayAbility/GA_Hero_Block.GA_Hero_Block_C")));
}

bool FHellunaHeroAbilitySet::IsValid() const
{
    return InputTag.IsValid() && AbilityToGrant;
}

void UDataAsset_HeroStartUpData::GiveToAbilitySystemComponent(UHellunaAbilitySystemComponent* InASCToGive, int32 ApplyLevel)
{
    if (!IsValid(InASCToGive))
    {
        UE_LOG(LogHellunaHeroStartUpData, Warning, TEXT("GiveToAbilitySystemComponent failed: ASC is invalid. DataAsset=%s"), *GetNameSafe(this));
        return;
    }

    Super::GiveToAbilitySystemComponent(InASCToGive, ApplyLevel);

    bool bHasConfiguredBlockAbility = false;

    for (const FHellunaHeroAbilitySet& AbilitySet : HeroStartUpAbilitySets)
    {
        if (!AbilitySet.IsValid())
        {
            if (AbilitySet.InputTag.MatchesTagExact(HellunaGameplayTags::InputTag_Block))
            {
                UE_LOG(LogHellunaHeroStartUpData, Warning, TEXT("InputTag.Block is configured but AbilityToGrant is invalid. DataAsset=%s"), *GetNameSafe(this));
            }

            continue;
        }

        if (AbilitySet.InputTag.MatchesTagExact(HellunaGameplayTags::InputTag_Block))
        {
            bHasConfiguredBlockAbility = true;
        }

        FGameplayAbilitySpec AbilitySpec(AbilitySet.AbilityToGrant);
        AbilitySpec.SourceObject = InASCToGive->GetAvatarActor();
        AbilitySpec.Level = ApplyLevel;
        AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilitySet.InputTag);

        InASCToGive->GiveAbility(AbilitySpec);
    }

    if (!bHasConfiguredBlockAbility)
    {
        TSubclassOf<UHellunaGameplayAbility> LoadedBlockAbility = BlockAbilityToGrant.LoadSynchronous();
        if (LoadedBlockAbility)
        {
            FGameplayAbilitySpec BlockSpec(LoadedBlockAbility);
            BlockSpec.SourceObject = InASCToGive->GetAvatarActor();
            BlockSpec.Level = ApplyLevel;
            BlockSpec.GetDynamicSpecSourceTags().AddTag(HellunaGameplayTags::InputTag_Block);

            InASCToGive->GiveAbility(BlockSpec);
            bHasConfiguredBlockAbility = true;
        }
        else if (!BlockAbilityToGrant.IsNull())
        {
            UE_LOG(LogHellunaHeroStartUpData, Warning, TEXT("Failed to load BlockAbilityToGrant. Path=%s DataAsset=%s"), *BlockAbilityToGrant.ToSoftObjectPath().ToString(), *GetNameSafe(this));
        }
    }

    if (!bHasConfiguredBlockAbility)
    {
        UE_LOG(LogHellunaHeroStartUpData, Warning, TEXT("InputTag.Block ability is not configured. Register a Blueprint ability derived from UHeroGameplayAbility_Block in HeroStartUpAbilitySets or BlockAbilityToGrant. DataAsset=%s"), *GetNameSafe(this));
    }
}
