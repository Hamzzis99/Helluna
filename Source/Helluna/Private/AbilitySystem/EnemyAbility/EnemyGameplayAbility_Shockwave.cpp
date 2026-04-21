// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_Shockwave.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "BossEvent/BossPatternZoneBase.h"
#include "Character/HellunaEnemyCharacter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

#define SW_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[Shockwave] " Fmt), ##__VA_ARGS__)

UEnemyGameplayAbility_Shockwave::UEnemyGameplayAbility_Shockwave()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

// ─────────────────────────────────────────────────────────────
// CanActivateAbility — 쿨타임 체크
// ─────────────────────────────────────────────────────────────
bool UEnemyGameplayAbility_Shockwave::CanActivateAbility(
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
				SW_LOG("Cooldown active: %.1f / %.1f", Elapsed, CooldownDuration);
				return false;
			}
		}
	}

	return true;
}

// ─────────────────────────────────────────────────────────────
// ActivateAbility
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_Shockwave::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	SW_LOG("=== ActivateAbility START ===");

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bPatternFinished = false;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		SW_LOG("FAIL: Enemy is null");
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 공격 중 상태 태그
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(AttackingTag);
	}

	// 이동 잠금
	Enemy->LockMovementAndFaceTarget(nullptr);

	// 시전 준비 VFX
	if (ChargingVFX)
	{
		Enemy->Multicast_SpawnPersistentVFX(0, ChargingVFX, ChargingVFXScale);
	}

	// 몽타주 재생
	if (CastMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this, NAME_None, CastMontage, 1.f, NAME_None, false
			);

		if (MontageTask)
		{
			MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_Shockwave::OnMontageCompleted);
			MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_Shockwave::OnMontageCancelled);
			MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_Shockwave::OnMontageCancelled);
			MontageTask->ReadyForActivation();
		}
	}

	// Zone 스폰 (비활성 상태)
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
				SpawnedZone->OnPatternFinished.AddDynamic(this, &UEnemyGameplayAbility_Shockwave::OnPatternFinished);
				SW_LOG("Zone spawned: %s", *SpawnedZone->GetName());
			}
		}
	}
	else
	{
		SW_LOG("WARNING: PatternZoneClass is null");
	}

	// 딜레이 후 Zone 활성화
	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(
			ZoneActivateTimerHandle,
			this,
			&UEnemyGameplayAbility_Shockwave::ActivateZone,
			ZoneActivateDelay,
			false
		);
	}

	SW_LOG("=== ActivateAbility END ===");
}

// ─────────────────────────────────────────────────────────────
// ActivateZone — 딜레이 후 Zone 활성화
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_Shockwave::ActivateZone()
{
	SW_LOG("ActivateZone called");

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		Enemy->Multicast_StopPersistentVFX(0);
	}

	if (SpawnedZone)
	{
		SpawnedZone->ActivateZone();
	}
}

// ─────────────────────────────────────────────────────────────
// OnPatternFinished
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_Shockwave::OnPatternFinished(bool bWasBroken)
{
	SW_LOG("OnPatternFinished: bWasBroken=%s", bWasBroken ? TEXT("TRUE") : TEXT("FALSE"));
	bPatternFinished = true;
	HandleFinished(false);
}

// ─────────────────────────────────────────────────────────────
// 몽타주 콜백
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_Shockwave::OnMontageCompleted()
{
	SW_LOG("OnMontageCompleted: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));

	if (bPatternFinished)
	{
		HandleFinished(false);
	}
}

void UEnemyGameplayAbility_Shockwave::OnMontageCancelled()
{
	SW_LOG("OnMontageCancelled: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));

	if (bPatternFinished)
	{
		HandleFinished(false);
	}
}

// ─────────────────────────────────────────────────────────────
// HandleFinished
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_Shockwave::HandleFinished(bool bWasCancelled)
{
	SW_LOG("=== HandleFinished (bCancelled=%s) ===", bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

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

// ─────────────────────────────────────────────────────────────
// EndAbility
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_Shockwave::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	SW_LOG("=== EndAbility (bCancelled=%s) ===", bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

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

		Enemy->Multicast_StopPersistentVFX(0);
	}

	// State.Enemy.Attacking 태그 제거
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(AttackingTag);
	}

	bPatternFinished = false;

	if (CooldownDuration > 0.f && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (const UWorld* World = ActorInfo->AvatarActor->GetWorld())
		{
			LastAbilityEndTime = World->GetTimeSeconds();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	SW_LOG("=== EndAbility COMPLETE (Cooldown=%.1fs) ===", CooldownDuration);
}

#undef SW_LOG
