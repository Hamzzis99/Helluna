// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_TimeDistortion.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "BossEvent/BossPatternZoneBase.h"
#include "Character/HellunaEnemyCharacter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

#define TD_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[TimeDistortion] " Fmt), ##__VA_ARGS__)

UEnemyGameplayAbility_TimeDistortion::UEnemyGameplayAbility_TimeDistortion()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

// ─────────────────────────────────────────────────────────────
// CanActivateAbility — 쿨타임 체크
// ─────────────────────────────────────────────────────────────
bool UEnemyGameplayAbility_TimeDistortion::CanActivateAbility(
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
				TD_LOG("Cooldown active: %.1f / %.1f", Elapsed, CooldownDuration);
				return false;
			}
		}
	}

	return true;
}

// ─────────────────────────────────────────────────────────────
// ActivateAbility
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	TD_LOG("=== ActivateAbility START ===");

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bPatternFinished = false;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		TD_LOG("FAIL: Enemy is null");
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
			MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_TimeDistortion::OnMontageCompleted);
			MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_TimeDistortion::OnMontageCancelled);
			MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_TimeDistortion::OnMontageCancelled);
			MontageTask->ReadyForActivation();
		}
	}

	// Zone 스폰 (비활성 상태로)
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
				SpawnedZone->OnPatternFinished.AddDynamic(this, &UEnemyGameplayAbility_TimeDistortion::OnPatternFinished);
				TD_LOG("Zone spawned: %s, Duration=%.2f", *SpawnedZone->GetName(), PatternDuration);
			}
		}
	}
	else
	{
		TD_LOG("WARNING: PatternZoneClass is null");
	}

	// 딜레이 후 Zone 활성화
	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(
			ZoneActivateTimerHandle,
			this,
			&UEnemyGameplayAbility_TimeDistortion::ActivateZone,
			ZoneActivateDelay,
			false
		);
	}

	TD_LOG("=== ActivateAbility END ===");
}

// ─────────────────────────────────────────────────────────────
// ActivateZone — 딜레이 후 Zone 활성화
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::ActivateZone()
{
	TD_LOG("ActivateZone called");

	// 시전 준비 VFX 끄기
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
// OnPatternFinished — Zone에서 패턴 종료 알림
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::OnPatternFinished(bool bWasBroken)
{
	TD_LOG("OnPatternFinished: bWasBroken=%s", bWasBroken ? TEXT("TRUE") : TEXT("FALSE"));
	bPatternFinished = true;
	HandleFinished(false);
}

// ─────────────────────────────────────────────────────────────
// 몽타주 콜백
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::OnMontageCompleted()
{
	TD_LOG("OnMontageCompleted: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));

	// 몽타주가 끝나도 패턴이 진행 중이면 무조건 대기
	if (bPatternFinished)
	{
		HandleFinished(false);
	}
	// else: 패턴 종료 콜백(OnPatternFinished)이 올 때까지 GA 유지
}

void UEnemyGameplayAbility_TimeDistortion::OnMontageCancelled()
{
	TD_LOG("OnMontageCancelled: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));

	// 몽타주가 취소되어도 패턴이 진행 중이면 무조건 대기
	if (bPatternFinished)
	{
		HandleFinished(false);
	}
	// else: 패턴 종료 콜백이 올 때까지 GA 유지 — 보스는 이동 잠금 상태 유지
}

// ─────────────────────────────────────────────────────────────
// HandleFinished
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::HandleFinished(bool bWasCancelled)
{
	TD_LOG("=== HandleFinished (bCancelled=%s) ===", bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

	// Zone 정리
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
void UEnemyGameplayAbility_TimeDistortion::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	TD_LOG("=== EndAbility (bCancelled=%s) ===", bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

	// Zone 정리 보장
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

		// 시전 준비 VFX 정리
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

	// 쿨타임 시작 시점 기록
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (const UWorld* World = ActorInfo->AvatarActor->GetWorld())
		{
			LastAbilityEndTime = World->GetTimeSeconds();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	TD_LOG("=== EndAbility COMPLETE (Cooldown=%.1fs) ===", CooldownDuration);
}

#undef TD_LOG
