// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaHeroGameplayAbility.h"
#include "Character/HellunaHeroCharacter.h"
#include "Controller/HellunaHeroController.h"
#include "HellunaGameplayTags.h"
#include "HellunaFunctionLibrary.h"

bool UHellunaHeroGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
		return false;

	// 패링/킥 중 다른 GA 차단 (GunParry/MeleeKick 자신은 bIgnoreParryBlock=true로 스킵)
	if (!bIgnoreParryBlock && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		AActor* Avatar = ActorInfo->AvatarActor.Get();
		if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(Avatar, HellunaGameplayTags::Player_State_ParryExecution)
			|| UHellunaFunctionLibrary::NativeDoesActorHaveTag(Avatar, HellunaGameplayTags::Hero_State_Kicking)
			|| UHellunaFunctionLibrary::NativeDoesActorHaveTag(Avatar, HellunaGameplayTags::Player_State_PostParryInvincible))
		{
			return false;
		}

		// 카메라 복귀 중 차단 (패링 후 FOV/ArmLength 복귀 완료까지)
		if (const AHellunaHeroCharacter* HeroChar = Cast<AHellunaHeroCharacter>(Avatar))
		{
			if (HeroChar->bParryCameraReturning)
			{
				return false;
			}
		}
	}

	return true;
}

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
