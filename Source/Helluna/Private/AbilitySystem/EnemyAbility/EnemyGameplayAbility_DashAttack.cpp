/**
 * EnemyGameplayAbility_DashAttack.cpp
 *
 * 보스 패턴 3: 빠른 돌진 + 근접 공격.
 *
 * @author 김민우
 */

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_DashAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "Character/HellunaEnemyCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

UEnemyGameplayAbility_DashAttack::UEnemyGameplayAbility_DashAttack()
{
	InstancingPolicy   = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

// ============================================================
// ActivateAbility
// ============================================================
void UEnemyGameplayAbility_DashAttack::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 공격 중 상태 태그 추가 (이동 방지 및 StateTree 전환 차단)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(Tag);
	}

	DashHitActors.Reset();

	// 타겟 방향으로 스냅 (LockMovement 없이 회전만)
	AActor* Target = CurrentTarget.Get();
	const FVector ToTarget = Target
		? (Target->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D()
		: Enemy->GetActorForwardVector();

	DashDirection = ToTarget.IsNearlyZero() ? Enemy->GetActorForwardVector() : ToTarget;
	const FRotator FacingRotation(0.f, DashDirection.Rotation().Yaw, 0.f);

	Enemy->LockMovementAndFaceTarget(Target);
	Enemy->SetActorRotation(FacingRotation, ETeleportType::TeleportPhysics);

	// 준비 몬타지가 있으면 Windup → StartDash, 없으면 바로 StartDash
	StartWindup();
}

// ============================================================
// Windup
// ============================================================
void UEnemyGameplayAbility_DashAttack::StartWindup()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) { StartDash(); return; }

	if (!WindupMontage)
	{
		// 준비 몬타지 없음 → 바로 돌진
		StartDash();
		return;
	}

	const float PlayRate = Enemy->bEnraged
		? WindupPlayRate * Enemy->EnrageAttackMontagePlayRate
		: WindupPlayRate;

	Enemy->Multicast_PlayAttackMontage(WindupMontage, PlayRate, Enemy->GetActorRotation());

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, WindupMontage, PlayRate, NAME_None, false
	);

	if (!MontageTask)
	{
		StartDash();
		return;
	}

	MontageTask->OnCompleted.AddDynamic  (this, &UEnemyGameplayAbility_DashAttack::OnWindupCompleted);
	MontageTask->OnCancelled.AddDynamic  (this, &UEnemyGameplayAbility_DashAttack::OnWindupCancelled);
	MontageTask->OnInterrupted.AddDynamic(this, &UEnemyGameplayAbility_DashAttack::OnWindupCancelled);
	MontageTask->ReadyForActivation();
}

void UEnemyGameplayAbility_DashAttack::OnWindupCompleted()
{
	StartDash();
}

void UEnemyGameplayAbility_DashAttack::OnWindupCancelled()
{
	// 준비 중단 시에도 돌진은 이어서 수행 (보스 의도 유지)
	StartDash();
}

// ============================================================
// Dash
// ============================================================
void UEnemyGameplayAbility_DashAttack::StartDash()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		HandleAttackFinished();
		return;
	}

	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		HandleAttackFinished();
		return;
	}

	// 돌진 사운드
	PlayAttackSound();

	// 돌진 중에는 CMC가 물리적으로 이동하도록 Walking 유지
	if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Walking);
	}

	// 돌진 트레일 VFX — Multicast로 모든 클라이언트에서 스폰 (MP 대응)
	if (DashTrailVFX)
	{
		Enemy->Multicast_SpawnAttachedVFX(DashTrailVFX);
	}

	// 돌진 중 몬타지 재생 — 비워두면 locomotion(보통 idle)이 노출되어 미끄러지듯 보임
	if (DashLoopMontage)
	{
		const float LoopRate = Enemy->bEnraged
			? DashLoopPlayRate * Enemy->EnrageAttackMontagePlayRate
			: DashLoopPlayRate;
		Enemy->Multicast_PlayAttackMontage(DashLoopMontage, LoopRate, Enemy->GetActorRotation());
	}

	// 시작 지점 기록 — 경과 거리 계산용 (모든 모드에서 사용)
	DashStartLocation = Enemy->GetActorLocation();

	// 첫 Launch 즉시 적용 + 반복 타이머 등록
	TickDash();

	World->GetTimerManager().SetTimer(
		DashTickTimerHandle,
		[this]() { TickDash(); },
		FMath::Max(0.02f, DashTickInterval),
		true
	);

	// 안전 타임아웃 — MaxDashDistance / DashSpeed 의 2배 여유로 자동 계산.
	// 보스가 벽에 끼이거나 타겟이 도달 불가능해져도 무한 대시 방지.
	// 디자이너는 MaxDashDistance 만 조절하면 됨.
	const float ExpectedTime = MaxDashDistance / FMath::Max(DashSpeed, 500.f);
	const float SafetyTime = FMath::Max(0.3f, ExpectedTime * 2.0f);
	World->GetTimerManager().SetTimer(
		DashEndTimerHandle,
		[this]() { FinishDash(); },
		SafetyTime,
		false
	);
}

