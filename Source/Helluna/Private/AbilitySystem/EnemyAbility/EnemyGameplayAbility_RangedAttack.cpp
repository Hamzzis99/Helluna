	// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_RangedAttack.h"
#include "Helluna.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Object/ResourceUsingObject/HellunaTurretBase.h"
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

	// 공격 중 상태 태그 추가.
	// [JumpLagFix] ErrorIfNotFound=false — 네이티브/INI 등록이 로드 순서상 늦을 경우
	// ensure+StackWalk 비용(수 초) 회피. 태그 정의가 없어도 조용히 no-op 처리.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
		if (AttackTag.IsValid())
		{
			FGameplayTagContainer Tag;
			Tag.AddTag(AttackTag);
			ASC->AddLooseGameplayTags(Tag);
		}
	}

	UAnimMontage* EffectiveMontage = GetEffectiveAttackMontage();

	// 몽타주 없으면 즉시 발사 후 종료
	if (!EffectiveMontage)
	{
		SpawnAndLaunchProjectile();
		HandleAttackFinished();
		return;
	}

	// [FaceTargetV1] 이동 잠금 + 타겟 방향 즉시 스냅.
	// 이전: LockMovementAndFaceTarget(nullptr) — 락만 걸고 회전 안 함 → 타워 향해 안 돌고 발사.
	// StateTree PreActivate 스냅이 있지만 몬타주/투사체는 서버 회전 리플리케이션에 의존 →
	// 클라는 직전 Yaw 를 한 프레임 더 보여 "옆 보고 쏘는" 문제. 여기서 서버 Yaw 를 재확정.
	// 비용: O(1) 벡터 계산 한 번 + SetActorRotation 한 번 — per-enemy 1회/공격 → 무시 가능.
	AActor* AttackTarget = CurrentTarget.Get();
	Enemy->LockMovementAndFaceTarget(AttackTarget);

	// 파라노이드 스냅: LockMovementAndFaceTarget 내부 SetActorRotation 직후 틱이 끼어들
	// 가능성 0 아님 → TeleportPhysics 로 FRepMovement.bTeleport 를 강제해 클라도 즉시 스냅.
	if (AttackTarget)
	{
		const FVector ToTarget = (AttackTarget->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator FacingRotation(0.f, ToTarget.Rotation().Yaw, 0.f);
			Enemy->SetActorRotation(FacingRotation, ETeleportType::TeleportPhysics);
		}
	}

	// 광폭화 시 재생 속도 배율 적용
	const float PlayRate = Enemy->bEnraged ? Enemy->EnrageAttackMontagePlayRate : 1.f;

	// ── 발사 방식 분기 ────────────────────────────────────────────
	// bFireViaAnimNotify=true 면 타이머 스킵 — AnimNotify가 FireProjectileFromNotify 호출.
	// 노티 누락 대비 안전망: OnMontageCompleted/Cancelled에서 미발사 상태면 폴백 발사.
	if (!bFireViaAnimNotify)
	{
		// 기존 방식: (몽타주 길이 / PlayRate) × LaunchDelayRatio 시점에 자동 발사
		const float MontageDuration = EffectiveMontage->GetPlayLength() / PlayRate;
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
	}

	PlayAttackSound();

	// [RangedMPFix] 클라 회전 스냅 + 몬타지 처음부터 재생.
	const FRotator CurrentRot = Enemy->GetActorRotation();

	// ═══ [MPAim][Server] 발사 직전 서버 측 회전 상태 진단 ═══
	float TargetYaw_Diag = 0.f;
	if (AttackTarget)
	{
		const FVector ToT = (AttackTarget->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
		if (!ToT.IsNearlyZero())
		{
			TargetYaw_Diag = ToT.Rotation().Yaw;
		}
	}
	UE_LOG(LogTemp, Warning,
		TEXT("[MPAim][Server] RangedActivate MyYaw=%.1f TargetYaw=%.1f Target=%s"),
		CurrentRot.Yaw, TargetYaw_Diag,
		AttackTarget ? *AttackTarget->GetName() : TEXT("null"));

	Enemy->Multicast_PlayAttackMontage(EffectiveMontage, PlayRate, CurrentRot);

	// ── 서버측 몽타주 재생 태스크 (Completion/Notify 이벤트 수신용) ─────
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, EffectiveMontage, PlayRate, NAME_None, false
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
// FireProjectileFromNotify — AnimNotify 경유 외부 발사 진입점
// ============================================================
void UEnemyGameplayAbility_RangedAttack::FireProjectileFromNotify()
{
	// 이중 발사 방지 — 타이머/노티/완료 콜백 중 하나가 먼저 실행되면 나머지는 no-op
	if (bProjectileLaunched) return;

	// 노티가 클라이언트에서 발화해도 실제 스폰은 서버에서만 필요 (ServerOnly GA)
	const FGameplayAbilityActorInfo* AI = GetCurrentActorInfo();
	AActor* Avatar = AI ? AI->AvatarActor.Get() : nullptr;
	if (!Avatar || !Avatar->HasAuthority()) return;

	SpawnAndLaunchProjectile();
}

// ============================================================
// SpawnAndLaunchProjectile — 투사체 스폰 & 발사
// ============================================================
void UEnemyGameplayAbility_RangedAttack::SpawnAndLaunchProjectile()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	// [RangedSyncFire] 발사 시점 클라 Yaw 재스냅 — 몬타지 중간 smoothing 지연 제거.
	// SpawnActor 보다 먼저 호출해 투사체가 올바른 방향에서 나오는 것처럼 보이게.
	Enemy->Multicast_SyncAttackRotation(Enemy->GetActorRotation());

	if (!ProjectileClass)
	{
#if HELLUNA_DEBUG_ENEMY
		UE_LOG(LogTemp, Warning, TEXT("[RangedAttack] ProjectileClass null — %s GA BP 에서 설정 필요"), *Enemy->GetName());
#endif
		return;
	}

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	// 발사 위치: 전방 오프셋 + 높이 오프셋
	const FVector SpawnLocation = Enemy->GetActorLocation()
		+ Enemy->GetActorForwardVector() * LaunchForwardOffset
		+ FVector(0.f, 0.f, LaunchHeightOffset);
	const FRotator SpawnRotation = Enemy->GetActorRotation();

	// 발사 방향: 타겟 Bounds 중심으로 조준.
	// [AimBoundsCenterV1] ActorLocation 은 타워/우주선 의 경우 피벗이 바닥에 있어서
	//   정확히 바닥을 쏘게 되는 버그 있었음. Bounds 는 콜리전/비주얼 전체를 감싸는
	//   AABB 라 Origin=중심점(몸통 중앙 높이) 이 자동 계산됨.
	// [TurretHorizontalFireV1] 타워 타겟 한정 Z 성분 제거 → 수평 정면 발사.
	//   투사체 스폰 높이(LaunchHeightOffset) 를 맞춰두면 타워 본체에 정확히 적중.
	FVector LaunchDirection;
	if (CurrentTarget.IsValid())
	{
		FVector TargetOrigin, TargetExtent;
		CurrentTarget->GetActorBounds(/*bOnlyCollidingComponents=*/false, TargetOrigin, TargetExtent, /*bIncludeFromChildActors=*/true);
		FVector ToTarget = TargetOrigin - SpawnLocation;
		if (Cast<AHellunaTurretBase>(CurrentTarget.Get()))
		{
			ToTarget.Z = 0.f;
		}
		LaunchDirection = ToTarget.GetSafeNormal();
		if (LaunchDirection.IsNearlyZero())
		{
			LaunchDirection = Enemy->GetActorForwardVector();
		}
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

	// [JumpLagFix] ErrorIfNotFound=false — EndAbility 경로에서도 ensure 비용 회피.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
		if (AttackTag.IsValid())
		{
			FGameplayTagContainer Tag;
			Tag.AddTag(AttackTag);
			ASC->RemoveLooseGameplayTags(Tag);
		}
	}

	CurrentTarget       = nullptr;
	bProjectileLaunched = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
