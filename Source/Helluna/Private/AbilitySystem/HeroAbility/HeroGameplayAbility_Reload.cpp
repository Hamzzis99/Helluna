// Capstone Project Helluna


#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Reload.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Character/HellunaHeroCharacter.h"
#include "Weapon/HeroWeapon_GunBase.h"

#include "DebugHelper.h"

UHeroGameplayAbility_Reload::UHeroGameplayAbility_Reload()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// R키는 보통 한번 누르는 Trigger
	InputActionPolicy = EHellunaInputActionPolicy::Trigger;
}

void UHeroGameplayAbility_Reload::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 총기만 리로드 가능
	Weapon = Cast<AHeroWeapon_GunBase>(Hero->GetCurrentWeapon());
	if (!Weapon)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 이미 풀탄이면 리로드 불필요
	if (!Weapon->CanReload())
	{
		Debug::Print(TEXT("[GA_Reload] Already Full"), FColor::Red);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 총기 BP에 세팅한 장전 몽타주 가져오기
	UAnimMontage* Montage = Weapon->GetAnimSet().Reload;

	// 몽타주가 없으면: 그냥 즉시 장전 처리하고 종료(그래도 시스템은 동작)
	if (!Montage)
	{
		Weapon->Reload();
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 로컬에서만 애니 재생(코스메틱)
	if (!ActorInfo || !ActorInfo->IsLocallyControlled())
	{
		// 로컬이 아니면 애니는 생략하고 종료(서버에서 탄약 처리는 별도 흐름에서 하거나, 여기서 Reload() 호출하고 끝내도 됨)
		// 보통은 로컬 입력 기반이라 여기까지 올 일이 적음.
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	Hero->Server_RequestPlayMontageExceptOwner(Montage);

	ReloadTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, 1.f);

	if (!ReloadTask)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ReloadTask->OnCompleted.AddDynamic(this, &ThisClass::OnReloadFinished);
	ReloadTask->OnBlendOut.AddDynamic(this, &ThisClass::OnReloadFinished);
	ReloadTask->OnInterrupted.AddDynamic(this, &ThisClass::OnReloadInterrupted);
	ReloadTask->OnCancelled.AddDynamic(this, &ThisClass::OnReloadInterrupted);

	ReloadTask->ReadyForActivation();
}

void UHeroGameplayAbility_Reload::OnReloadFinished()
{
	// 서버에서 최종 반영되도록 Gun->Reload() 내부가 Authority/RPC 처리
	if (Weapon)
	{
		Weapon->Reload();
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UHeroGameplayAbility_Reload::OnReloadInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UHeroGameplayAbility_Reload::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ReloadTask = nullptr;
	Weapon = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}