void UEnemyGameplayAbility_DashAttack::TickDash()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	AActor* TargetActor = CurrentTarget.Get();
	const FVector Center = Enemy->GetActorLocation();

	// ── Homing 방향 갱신 ─────────────────────────────────────────────
	// Locked 모드는 ActivateAbility에서 고정된 DashDirection 그대로 사용.
	// Homing은 매 틱 타겟을 향해 부드럽게 보간 — HomingInterpSpeed가 클수록 빠르게 추적.
	if (DashDirectionMode == EDashDirectionMode::Homing && TargetActor)
	{
		const FVector ToTarget = (TargetActor->GetActorLocation() - Center).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			// Rotator 기반 보간 — Yaw만 갱신해서 지면 평면 유지.
			const FRotator CurrentDirRot = DashDirection.Rotation();
			const FRotator DesiredDirRot(0.f, ToTarget.Rotation().Yaw, 0.f);
			const FRotator NewDirRot = FMath::RInterpTo(
				CurrentDirRot, DesiredDirRot, DashTickInterval, HomingInterpSpeed);

			DashDirection = NewDirRot.Vector();

			// 회전도 함께 맞춰줌 (돌진 몬타지 방향이 어긋나지 않도록)
			Enemy->SetActorRotation(NewDirRot, ETeleportType::TeleportPhysics);
		}
	}

	// 속도 유지: LaunchCharacter 로 매 간격 속도 재적용
	const float Speed = Enemy->bEnraged
		? DashSpeed * FMath::Max(1.f, Enemy->EnrageAttackMontagePlayRate)
		: DashSpeed;
	Enemy->LaunchCharacter(DashDirection * Speed, true, false);

	// 돌진 충돌 판정
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Enemy);

	World->OverlapMultiByObjectType(
		Overlaps,
		Center,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECollisionChannel::ECC_Pawn),
		FCollisionShape::MakeSphere(DashHitRadius),
		QueryParams
	);

	const float FinalDashDamage = Enemy->bEnraged
		? DashHitDamage * Enemy->EnrageDamageMultiplier
		: DashHitDamage;

	bool bHitTargetThisTick = false;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == Enemy) continue;

		// 아군 몬스터 제외
		if (HitActor->IsA<AHellunaEnemyCharacter>()) continue;

		// 중복 타격 방지
		if (DashHitActors.Contains(HitActor)) continue;
		DashHitActors.Add(HitActor);

		// HitTarget 종료 조건용 — 메인 타겟과 일치 여부 기록
		if (TargetActor && HitActor == TargetActor)
		{
			bHitTargetThisTick = true;
		}

		UGameplayStatics::ApplyDamage(
			HitActor, FinalDashDamage, Enemy->GetInstigatorController(), Enemy, nullptr);

		// [DashHitVFXV1] 피격 대상 위치에 VFX 멀티캐스트. 서버에서만 호출, 모든 머신에서 Niagara 재생.
		if (DashHitVFX)
		{
			const FVector HitVFXLoc = HitActor->GetActorLocation() + FVector(0.f, 0.f, DashHitVFXZOffset);
			Enemy->MulticastPlayEffect(HitVFXLoc, DashHitVFX, DashHitVFXScale, false);
			UE_LOG(LogTemp, Warning,
				TEXT("[DashHitVFXV1] Hit VFX spawned on %s at (%.0f,%.0f,%.0f) scale=%.2f"),
				*HitActor->GetName(), HitVFXLoc.X, HitVFXLoc.Y, HitVFXLoc.Z, DashHitVFXScale);
		}

		if (DashKnockbackForce > 0.f)
		{
			if (ACharacter* HitChar = Cast<ACharacter>(HitActor))
			{
				FVector KnockDir = DashDirection;
				KnockDir.Z = 0.2f;
				KnockDir.Normalize();
				HitChar->LaunchCharacter(KnockDir * DashKnockbackForce, true, true);
			}
		}
	}

	// ── 종료 조건 체크 ───────────────────────────────────────────────
	// MaxDashDistance는 모든 모드에서 공통 안전 상한으로 작동.
	// 자동 안전 타임아웃(DashEndTimerHandle)은 보스가 벽에 끼였을 때의 최후 방어선.
	const float Traveled = FVector::Dist2D(Center, DashStartLocation);
	const bool bReachedMaxDistance = Traveled >= MaxDashDistance;

	bool bEarlyEnd = false;
	const TCHAR* EndReason = TEXT("");

	switch (DashStopMode)
	{
	case EDashStopMode::MaxDistance:
	{
		if (bReachedMaxDistance)
		{
			bEarlyEnd = true;
			EndReason = TEXT("MaxDistance");
		}
		break;
	}
	case EDashStopMode::ReachTarget:
	{
		if (TargetActor)
		{
			const float DistToTarget = FVector::Dist2D(Center, TargetActor->GetActorLocation());
			if (DistToTarget <= StopAtTargetRange)
			{
				bEarlyEnd = true;
				EndReason = TEXT("ReachTarget");
			}
		}
		// ReachTarget에서도 MaxDashDistance 초과 시 안전 종료
		if (!bEarlyEnd && bReachedMaxDistance)
		{
			bEarlyEnd = true;
			EndReason = TEXT("MaxDistance(Safety)");
		}
		break;
	}
	case EDashStopMode::HitTarget:
	{
		if (bHitTargetThisTick)
		{
			bEarlyEnd = true;
			EndReason = TEXT("HitTarget");
		}
		// HitTarget에서도 MaxDashDistance 초과 시 안전 종료
		if (!bEarlyEnd && bReachedMaxDistance)
		{
			bEarlyEnd = true;
			EndReason = TEXT("MaxDistance(Safety)");
		}
		break;
	}
	default:
		break;
	}

	if (bEarlyEnd)
	{
		(void)EndReason;
		// Duration 타이머 취소해 이중 호출 방지, 즉시 FinishDash
		World->GetTimerManager().ClearTimer(DashEndTimerHandle);
		World->GetTimerManager().ClearTimer(DashTickTimerHandle);
		FinishDash();
	}
}

