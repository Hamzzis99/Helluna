// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_SpawnWeapon.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "Components/SkeletalMeshComponent.h"


#include "DebugHelper.h"

void UHeroGameplayAbility_SpawnWeapon::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{

	SpawnWeapon();

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

}

void UHeroGameplayAbility_SpawnWeapon::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);

}

void UHeroGameplayAbility_SpawnWeapon::SpawnWeapon()
{

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();

	USkeletalMeshComponent* Mesh = Hero->GetMesh();

	const FTransform SocketTM = Mesh->GetSocketTransform(AttachSocketName);

	FActorSpawnParameters Params;
	Params.Owner = Hero;
	Params.Instigator = Hero;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHellunaHeroWeapon* NewWeapon = Hero->GetWorld()->SpawnActor<AHellunaHeroWeapon>(WeaponClass, SocketTM, Params);
	if (NewWeapon)
	{
		NewWeapon->AttachToComponent(
			Mesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			AttachSocketName
		);
		Hero->SetCurrentWeapon(NewWeapon);
	}
}