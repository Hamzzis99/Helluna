// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RealityFracture.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "BossEvent/BossPatternZoneBase.h"
#include "BossEvent/StasisSalvoOrb.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

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

	// [TDEndSyncV1] 몽타주 종료 + 패턴 종료 둘 다 충족돼야 GA 종료.
	bPatternFinished = false;
	bMontageFinished = false;

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
		else
		{
			bMontageFinished = true; // 태스크 생성 실패 — 몽타주 조건 즉시 충족
		}
	}
	else
	{
		bMontageFinished = true; // 몽타주 없음 → 즉시 충족
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
				&UEnemyGameplayAbility_RealityFracture::LaunchOrbOrActivateZone,
				DelayTime,
				false
			);
		}
		else
		{
			LaunchOrbOrActivateZone();
		}
	}

	RF_LOG("=== ActivateAbility END ===");
}

// ─────────────────────────────────────────────────────────────
// [StasisSalvoV2] 구체 발사 (또는 OrbClass 없으면 곧장 Zone 활성화)
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_RealityFracture::LaunchOrbOrActivateZone()
{
	RF_LOG("LaunchOrbOrActivateZone — SpawnedZone=%s, OrbClass=%s",
		*GetNameSafe(SpawnedZone), *GetNameSafe(OrbClass));

	if (!SpawnedZone)
	{
		RF_LOG("LaunchOrb — SpawnedZone null, abort");
		return;
	}

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	UWorld* World = Enemy ? Enemy->GetWorld() : nullptr;

	if (!OrbClass || !Enemy || !World)
	{
		// 구버전 동작 — 구체 없이 곧장 존 활성화.
		RF_LOG("LaunchOrb — OrbClass/Enemy/World 없음 → 곧장 ActivateZone (OrbClass=%s)", *GetNameSafe(OrbClass));
		SpawnedZone->ActivateZone();
		return;
	}

	const FVector LaunchLoc = Enemy->GetActorLocation()
		+ Enemy->GetActorForwardVector() * OrbLaunchForwardOffset
		+ FVector(0.f, 0.f, OrbLaunchHeightOffset);

	// 방향 = 가장 가까운 플레이어 폰 향해. 없으면 보스 forward.
	FVector Dir = Enemy->GetActorForwardVector();
	{
		APawn* Best = nullptr;
		float BestSq = TNumericLimits<float>::Max();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!PC) continue;
			APawn* P = PC->GetPawn();
			if (!P) continue;
			const float D = FVector::DistSquared(P->GetActorLocation(), LaunchLoc);
			if (D < BestSq) { BestSq = D; Best = P; }
		}
		if (Best)
		{
			const FVector ToTarget = Best->GetActorLocation() - LaunchLoc;
			if (!ToTarget.IsNearlyZero()) { Dir = ToTarget.GetSafeNormal(); }
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Enemy;
	SpawnParams.Instigator = Enemy;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStasisSalvoOrb* Orb = World->SpawnActor<AStasisSalvoOrb>(OrbClass, LaunchLoc, Dir.Rotation(), SpawnParams);
	if (Orb)
	{
		Orb->Init(Enemy, SpawnedZone, Dir);
		RF_LOG("LaunchOrb — %s launched from %s, dir=%s", *Orb->GetName(), *LaunchLoc.ToString(), *Dir.ToString());
	}
	else
	{
		RF_LOG("LaunchOrb — orb spawn 실패 → 곧장 ActivateZone");
		SpawnedZone->ActivateZone();
	}
}

// [StasisSalvoV2] GA 종료조건 = "패턴(Zone) 종료" 하나. 보스는 ActivateAbility 의 LockMovementAndFaceTarget
//   부터 EndAbility 의 UnlockMovement 까지 줄곧 이동 락 상태 — 즉 발사 애니가 끝나든 말든(시간정지로
//   anim rate 0 되어 안 끝날 수도 있음) 패턴이 끝날 때까지 보스는 다른 행동을 못 함. 요구사항 충족.
//   (montage 종료를 GA 종료조건에 넣으면 시간정지로 montage 가 frozen 돼서 GA 가 ~수 초 idle 됨 → 안 넣음)
void UEnemyGameplayAbility_RealityFracture::OnPatternFinished(bool bWasBroken)
{
	RF_LOG("OnPatternFinished: bWasBroken=%s, bMontageFinished=%s",
		bWasBroken ? TEXT("TRUE") : TEXT("FALSE"), bMontageFinished ? TEXT("TRUE") : TEXT("FALSE"));
	bPatternFinished = true;
	HandleFinished(false);
}

void UEnemyGameplayAbility_RealityFracture::OnMontageCompleted()
{
	RF_LOG("OnMontageCompleted: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));
	bMontageFinished = true;
	// 패턴이 이미 끝났는데도 montage 가 늦게 끝난 경우(거의 없음)에만 여기서 종료. 보통은 OnPatternFinished 가 먼저.
	if (bPatternFinished)
	{
		HandleFinished(false);
	}
}

void UEnemyGameplayAbility_RealityFracture::OnMontageCancelled()
{
	RF_LOG("OnMontageCancelled: bPatternFinished=%s", bPatternFinished ? TEXT("TRUE") : TEXT("FALSE"));
	bMontageFinished = true;
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
	bMontageFinished = false;

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