void UEnemyGameplayAbility_DashAttack::FinishDash()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(DashTickTimerHandle);
		}
		// 잔여 속도 감쇠 — 0 방향으로 Launch
		Enemy->LaunchCharacter(FVector::ZeroVector, true, false);

		// [DashEndV1] 대쉬 중 몬타지 중단 — 안 하면 대쉬 포즈가 유지돼 idle 복귀가 안 됨
		if (DashLoopMontage)
		{
			Enemy->Multicast_StopMontage(DashLoopMontage, 0.25f);
		}

		// [DashEndV1] 대쉬 종료 시점에 플레이어 방향으로 회전
		// 준비/돌진 동안은 Locked 방향 유지, 끝나고 나서 비로소 플레이어 쪽으로 face.
		if (AActor* Target = CurrentTarget.Get())
		{
			const FVector ToTarget = (Target->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
			if (!ToTarget.IsNearlyZero())
			{
				const FRotator FaceRot(0.f, ToTarget.Rotation().Yaw, 0.f);
				Enemy->SetActorRotation(FaceRot, ETeleportType::TeleportPhysics);
				if (AController* C = Enemy->GetController())
				{
					C->SetControlRotation(FaceRot);
				}
				// 클라 스냅 동기화
				Enemy->Multicast_SyncAttackRotation(FaceRot);

				UE_LOG(LogTemp, Warning,
					TEXT("[DashEndV1] FinishDash faced target yaw=%.1f target=%s"),
					FaceRot.Yaw, *Target->GetName());
			}
		}
	}

	// [DashFollowupV2] 후속 GA 가 지정돼 있으면 그쪽으로 분기, 아니면 즉시 종료.
	// Why: 마무리 근접 공격은 별도 GA(예: 내려찍기)가 전담. 본 GA 는 대쉬 + 체이닝만 책임.
	//      StateTree 가 다른 패턴으로 못 빠지도록 후속 GA 가 끝날 때까지 살아있음.
	if (FollowupAbilityClass)
	{
		StartFollowupAbility();
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[DashFollowupV2] FollowupAbilityClass 미지정 → 후속 없이 종료"));
	HandleAttackFinished();
}

