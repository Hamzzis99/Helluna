// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Run.h"
#include "Character/HellunaHeroCharacter.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HellunaGameplayTags.h"

#include "DebugHelper.h"

void UHeroGameplayAbility_Run::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	AHellunaHeroCharacter* RunHero = GetHeroCharacterFromActorInfo();
	// [MoveSpeedBaseV1] ActiveBaseWalkSpeed 를 RunSpeed 로 세팅 → Hero 가 ActiveBase * MoveSpeedMultiplier 로 재계산.
	RunHero->SetActiveBaseWalkSpeed(RunSpeed);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

}

void UHeroGameplayAbility_Run::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	CleanUp();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

bool UHeroGameplayAbility_Run::IsRun()
{
	return GetAbilitySystemComponentFromActorInfo()
		->HasMatchingGameplayTag(HellunaGameplayTags::Player_status_Run);
}

void UHeroGameplayAbility_Run::CleanUp()
{
	// [MoveSpeedBaseV1] Run 종료 시 ActiveBaseWalkSpeed 를 기본(BaseWalkSpeed) 로 돌려놓음.
	//   Hero 가 MaxWalkSpeed 를 재계산 → 슬로우가 남아 있어도 정확.
	if (AHellunaHeroCharacter* RunHero = GetHeroCharacterFromActorInfo())
	{
		RunHero->SetActiveBaseWalkSpeed(RunHero->GetBaseWalkSpeed());
	}
}