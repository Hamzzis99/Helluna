// Fill out your copyright notice in the Description page of Project Settings.


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_SpawnWeapon.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HellunaHeroWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"

#include "EquipmentManagement/Components/Inv_EquipmentComponent.h"
#include "Component/WeaponBridgeComponent.h"

#include "DebugHelper.h"

UHeroGameplayAbility_SpawnWeapon::UHeroGameplayAbility_SpawnWeapon()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;

	BlockAbilitiesWithTag.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Reload")));
	BlockAbilitiesWithTag.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Shoot")));
	BlockAbilitiesWithTag.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.SpawnWeapon")));
	BlockAbilitiesWithTag.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.SpawnWeapon2")));
}

void UHeroGameplayAbility_SpawnWeapon::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Debug::Print(TEXT("[GA_SpawnWeapon] ActivateAbility called"), FColor::Green);

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bEquipEndCalled = false;

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

	// [Fix:dup-callback 2026-05-02] OnCompleted+OnBlendOut 동일 함수 바인딩 시 SetEquipping(false) 이중 호출.
	// OnBlendOut 제거 + AddUniqueDynamic으로 중복 차단.
	EquipTask->OnCompleted.AddUniqueDynamic(this, &UHeroGameplayAbility_SpawnWeapon::OnEquipFinished);
	EquipTask->OnInterrupted.AddUniqueDynamic(this, &UHeroGameplayAbility_SpawnWeapon::OnEquipInterrupted);
	EquipTask->OnCancelled.AddUniqueDynamic(this, &UHeroGameplayAbility_SpawnWeapon::OnEquipInterrupted);

	// [EquipLockOwnershipV1] 여기가 "되돌릴 수 없는 지점" — 모든 early-return 을 통과했고
	//   몽타주가 실제로 시작된다. 이 시점에서만 장착 잠금 ON (장착 중 1/2키 입력 차단).
	//   해제는 EndAbility 에서 항상 수행 → 잠금이 stuck 될 수 없음.
	if (UWeaponBridgeComponent* WeaponBridge = Hero->FindComponentByClass<UWeaponBridgeComponent>())
	{
		WeaponBridge->SetEquipping(true);
	}

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

	// [EquipLockOwnershipV1] 어떤 경로로 끝나든(완료/중단/취소/early-return/사망/언포세스)
	//   장착 잠금을 항상 해제. GAS 는 모든 종료 경로에서 EndAbility 호출을 보장하므로
	//   여기가 유일하고 확실한 reset 지점 → 플래그 stuck 으로 스왑이 영구 차단되던 버그 방지.
	if (AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo())
	{
		if (UWeaponBridgeComponent* WeaponBridge = Hero->FindComponentByClass<UWeaponBridgeComponent>())
		{
			WeaponBridge->SetEquipping(false);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UHeroGameplayAbility_SpawnWeapon::OnEquipFinished()
{
	if (bEquipEndCalled) return;
	bEquipEndCalled = true;

	// ⭐ 장착 애니메이션 완료 → 무기 전환 허용 (잠금 해제는 EndAbility 가 일괄 처리)
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UHeroGameplayAbility_SpawnWeapon::OnEquipInterrupted()
{
	if (bEquipEndCalled) return;
	bEquipEndCalled = true;

	// ⭐ 장착 애니메이션 중단 → 무기 전환 허용 (잠금 해제는 EndAbility 가 일괄 처리)
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}