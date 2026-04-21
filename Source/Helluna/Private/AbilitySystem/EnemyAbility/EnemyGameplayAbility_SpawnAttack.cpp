// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_SpawnAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "Character/HellunaEnemyCharacter.h"
#include "BossEvent/BossPatternZoneBase.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define SA_GA_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[SpawnAttackV1] " Fmt), ##__VA_ARGS__)

UEnemyGameplayAbility_SpawnAttack::UEnemyGameplayAbility_SpawnAttack()
{
	InstancingPolicy   = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UEnemyGameplayAbility_SpawnAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bMontageFinished = false;
	bSpawnTriggered = false;
	bLifetimeExpired = false;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 공격 중 상태 태그 (StateTree 다른 패턴 진입 방지).
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(Tag);
	}

	Enemy->LockMovementAndFaceTarget(nullptr);

	if (CastVFX)
	{
		Enemy->Multicast_SpawnPersistentVFX(0, CastVFX, CastVFXScale);
	}

	// 시전 몬타지 (있을 때만).
	if (CastMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this, NAME_None, CastMontage, 1.f, NAME_None, false);

		if (MontageTask)
		{
			MontageTask->OnCompleted.AddDynamic(this, &UEnemyGameplayAbility_SpawnAttack::OnCastMontageCompleted);
			MontageTask->OnCancelled.AddDynamic(this, &UEnemyGameplayAbility_SpawnAttack::OnCastMontageCancelled);
			MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_SpawnAttack::OnCastMontageCancelled);
			MontageTask->ReadyForActivation();
		}
		else
		{
			bMontageFinished = true;
		}
	}
	else
	{
		bMontageFinished = true;
	}

	// SpawnDelay 후 BP 스폰.
	if (UWorld* World = Enemy->GetWorld())
	{
		const float Delay = FMath::Max(0.f, SpawnDelay);
		if (Delay <= KINDA_SMALL_NUMBER)
		{
			HandleSpawnTimer();
		}
		else
		{
			World->GetTimerManager().SetTimer(
				SpawnDelayTimerHandle, this,
				&UEnemyGameplayAbility_SpawnAttack::HandleSpawnTimer,
				Delay, false);
		}
	}

	SA_GA_LOG("Activate Enemy=%s SpawnedActorClass=%s SpawnDelay=%.2f Lifetime=%.2f VFX=%s VFXScale=%.2f",
		*Enemy->GetName(),
		SpawnedActorClass ? *SpawnedActorClass->GetName() : TEXT("null"),
		SpawnDelay, SpawnedActorLifetime,
		CastVFX ? *CastVFX->GetName() : TEXT("null"),
		CastVFXScale);
}

void UEnemyGameplayAbility_SpawnAttack::HandleSpawnTimer()
{
	if (bSpawnTriggered) return;
	bSpawnTriggered = true;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		HandleAbilityFinished(true);
		return;
	}

	// 시전 VFX 종료.
	Enemy->Multicast_StopPersistentVFX(0);

	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		HandleAbilityFinished(true);
		return;
	}

	if (!SpawnedActorClass)
	{
		SA_GA_LOG("SpawnedActorClass 미지정 → BP 소환 스킵, 즉시 종료");
		HandleAbilityFinished(false);
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = Enemy;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	SpawnedActor = World->SpawnActor<AActor>(
		SpawnedActorClass,
		Enemy->GetActorLocation(),
		Enemy->GetActorRotation(),
		Params);

	SA_GA_LOG("Spawned Actor=%s Class=%s",
		SpawnedActor ? *SpawnedActor->GetName() : TEXT("null"),
		*SpawnedActorClass->GetName());

	if (!SpawnedActor)
	{
		// 스폰 실패 → 즉시 종료.
		HandleAbilityFinished(false);
		return;
	}

	// [SpawnAttackV1.PatternZone] 스폰된 액터가 BossPatternZoneBase 파생이면
	// Zone 인터페이스로 SetOwnerEnemy + SetPatternDuration + ActivateZone +
	// OnPatternFinished 바인딩까지 자동 처리. PhantomBlades / TimeDistortion 통합 경로.
	if (ABossPatternZoneBase* Zone = Cast<ABossPatternZoneBase>(SpawnedActor))
	{
		Zone->SetOwnerEnemy(Enemy);
		Zone->SetPatternDuration(SpawnedActorLifetime);
		Zone->OnPatternFinished.AddDynamic(this, &UEnemyGameplayAbility_SpawnAttack::OnSpawnedZonePatternFinished);
		Zone->ActivateZone();
		SA_GA_LOG("Spawned actor is BossPatternZone — bound OnPatternFinished, ActivateZone() 호출");
		// Zone 자체가 종료 시점을 결정하므로 별도 Lifetime 타이머는 두지 않음 (Zone 의 PatternDuration 참조).
		return;
	}

	// 일반 AActor — Lifetime 후 자동 정리.
	if (SpawnedActorLifetime > 0.f)
	{
		World->GetTimerManager().SetTimer(
			LifetimeTimerHandle, this,
			&UEnemyGameplayAbility_SpawnAttack::HandleLifetimeExpired,
			SpawnedActorLifetime, false);
	}
	// 라이프타임이 0 이면 GA 가 무한 유지 — 외부에서 EndAbility/Cancel 필요. (디자이너 의도)
}

void UEnemyGameplayAbility_SpawnAttack::OnSpawnedZonePatternFinished(bool bWasBroken)
{
	SA_GA_LOG("OnSpawnedZonePatternFinished bWasBroken=%d", bWasBroken ? 1 : 0);
	HandleAbilityFinished(false);
}

void UEnemyGameplayAbility_SpawnAttack::HandleLifetimeExpired()
{
	bLifetimeExpired = true;
	SA_GA_LOG("Lifetime expired");
	HandleAbilityFinished(false);
}

void UEnemyGameplayAbility_SpawnAttack::OnCastMontageCompleted()
{
	bMontageFinished = true;
}

void UEnemyGameplayAbility_SpawnAttack::OnCastMontageCancelled()
{
	bMontageFinished = true;
}

void UEnemyGameplayAbility_SpawnAttack::HandleAbilityFinished(bool bWasCancelled)
{
	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo Activation = GetCurrentActivationInfo();
	EndAbility(Handle, Info, Activation, true, bWasCancelled);
}

void UEnemyGameplayAbility_SpawnAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 소환된 액터 정리. BossPatternZone 이면 DeactivateZone 으로 cleanup 기회 부여.
	if (SpawnedActor)
	{
		if (ABossPatternZoneBase* Zone = Cast<ABossPatternZoneBase>(SpawnedActor))
		{
			Zone->OnPatternFinished.RemoveDynamic(this, &UEnemyGameplayAbility_SpawnAttack::OnSpawnedZonePatternFinished);
			Zone->DeactivateZone();
		}
		SpawnedActor->Destroy();
		SpawnedActor = nullptr;
	}

	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		Enemy->UnlockMovement();
		Enemy->Multicast_StopPersistentVFX(0);

		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(SpawnDelayTimerHandle);
			World->GetTimerManager().ClearTimer(LifetimeTimerHandle);
		}
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(Tag);
	}

	bMontageFinished = false;
	bSpawnTriggered = false;
	bLifetimeExpired = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#undef SA_GA_LOG
