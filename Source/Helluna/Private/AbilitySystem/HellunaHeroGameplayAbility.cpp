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

	// [Downed] 다운 상태: bIgnoreParryBlock과 무관하게 모든 GA 차단
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (UHellunaFunctionLibrary::NativeDoesActorHaveTag(
			ActorInfo->AvatarActor.Get(), HellunaGameplayTags::Player_State_Downed))
		{
			return false;
		}

		// [MenuInputLockV1] UI 메뉴(우주선 수리/회복 등) 열림 중엔 전투 GA(발사/조준/장전 등) 차단.
		//   클릭이 발사로 새지 않게 하기 위함. 메뉴 토글 GA(Repair/InRepair)는 bIgnoreMenuLock=true 로 예외.
		if (!bIgnoreMenuLock && UHellunaFunctionLibrary::NativeDoesActorHaveTag(
			ActorInfo->AvatarActor.Get(), HellunaGameplayTags::Player_State_MenuOpen))
		{
			return false;
		}
	}

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
