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

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_Shoot::Shoot()
{

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	AHellunaHeroWeapon* Weapon = Hero->GetCurrentWeapon();
	if (!Weapon) {
		Debug::Print(TEXT("Shoot Failed: No Weapon"), FColor::Red);
		return;
	}

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

	// 2) 실제 발사 로직
	if (AController* Controller = Hero->GetController())
	{
		Weapon->Fire(Controller);
	}



	// 3) 반동
	const float PitchKick = Weapon->ReboundUp;
	const float YawKick = FMath::RandRange(-Weapon->ReboundLeftRight, Weapon->ReboundLeftRight);

	Character->AddControllerPitchInput(-PitchKick);
	Character->AddControllerYawInput(YawKick);
}
