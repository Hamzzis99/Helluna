// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_SpawnWeapon.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"

#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"

#include "DebugHelper.h"

UHeroGameplayAbility_SpawnWeapon::UHeroGameplayAbility_SpawnWeapon()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
}

void UHeroGameplayAbility_SpawnWeapon::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Debug::Print(TEXT("[GA_SpawnWeapon] ActivateAbility called"), FColor::Green);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!ActorInfo || !ActorInfo->AvatarActor.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ✅ 1) 무기 클래스 유효성 (서버 스폰 요청에 필요)
	if (!WeaponClass)
	{
		Debug::Print(TEXT("[GA_SpawnWeapon] WeaponClass is null"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ✅ 2) 애니 재생은 로컬에서만
	if (!ActorInfo->IsLocallyControlled())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 캐릭터 애님 인스턴스 체크
	USkeletalMeshComponent* CharacterMesh = Hero->GetMesh();
	if (!CharacterMesh || !CharacterMesh->GetAnimInstance())
	{
		Debug::Print(TEXT("[GA_SpawnWeapon] AnimInstance null"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	FName SocketToAttach = NAME_None;

	// ✅ 4) 무기 애니셋에서 Equip 몽타주, 소켓 가져오기
	UAnimMontage* EquipMontage = nullptr;
	if (const AHellunaHeroWeapon* WeaponCDO = WeaponClass->GetDefaultObject<AHellunaHeroWeapon>())
	{
		EquipMontage = WeaponCDO->GetAnimSet().Equip;
		SocketToAttach = WeaponCDO->GetEquipSocketName();
	}

	if (!EquipMontage)
	{
		Debug::Print(TEXT("[GA_SpawnWeapon] EquipMontage null (check weapon AnimSet)"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ✅ 5) 소켓 이름이 비어있으면 GA에 설정된 기본 소켓 사용
	if (SocketToAttach.IsNone())
	{
		Debug::Print(TEXT("[GA_SpawnWeapon] Weapon socket is Clear (check weapon EquipSocketName or GA AttachSocketName)"), FColor::Red);
		SocketToAttach = AttachSocketName;  // ✅ 추가 (원래 GA에 있던 소켓)
	}

	// 2) 서버에게: 스폰 + "다른 클라에게만" 애니 재생시키기
	if (SocketToAttach.IsNone())
	{
		Debug::Print(TEXT("[GA_SpawnWeapon] Attach socket is None (check weapon EquipSocketName or GA AttachSocketName)"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	

	if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
	{
		if (UInv_EquipmentComponent* EquipComp = PC->FindComponentByClass<UInv_EquipmentComponent>())
		{
			EquipComp->ActiveUnequipWeapon();
		}
	}


	// 2) 서버에게: 스폰 + "다른 클라에게만" 애니 재생시키기
	Hero->Server_RequestSpawnWeapon(WeaponClass, SocketToAttach, EquipMontage);

	// 3) 로컬에서 몽타주 재생 + Wait (장착중 발사, 점프 등 다른 어빌리티 사용의 방지 목적)
	EquipTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, EquipMontage, 1.f);

	if (!EquipTask)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	EquipTask->OnCompleted.AddDynamic(this, &UHeroGameplayAbility_SpawnWeapon::OnEquipFinished);
	EquipTask->OnBlendOut.AddDynamic(this, &UHeroGameplayAbility_SpawnWeapon::OnEquipFinished);
	EquipTask->OnInterrupted.AddDynamic(this, &UHeroGameplayAbility_SpawnWeapon::OnEquipInterrupted);
	EquipTask->OnCancelled.AddDynamic(this, &UHeroGameplayAbility_SpawnWeapon::OnEquipInterrupted);

	EquipTask->ReadyForActivation();
}

void UHeroGameplayAbility_SpawnWeapon::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	EquipTask = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_SpawnWeapon::OnEquipFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UHeroGameplayAbility_SpawnWeapon::OnEquipInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}