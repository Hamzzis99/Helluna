// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HellunaEnemyGameplayAbility.h"
#include "Character/HellunaEnemyCharacter.h"
#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "HellunaGameplayTags.h"
#include "Animation/AnimMontage.h"
#include "Sound/SoundBase.h"

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

// ═══════════════════════════════════════════════════════════
// [AttackAssetsV1] GA 소유 Montage/VFX/Sound
// ═══════════════════════════════════════════════════════════

UAnimMontage* UHellunaEnemyGameplayAbility::GetEffectiveAttackMontage() const
{
	if (AttackMontage)
	{
		return AttackMontage;
	}
	if (const_cast<UHellunaEnemyGameplayAbility*>(this)->CurrentActorInfo)
	{
		if (AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(
			const_cast<UHellunaEnemyGameplayAbility*>(this)->CurrentActorInfo->AvatarActor))
		{
			return Enemy->AttackMontage;
		}
	}
	return nullptr;
}

void UHellunaEnemyGameplayAbility::PlayAttackSound()
{
	if (!AttackSound) return;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	Enemy->Multicast_PlayAttackSound(AttackSound, AttackSoundVolumeMultiplier);
}

// ═══════════════════════════════════════════════════════════
// [HitSoundV1] 타격 사운드 캐싱 — 모든 자식 GA 의 Super::ActivateAbility 경로에서 자동 수행
// ═══════════════════════════════════════════════════════════

void UHellunaEnemyGameplayAbility::CacheHitSound()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	Enemy->SetCachedHitSound(HitSound, HitSoundVolumeMultiplier, HitSoundAttenuation);
}

void UHellunaEnemyGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// HitSound 를 매 발동마다 캐릭터에 캐싱.
	// Why: 같은 몬스터가 여러 공격 GA 를 가질 때 직전에 활성화된 GA 의 HitSound 가 사용되도록.
	// Cost: O(1) setter 한 번 — GA 발동당 1회. 오픈월드 프레임 부담 무시 가능.
	CacheHitSound();
}
