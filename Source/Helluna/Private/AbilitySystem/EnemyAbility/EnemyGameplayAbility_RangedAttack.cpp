	// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RangedAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Weapon/Projectile/HellunaProjectile_Enemy.h"
#include "Engine/World.h"

UEnemyGameplayAbility_RangedAttack::UEnemyGameplayAbility_RangedAttack()
{
	InstancingPolicy   = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

// ============================================================
// ActivateAbility
// ============================================================
void UEnemyGameplayAbility_RangedAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bProjectileLaunched = false;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 공격 중 상태 태그 추가
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(Tag);
	}

	UAnimMontage* AttackMontage = Enemy->AttackMontage;

	// 몽타주 없으면 즉시 발사 후 종료
	if (!AttackMontage)
	{
		SpawnAndLaunchProjectile();
		HandleAttackFinished();
		return;
	}

	// 이동 잠금
	Enemy->LockMovementAndFaceTarget(nullptr);

	// 광폭화 시 재생 속도 배율 적용
	const float PlayRate = Enemy->bEnraged ? Enemy->EnrageAttackMontagePlayRate : 1.f;

	// ── 발사 딜레이 타이머 설정 ───────────────────────────────────
	// 발사 시간 = (몽타주 길이 / PlayRate) * LaunchDelayRatio
	// PlayRate 가 빠를수록 실제 발사 시간도 비례해서 짧아진다.
	// 예) 몽타주 2초, PlayRate 1.5, Ratio 0.5 → 0.67초 후 발사
	const float MontageDuration = AttackMontage->GetPlayLength() / PlayRate;
	const float LaunchDelay     = MontageDuration * FMath::Clamp(LaunchDelayRatio, 0.f, 1.f);

	GetWorld()->GetTimerManager().SetTimer(
		LaunchTimerHandle,
		[this]()
		{
			if (!bProjectileLaunched)
			{
				SpawnAndLaunchProjectile();
			}
		},
		LaunchDelay,
		false
	);

	// ── 몽타주 재생 태스크 ────────────────────────────────────────
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, AttackMontage, PlayRate, NAME_None, false
	);

	if (!MontageTask)
	{
		GetWorld()->GetTimerManager().ClearTimer(LaunchTimerHandle);
		SpawnAndLaunchProjectile();
		HandleAttackFinished();
		return;
	}

	MontageTask->OnCompleted.AddDynamic  (this, &UEnemyGameplayAbility_RangedAttack::OnMontageCompleted);
	MontageTask->OnCancelled.AddDynamic  (this, &UEnemyGameplayAbility_RangedAttack::OnMontageCancelled);
	MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_RangedAttack::OnMontageCancelled);

	MontageTask->ReadyForActivation();
}

// ============================================================
// OnMontageCompleted — 몽타주 정상 완료
// ============================================================
void UEnemyGameplayAbility_RangedAttack::OnMontageCompleted()
{

	// LaunchDelayRatio 가 1.0 에 가까워서 타이머 전에 몽타주가 끝난 경우 여기서 발사
	if (!bProjectileLaunched)
	{
		SpawnAndLaunchProjectile();
	}

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) { HandleAttackFinished(); return; }

	UWorld* World = Enemy->GetWorld();
	if (!World) { HandleAttackFinished(); return; }

	// AttackRecoveryDelay 후 이동 잠금 해제 → 종료
	World->GetTimerManager().SetTimer(
		DelayedReleaseTimerHandle,
		[this]()
		{
			if (AHellunaEnemyCharacter* E = GetEnemyCharacterFromActorInfo())
			{
				E->UnlockMovement();
			}
			HandleAttackFinished();
		},
		AttackRecoveryDelay,
		false
	);
}

// ============================================================
// OnMontageCancelled — 몽타주 중단
// ============================================================
void UEnemyGameplayAbility_RangedAttack::OnMontageCancelled()
{

	// 아직 발사 전이면 취소 시점에 발사
	if (!bProjectileLaunched)
	{
		SpawnAndLaunchProjectile();
	}

	const FGameplayAbilitySpecHandle     H  = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo*     AI = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo AV = GetCurrentActivationInfo();
	EndAbility(H, AI, AV, true, true);
}

// ============================================================
// SpawnAndLaunchProjectile — 투사체 스폰 & 발사
// ============================================================
void UEnemyGameplayAbility_RangedAttack::SpawnAndLaunchProjectile()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	if (!ProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RangedAttack] ProjectileClass null — %s GA BP 에서 설정 필요"), *Enemy->GetName());
		return;
	}

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	// 발사 위치: 전방 오프셋 + 높이 오프셋
	const FVector SpawnLocation = Enemy->GetActorLocation()
		+ Enemy->GetActorForwardVector() * LaunchForwardOffset
		+ FVector(0.f, 0.f, LaunchHeightOffset);
	const FRotator SpawnRotation = Enemy->GetActorRotation();

	// 발사 방향: 타겟 있으면 타겟 중심, 없으면 정면
	FVector LaunchDirection;
	if (CurrentTarget.IsValid())
	{
		LaunchDirection = (CurrentTarget->GetActorLocation() - SpawnLocation).GetSafeNormal();
	}
	else
	{
		LaunchDirection = Enemy->GetActorForwardVector();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner      = Enemy;
	SpawnParams.Instigator = Enemy;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHellunaProjectile_Enemy* Projectile = World->SpawnActor<AHellunaProjectile_Enemy>(
		ProjectileClass, SpawnLocation, SpawnRotation, SpawnParams
	);

	if (!Projectile) return;

	// 광폭화 시 데미지 배율 적용
	const float FinalDamage = Enemy->bEnraged
		? AttackDamage * Enemy->EnrageDamageMultiplier
		: AttackDamage;

	Projectile->InitProjectile(FinalDamage, LaunchDirection, ProjectileSpeed, ProjectileLifeSeconds, Enemy);

	bProjectileLaunched = true;
}

// ============================================================
// HandleAttackFinished
// ============================================================
void UEnemyGameplayAbility_RangedAttack::HandleAttackFinished()
{
	const FGameplayAbilitySpecHandle     H  = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo*     AI = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo AV = GetCurrentActivationInfo();
	EndAbility(H, AI, AV, true, false);
}

// ============================================================
// EndAbility
// ============================================================
void UEnemyGameplayAbility_RangedAttack::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		Enemy->UnlockMovement();

		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(LaunchTimerHandle);
			World->GetTimerManager().ClearTimer(DelayedReleaseTimerHandle);
		}
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(Tag);
	}

	CurrentTarget       = nullptr;
	bProjectileLaunched = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
