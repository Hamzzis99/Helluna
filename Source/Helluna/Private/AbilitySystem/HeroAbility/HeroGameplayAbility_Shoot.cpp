// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Shoot.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HellunaHeroWeapon.h"

#include "DebugHelper.h"


UHeroGameplayAbility_Shoot::UHeroGameplayAbility_Shoot()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// ✅ 네 ASC Release 로직이 이걸 보고 Cancel 해줌
	InputActionPolicy = EHellunaInputActionPolicy::Hold;
}

void UHeroGameplayAbility_Shoot::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHellunaHeroWeapon* Weapon = Hero->GetCurrentWeapon();
	if (!Weapon)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (Weapon->FireMode == EWeaponFireMode::SemiAuto) // 단발일 떄는 한번 발사하고 종료
	{
		Shoot();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	// 연발일 때는 타이머로 자동 발사 시작
	Shoot();

	const float Interval = Weapon->AttackSpeed;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AutoFireTimerHandle,
			this,
			&ThisClass::Shoot,
			Interval,
			true
		);
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

}

void UHeroGameplayAbility_Shoot::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoFireTimerHandle);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_Shoot::Shoot()
{

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero) return;

	AHellunaHeroWeapon* Weapon = Hero->GetCurrentWeapon(); if (!Weapon) { Debug::Print(TEXT("Shoot Failed: No Weapon"), FColor::Red); return; }

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());

	if (UAnimMontage* AttackMontage = Weapon->AnimSet.Attack)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			// ✅ 자동연사에서 매 발마다 몽타주가 "다시 시작"되는 걸 막고 싶으면 이 체크 유지
			bool bShouldPlay = true;

			if (USkeletalMeshComponent* Mesh = Character->GetMesh())
			{
				if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
				{
					bShouldPlay = !AnimInst->Montage_IsPlaying(AttackMontage);
				}
			}

			if (bShouldPlay)
			{
				ASC->PlayMontage(this, GetCurrentActivationInfo(), AttackMontage, 1.0f);
			}
		}
	}

	// 1) 로컬 코스메틱(몽타주/반동)
	if (Hero->IsLocallyControlled())
	{
		const float PitchKick = Weapon->ReboundUp;
		const float YawKick = FMath::RandRange(-Weapon->ReboundLeftRight, Weapon->ReboundLeftRight);

		Character->AddControllerPitchInput(-PitchKick);
		Character->AddControllerYawInput(YawKick);
	}

	// 2) ✅ 데미지/히트판정은 “권한 실행”에서만
	const bool bAuthorityExecution =
		(GetCurrentActivationInfo().ActivationMode == EGameplayAbilityActivationMode::Authority);

	if (bAuthorityExecution)
	{
		if (AController* Controller = Hero->GetController())
		{
			Weapon->Fire(Controller);  // 여기서 ApplyPointDamage + MulticastFireFX
		}
	}

}
