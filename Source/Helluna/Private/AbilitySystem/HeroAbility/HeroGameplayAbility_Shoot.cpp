// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Shoot.h"
#include "AbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HeroWeapon_GunBase.h"

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

	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	

	if (!Weapon->CanFire())
	{
		// (선택) 0일 때는 자동으로 장전 유도 UI만 보이게 하고 싶다면 여기서 끝.
		Debug::Print(TEXT("No Mag"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	const float Interval = FMath::Max(Weapon->AttackSpeed, 0.01f);

	// =========================================================
	// [MOD] ✅ 발사 간격(=연사 제한) : FireMode 상관없이 무조건 적용
	// =========================================================
	if (!Weapon->CanFireByRate(Now, Interval))
	{
		// 너무 자주 클릭했거나(단발/연발 공통), 타이머가 아주 미세하게 빨리 불린 경우
		return;
	}
	Weapon->ConsumeFireByRate(Now, Interval);

	if (Weapon->FireMode == EWeaponFireMode::SemiAuto) // 단발일 떄는 한번 발사하고 종료
	{
		Shoot();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	// 연발일 때는 타이머로 자동 발사 시작
	Shoot();

	//const float Interval = Weapon->AttackSpeed;
	if (World)
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

	AHeroWeapon_GunBase* Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon()); if (!Weapon) { Debug::Print(TEXT("Shoot Failed: No Weapon"), FColor::Red); return; }

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());

	if (!Weapon->CanFire())
	{
		// (선택) 0일 때는 자동으로 장전 유도 UI만 보이게 하고 싶다면 여기서 끝.
		return;
	}

	if (UAnimMontage* AttackMontage = Weapon->AnimSet.Attack)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->PlayMontage(this, GetCurrentActivationInfo(), AttackMontage, 1.f);
		}
	}

	// 1) 로컬 코스메틱(몽타주/반동)
	if (Hero->IsLocallyControlled())
	{
		//const float PitchKick = Weapon->ReboundUp;
		//const float YawKick = FMath::RandRange(-Weapon->ReboundLeftRight, Weapon->ReboundLeftRight);

		Weapon->ApplyRecoil(Hero);

	}

	// 2) ✅ 데미지/히트판정은 “권한 실행”에서만
	const bool bAuthorityExecution =
		(GetCurrentActivationInfo().ActivationMode == EGameplayAbilityActivationMode::Authority);

	if (bAuthorityExecution)
	{
		if (AController* Controller = Hero->GetController())
		{
			Weapon->Fire(Controller);  // 여기서 ApplyPointDamage + MulticastFireFX

			Debug::Print(FString::Printf(
				TEXT("weapon name : %s, mag : %d/%d"),
				*GetNameSafe(Weapon),
				Weapon->CurrentMag,
				Weapon->MaxMag
			));
		}
	}

}