// ============================================================
// [DashFollowupV1] 후속 GA 체이닝
// ============================================================
void UEnemyGameplayAbility_DashAttack::StartFollowupAbility()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		HandleAttackFinished();
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || !FollowupAbilityClass)
	{
		HandleAttackFinished();
		return;
	}

	// 어빌리티가 없으면 GiveAbility (ShipJump 패턴 따라).
	bool bAlreadyHas = false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == FollowupAbilityClass)
		{
			bAlreadyHas = true;
			break;
		}
	}
	if (!bAlreadyHas)
	{
		FGameplayAbilitySpec Spec(FollowupAbilityClass);
		Spec.SourceObject = Enemy;
		Spec.Level = 1;
		ASC->GiveAbility(Spec);
	}

	// 후속 GA 에 CurrentTarget 주입 (CDO + Instance 양쪽).
	AActor* TargetForFollowup = CurrentTarget.Get();
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != FollowupAbilityClass) continue;
		if (UHellunaEnemyGameplayAbility* CDO = Cast<UHellunaEnemyGameplayAbility>(Spec.Ability))
		{
			CDO->SetCurrentTarget(TargetForFollowup);
		}
		if (UGameplayAbility* Inst = Spec.GetPrimaryInstance())
		{
			if (UHellunaEnemyGameplayAbility* InstTyped = Cast<UHellunaEnemyGameplayAbility>(Inst))
			{
				InstTyped->SetCurrentTarget(TargetForFollowup);
			}
		}
		break;
	}

	// OnAbilityEnded 바인드 — 후속 GA 가 끝나는 순간 콜백.
	FollowupOnAbilityEndedHandle = ASC->OnAbilityEnded.AddUObject(
		this, &UEnemyGameplayAbility_DashAttack::OnFollowupAbilityEnded);

	const bool bActivated = ASC->TryActivateAbilityByClass(FollowupAbilityClass);

	UE_LOG(LogTemp, Warning,
		TEXT("[DashFollowupV1] Enemy=%s Followup=%s Activated=%d Target=%s"),
		*Enemy->GetName(),
		*FollowupAbilityClass->GetName(),
		bActivated ? 1 : 0,
		TargetForFollowup ? *TargetForFollowup->GetName() : TEXT("null"));

	if (!bActivated)
	{
		// 활성화 실패 → 콜백 해제 후 즉시 종료.
		CleanupFollowupBindings();
		HandleAttackFinished();
		return;
	}

	bWaitingForFollowupGA = true;

	// 페일세이프 — 후속 GA 가 EndAbility 신호를 안 보내도 일정 시간 후 강제 종료.
	if (UWorld* World = Enemy->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FollowupFailsafeHandle,
			[this]() { OnFollowupFailsafeTimeout(); },
			FMath::Max(1.f, FollowupFailsafeTime),
			false);
	}
}

void UEnemyGameplayAbility_DashAttack::OnFollowupAbilityEnded(const FAbilityEndedData& EndData)
{
	if (!bWaitingForFollowupGA) return;
	if (!FollowupAbilityClass) return;
	UGameplayAbility* EndedAbility = EndData.AbilityThatEnded;
	if (!EndedAbility) return;
	if (EndedAbility->GetClass() != FollowupAbilityClass) return;

	UE_LOG(LogTemp, Warning,
		TEXT("[DashFollowupV1] Followup ENDED (Cancelled=%d) → DashAttack 종료"),
		EndData.bWasCancelled ? 1 : 0);

	bWaitingForFollowupGA = false;
	CleanupFollowupBindings();
	HandleAttackFinished();
}

void UEnemyGameplayAbility_DashAttack::OnFollowupFailsafeTimeout()
{
	if (!bWaitingForFollowupGA) return;

	UE_LOG(LogTemp, Warning,
		TEXT("[DashFollowupV1] Followup FAILSAFE 발화 — 강제 종료"));

	bWaitingForFollowupGA = false;
	CleanupFollowupBindings();
	HandleAttackFinished();
}

void UEnemyGameplayAbility_DashAttack::CleanupFollowupBindings()
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (FollowupOnAbilityEndedHandle.IsValid())
		{
			ASC->OnAbilityEnded.Remove(FollowupOnAbilityEndedHandle);
		}
	}
	FollowupOnAbilityEndedHandle.Reset();

	if (AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo())
	{
		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(FollowupFailsafeHandle);
		}
	}
}

// ============================================================
// Finish & End
// ============================================================
void UEnemyGameplayAbility_DashAttack::HandleAttackFinished()
{
	const FGameplayAbilitySpecHandle H = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* AI = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo AV = GetCurrentActivationInfo();
	EndAbility(H, AI, AV, true, false);
}

void UEnemyGameplayAbility_DashAttack::EndAbility(
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
			World->GetTimerManager().ClearTimer(DashTickTimerHandle);
			World->GetTimerManager().ClearTimer(DashEndTimerHandle);
			World->GetTimerManager().ClearTimer(FollowupFailsafeHandle);
		}
	}

	// [DashFollowupV1] OnAbilityEnded 콜백 안전 해제 (재진입 방지).
	CleanupFollowupBindings();
	bWaitingForFollowupGA = false;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer Tag;
		Tag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(Tag);
	}

	DashHitActors.Reset();
	CurrentTarget = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
