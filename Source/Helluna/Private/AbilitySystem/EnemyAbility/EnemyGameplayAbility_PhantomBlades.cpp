// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_PhantomBlades.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "BossEvent/BossPatternZoneBase.h"
#include "Character/HellunaEnemyCharacter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

#define PB_GA_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[PhantomBlades_GA] " Fmt), ##__VA_ARGS__)

UEnemyGameplayAbility_PhantomBlades::UEnemyGameplayAbility_PhantomBlades()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

bool UEnemyGameplayAbility_PhantomBlades::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
		return false;

	if (CooldownDuration > 0.f && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		const UWorld* World = ActorInfo->AvatarActor->GetWorld();
		if (World && (World->GetTimeSeconds() - LastAbilityEndTime) < CooldownDuration)
			return false;
	}
	return true;
}

void UEnemyGameplayAbility_PhantomBlades::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	PB_GA_LOG("=== ActivateAbility ===");
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	bPatternFinished = false;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(Tag);
	}

	Enemy->LockMovementAndFaceTarget(nullptr);

	if (ChargingVFX)
		Enemy->Multicast_SpawnPersistentVFX(0, ChargingVFX, ChargingVFXScale);

	if (CastMontage)
	{
		UAbilityTask_PlayMontageAndWait* Task =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this, NAME_None, CastMontage, 1.f, NAME_None, false);
		if (Task)
		{
			Task->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_PhantomBlades::OnMontageCompleted);
			Task->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_PhantomBlades::OnMontageCancelled);
			Task->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_PhantomBlades::OnMontageCancelled);
			Task->ReadyForActivation();
		}
	}

	if (PatternZoneClass)
	{
		UWorld* World = Enemy->GetWorld();
		if (World)
		{
			FActorSpawnParameters Params;
			Params.Owner = Enemy;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			SpawnedZone = World->SpawnActor<ABossPatternZoneBase>(
				PatternZoneClass, Enemy->GetActorLocation(), FRotator::ZeroRotator, Params);

			if (SpawnedZone)
			{
				SpawnedZone->SetOwnerEnemy(Enemy);
				SpawnedZone->SetPatternDuration(PatternDuration);
				SpawnedZone->OnPatternFinished.AddDynamic(this, &UEnemyGameplayAbility_PhantomBlades::OnPatternFinished);
			}
		}
	}

	if (UWorld* World = Enemy->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ZoneActivateTimerHandle, this,
			&UEnemyGameplayAbility_PhantomBlades::ActivateZone,
			ZoneActivateDelay, false);
	}
}

void UEnemyGameplayAbility_PhantomBlades::ActivateZone()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy) Enemy->Multicast_StopPersistentVFX(0);
	if (SpawnedZone) SpawnedZone->ActivateZone();
}

void UEnemyGameplayAbility_PhantomBlades::OnPatternFinished(bool bWasBroken)
{
	PB_GA_LOG("OnPatternFinished");
	bPatternFinished = true;
	HandleFinished(false);
}

void UEnemyGameplayAbility_PhantomBlades::OnMontageCompleted()
{
	if (bPatternFinished) HandleFinished(false);
}

void UEnemyGameplayAbility_PhantomBlades::OnMontageCancelled()
{
	if (bPatternFinished) HandleFinished(false);
}

void UEnemyGameplayAbility_PhantomBlades::HandleFinished(bool bWasCancelled)
{
	if (SpawnedZone)
	{
		SpawnedZone->DeactivateZone();
		SpawnedZone->Destroy();
		SpawnedZone = nullptr;
	}
	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo Activation = GetCurrentActivationInfo();
	EndAbility(Handle, Info, Activation, true, bWasCancelled);
}

void UEnemyGameplayAbility_PhantomBlades::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (SpawnedZone)
	{
		SpawnedZone->DeactivateZone();
		SpawnedZone->Destroy();
		SpawnedZone = nullptr;
	}

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		Enemy->UnlockMovement();
		if (UWorld* World = Enemy->GetWorld())
			World->GetTimerManager().ClearTimer(ZoneActivateTimerHandle);
		Enemy->Multicast_StopPersistentVFX(0);
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(Tag);
	}

	bPatternFinished = false;

	if (CooldownDuration > 0.f && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (const UWorld* World = ActorInfo->AvatarActor->GetWorld())
			LastAbilityEndTime = World->GetTimeSeconds();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#undef PB_GA_LOG
