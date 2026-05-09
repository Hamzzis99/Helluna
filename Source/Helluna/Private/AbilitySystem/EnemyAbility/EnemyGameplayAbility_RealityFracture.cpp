// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RealityFracture.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "BossEvent/BossPatternZoneBase.h"
#include "Character/HellunaEnemyCharacter.h"

#define RF_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[RealityFracture] " Fmt), ##__VA_ARGS__)

UEnemyGameplayAbility_RealityFracture::UEnemyGameplayAbility_RealityFracture()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

bool UEnemyGameplayAbility_RealityFracture::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	if (CooldownDuration > 0.f && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		const UWorld* World = ActorInfo->AvatarActor->GetWorld();
		if (World)
		{
			const double Elapsed = World->GetTimeSeconds() - LastAbilityEndTime;
			if (Elapsed < CooldownDuration)
			{
				RF_LOG("Cooldown active: %.1f / %.1f", Elapsed, CooldownDuration);
				return false;
			}
		}
	}

	return true;
}

void UEnemyGameplayAbility_RealityFracture::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	RF_LOG("=== ActivateAbility START ===");

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bPatternFinished = false;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		RF_LOG("FAIL: Enemy is null");
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(AttackingTag);
	}

	Enemy->LockMovementAndFaceTarget(nullptr);

	if (CastMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this, NAME_None, CastMontage, 1.f, NAME_None, false
			);
		if (MontageTask)
		{
			MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_RealityFracture::OnMontageCompleted);
			MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_RealityFracture::OnMontageCancelled);
			MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_RealityFracture::OnMontageCancelled);
			MontageTask->ReadyForActivation();
		}
	}

	if (PatternZoneClass)
	{
		UWorld* World = Enemy->GetWorld();
		if (World)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = Enemy;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			SpawnedZone = World->SpawnActor<ABossPatternZoneBase>(
				PatternZoneClass,
				Enemy->GetActorLocation(),
				FRotator::ZeroRotator,
				SpawnParams
			);

			if (SpawnedZone)
			{
				SpawnedZone->SetOwnerEnemy(Enemy);
				SpawnedZone->SetPatternDuration(PatternDuration);
				SpawnedZone->OnPatternFinished.AddDynamic(this, &UEnemyGameplayAbility_RealityFracture::OnPatternFinished);
				RF_LOG("Zone spawned: %s, Duration=%.2f", *SpawnedZone->GetName(), PatternDuration);
			}
		}
	}
	else
	{
		RF_LOG("WARNING: PatternZoneClass is null");
	}

	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		const float DelayTime = CastMontage ? ZoneActivateDelay : 0.f;
		if (DelayTime > 0.f)
		{
			World->GetTimerManager().SetTimer(
				ZoneActivateTimerHandle,
				this,
				&UEnemyGameplayAbility_RealityFracture::ActivateZone,
				DelayTime,
				false
			);
		}
		else
		{
			ActivateZone();
		}
	}

	RF_LOG("=== ActivateAbility END ===");
}

void UEnemyGameplayAbility_RealityFracture::ActivateZone()
{
	RF_LOG("ActivateZone called");
	if (SpawnedZone)
	{
		SpawnedZone->ActivateZone();
	}
}

void UEnemyGameplayAbility_RealityFracture::OnPatternFinished(bool bWasBroken)
{
	RF_LOG("OnPatternFinished: bWasBroken=%s", bWasBroken ? TEXT("TRUE") : TEXT("FALSE"));
	bPatternFinished = true;
	HandleFinished(false);
}

void UEnemyGameplayAbility_RealityFracture::OnMontageCompleted()
{
	RF_LOG("OnMontageCompleted: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));
	if (bPatternFinished)
	{
		HandleFinished(false);
	}
}

void UEnemyGameplayAbility_RealityFracture::OnMontageCancelled()
{
	RF_LOG("OnMontageCancelled: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));
	if (bPatternFinished)
	{
		HandleFinished(false);
	}
}

void UEnemyGameplayAbility_RealityFracture::HandleFinished(bool bWasCancelled)
{
	RF_LOG("=== HandleFinished (bCancelled=%s) ===", bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

	if (SpawnedZone)
	{
		SpawnedZone->DeactivateZone();
		SpawnedZone->Destroy();
		SpawnedZone = nullptr;
	}

	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, bWasCancelled);
}

void UEnemyGameplayAbility_RealityFracture::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	RF_LOG("=== EndAbility (bCancelled=%s) ===", bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

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
		{
			World->GetTimerManager().ClearTimer(ZoneActivateTimerHandle);
		}
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(AttackingTag);
	}

	bPatternFinished = false;

	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (const UWorld* World = ActorInfo->AvatarActor->GetWorld())
		{
			LastAbilityEndTime = World->GetTimeSeconds();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	RF_LOG("=== EndAbility COMPLETE (Cooldown=%.1fs) ===", CooldownDuration);
}

#undef RF_LOG
