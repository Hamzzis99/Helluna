// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"

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

// ═══════════════════════════════════════════════════════════
// 건패링 윈도우 헬퍼
// ═══════════════════════════════════════════════════════════

void UHellunaEnemyGameplayAbility::TryOpenParryWindow()
{
	if (!bOpensParryWindow) return;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy || !Enemy->bCanBeParried) return;

	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	if (!ASC) return;

	ASC->AddStateTag(HellunaGameplayTags::Enemy_Ability_Parryable);

	if (UWorld* World = Enemy->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ParryWindowTimerHandle,
			[this]() { CloseParryWindow(); },
			ParryWindowDuration,
			false
		);
	}
}

void UHellunaEnemyGameplayAbility::CloseParryWindow()
{
	UHellunaAbilitySystemComponent* ASC = Cast<UHellunaAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
	if (ASC)
	{
		ASC->RemoveStateTag(HellunaGameplayTags::Enemy_Ability_Parryable);
	}

	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(ParryWindowTimerHandle);
		}
	}
}
