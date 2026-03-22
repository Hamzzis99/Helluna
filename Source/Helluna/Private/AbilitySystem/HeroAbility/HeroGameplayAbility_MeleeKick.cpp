// Capstone Project Helluna — Melee Kick System

#include "AbilitySystem/HeroAbility/HeroGameplayAbility_MeleeKick.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "AIController.h"

#include "AbilitySystem/HellunaAbilitySystemComponent.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "Combat/MeleeTraceComponent.h"
#include "HellunaGameplayTags.h"
#include "HellunaFunctionLibrary.h"
#include "Engine/OverlapResult.h"
#include "NiagaraFunctionLibrary.h"

DEFINE_LOG_CATEGORY(LogMeleeKick);

// ═══════════════════════════════════════════════════════════
// 생성자
// ═══════════════════════════════════════════════════════════

UHeroGameplayAbility_MeleeKick::UHeroGameplayAbility_MeleeKick()
{
	AbilityActivationPolicy = EHellunaAbilityActivationPolicy::OnTriggered;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

// ═══════════════════════════════════════════════════════════
// CanActivateAbility
// ═══════════════════════════════════════════════════════════

bool UHeroGameplayAbility_MeleeKick::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
		return false;

	// 점프/공중 상태에서는 발차기 불가
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (const ACharacter* Char = Cast<ACharacter>(ActorInfo->AvatarActor.Get()))
		{
			if (const UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
			{
				if (CMC->IsFalling()) return false;
			}
		}
	}

	// 전방에 Staggered 적이 있을 때만 활성화
	AHellunaEnemyCharacter* StaggeredEnemy = FindStaggeredEnemy();
	return StaggeredEnemy != nullptr;
}

// ═══════════════════════════════════════════════════════════
// FindStaggeredEnemy
// ═══════════════════════════════════════════════════════════

AHellunaEnemyCharacter* UHeroGameplayAbility_MeleeKick::FindStaggeredEnemy() const
{
	const AHellunaHeroCharacter* Hero = nullptr;
	if (GetCurrentActorInfo() && GetCurrentActorInfo()->AvatarActor.IsValid())
	{
		Hero = Cast<AHellunaHeroCharacter>(GetCurrentActorInfo()->AvatarActor.Get());
	}
	if (!Hero) return nullptr;

	UWorld* World = Hero->GetWorld();
	if (!World) return nullptr;

	const FVector HeroLocation = Hero->GetActorLocation();
	const FVector HeroForward = Hero->GetActorForwardVector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(KickDetectionHalfAngle));

	AHellunaEnemyCharacter* BestEnemy = nullptr;
	float BestDistSq = KickDetectionRange * KickDetectionRange;

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(KickDetectionRange);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Hero);

	if (!World->OverlapMultiByObjectType(
		Overlaps, HeroLocation, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), Sphere, Params))
	{
		return nullptr;
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaEnemyCharacter* Enemy = Cast<AHellunaEnemyCharacter>(Overlap.GetActor());
		if (!Enemy) continue;

		// Staggered 태그 체크
		if (!UHellunaFunctionLibrary::NativeDoesActorHaveTag(Enemy, HellunaGameplayTags::Enemy_State_Staggered))
			continue;

		// 사망 체크
		if (UHellunaHealthComponent* HC = Enemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			if (HC->IsDead()) continue;
		}

		// 전방각 체크
		const FVector ToEnemy = (Enemy->GetActorLocation() - HeroLocation).GetSafeNormal();
		if (FVector::DotProduct(HeroForward, ToEnemy) < CosHalfAngle)
			continue;

		const float DistSq = FVector::DistSquared(HeroLocation, Enemy->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestEnemy = Enemy;
		}
	}

	return BestEnemy;
}

// ═══════════════════════════════════════════════════════════
// ActivateAbility
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Staggered 적 찾기
	KickTarget = FindStaggeredEnemy();
	if (!KickTarget)
	{
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] ActivateAbility — Staggered 적 없음 → 취소"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bKickImpactProcessed = false;
	bSocketHitOccurred = false;

	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] ActivateAbility — 타겟=%s, 워프거리=%.0f"),
		*KickTarget->GetName(), KickWarpDistance);

	// 워프: 적 앞으로 이동
	{
		const FVector EnemyLoc = KickTarget->GetActorLocation();
		const FVector ToHero = (Hero->GetActorLocation() - EnemyLoc).GetSafeNormal();
		FVector WarpLoc = EnemyLoc + ToHero * KickWarpDistance;
		WarpLoc.Z = Hero->GetActorLocation().Z; // Z 유지

		Hero->SetActorLocation(WarpLoc, false, nullptr, ETeleportType::TeleportPhysics);

		// 적 방향으로 회전
		const FRotator LookRot = (EnemyLoc - WarpLoc).Rotation();
		Hero->SetActorRotation(FRotator(0.f, LookRot.Yaw, 0.f));

		if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
		{
			FRotator Ctrl = PC->GetControlRotation();
			Ctrl.Yaw = LookRot.Yaw;
			PC->SetControlRotation(Ctrl);
		}
	}

	// Kicking + Invincible 태그 부여 (킥 중 피격 방지)
	if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
	{
		HeroASC->AddStateTag(HellunaGameplayTags::Hero_State_Kicking);
		HeroASC->AddStateTag(HellunaGameplayTags::Player_State_Invincible);
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Kicking + Invincible 태그 부여"));
	}

	// 킥 중 완전 잠금 (떨림 방지)
	if (UCharacterMovementComponent* CMC = Hero->GetCharacterMovement())
	{
		CMC->StopMovementImmediately();  // Velocity 즉시 0
		CMC->DisableMovement();           // MOVE_None → 중력+이동 OFF
		bSavedOrientRotation = CMC->bOrientRotationToMovement;
		CMC->bOrientRotationToMovement = false;  // CMC 회전 OFF
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 이동+회전 완전 잠금"));
	}

	// === 카메라 연출 시작 (CLIENT만) ===
	if (Hero->IsLocallyControlled())
	{
		if (USpringArmComponent* SpringArm = Hero->FindComponentByClass<USpringArmComponent>())
		{
			SavedKickArmLength = SpringArm->TargetArmLength;
			SavedKickSocketOffset = SpringArm->SocketOffset;

			// 즉시 스냅 (InterpTo 틱 제거 — 떨림 방지)
			SpringArm->TargetArmLength = SavedKickArmLength * KickArmLengthMultiplier;
			SpringArm->SocketOffset = SavedKickSocketOffset + KickCameraSocketOffset;
		}
		if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				SavedKickFOV = PC->PlayerCameraManager->GetFOVAngle();
				PC->PlayerCameraManager->SetFOV(SavedKickFOV * KickFOVMultiplier);
			}
		}

		bKickCameraActive = true;

		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 카메라 즉시 스냅 — ArmLength=%.0f→%.0f, FOV=%.0f→%.0f"),
			SavedKickArmLength, SavedKickArmLength * KickArmLengthMultiplier,
			SavedKickFOV, SavedKickFOV * KickFOVMultiplier);
	}


	// 몽타주 재생
	if (!KickMontage)
	{
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] KickMontage=nullptr → 취소"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, KickMontage, 1.0f);

	// FullBody 몽타주 재생 (ABP에서 전신 오버라이드)
	Hero->PlayFullBody = true;
	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Hero.PlayFullBody = true"));

	if (MontageTask)
	{
		MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnKickMontageCompleted);
		MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnKickMontageCompleted);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnKickMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnKickMontageInterrupted);
		MontageTask->ReadyForActivation();
	}

	// Event.Kick.Impact 대기
	UAbilityTask_WaitGameplayEvent* EventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, HellunaGameplayTags::Event_Kick_Impact, nullptr, false, true);

	if (EventTask)
	{
		EventTask->EventReceived.AddDynamic(this, &ThisClass::OnKickImpactEvent);
		EventTask->ReadyForActivation();
	}

	// ═══════════════════════════════════════════════════════════
	// 소켓 트레이스 시작 + 서버 AnimInstance 강제 Tick
	// ═══════════════════════════════════════════════════════════

	// 서버: AnimInstance 강제 Tick (소켓 위치 정확 추적용)
	if (Hero->HasAuthority())
	{
		if (USkeletalMeshComponent* Mesh = Hero->GetMesh())
		{
			SavedAnimTickOption = Mesh->VisibilityBasedAnimTickOption;
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			UE_LOG(LogMeleeKick, Log, TEXT("[MeleeKick] 서버 AnimInstance 강제 Tick ON"));
		}
	}

	// MeleeTraceComponent 소켓 추적 시작
	if (UMeleeTraceComponent* TraceComp = Hero->GetMeleeTraceComponent())
	{
		TraceComp->OnMeleeHit.AddDynamic(this, &ThisClass::OnSocketHit);
		TraceComp->StartTrace(FName("kick_trace"));
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 소켓 트레이스 시작 — kick_trace, Radius=%.0f"),
			TraceComp->TraceSphereRadius);
	}
}

// ═══════════════════════════════════════════════════════════
// OnSocketHit — 소켓이 실제로 적에 닿았을 때
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::OnSocketHit(AActor* HitActor, const FHitResult& HitResult)
{
	// 이미 소켓 히트 처리됨 → 스킵
	if (bSocketHitOccurred) return;

	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero || !Hero->HasAuthority()) return;

	// 적 캐릭터인지 검증
	AHellunaEnemyCharacter* HitEnemy = Cast<AHellunaEnemyCharacter>(HitActor);
	if (!HitEnemy)
	{
		UE_LOG(LogMeleeKick, Log, TEXT("[MeleeKick] SocketHit 무시 — Actor=%s (적 아님)"),
			HitActor ? *HitActor->GetName() : TEXT("nullptr"));
		return;
	}

	// KickTarget이 아닌 적이면 Staggered 상태인지 확인
	if (HitEnemy != KickTarget.Get())
	{
		if (!UHellunaFunctionLibrary::NativeDoesActorHaveTag(HitEnemy, HellunaGameplayTags::Enemy_State_Staggered))
		{
			UE_LOG(LogMeleeKick, Log, TEXT("[MeleeKick] SocketHit 무시 — Actor=%s (Staggered 아님)"),
				*HitEnemy->GetName());
			return;
		}
	}

	bSocketHitOccurred = true;

	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] SocketHit — 적=%s, ImpactPoint=%s"),
		*HitEnemy->GetName(), *HitResult.ImpactPoint.ToString());

	// 1. 소켓 히트 데미지 (서버 Authority)
	if (KickDamage > 0.f)
	{
		if (UHellunaHealthComponent* HC = HitEnemy->FindComponentByClass<UHellunaHealthComponent>())
		{
			const float NewHP = FMath::Max(0.f, HC->GetHealth() - KickDamage);
			HC->SetHealth(NewHP);
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 소켓 히트 데미지 %.0f → HP=%.1f"), KickDamage, NewHP);
		}
	}

	// 2. 타겟 Staggered 해제
	if (UHellunaAbilitySystemComponent* TargetASC = HitEnemy->GetHellunaAbilitySystemComponent())
	{
		TargetASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_Staggered);
	}

	// 3. 타겟 넉백 (킥 방향)
	{
		FVector KnockDir = (HitEnemy->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal();
		KnockDir.Z = 0.5f;
		KnockDir.Normalize();
		HitEnemy->LaunchCharacter(KnockDir * KickAOEKnockback * 1.5f, true, false);
	}

	// 4. 히트 VFX (ImpactPoint에 나이아가라 스폰 — 로컬 클라이언트만)
	if (KickImpactVFX && Hero->IsLocallyControlled())
	{
		const FRotator VFXRotation = (Hero->GetActorLocation() - HitResult.ImpactPoint).Rotation();
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			Hero->GetWorld(), KickImpactVFX, HitResult.ImpactPoint,
			VFXRotation, FVector(1.f), true, true);
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 소켓 히트 VFX 스폰 — %s @ %s"),
			*KickImpactVFX->GetName(), *HitResult.ImpactPoint.ToString());
	}

	// 5. 카메라 셰이크 (로컬 클라이언트만)
	if (KickCameraShake && Hero->IsLocallyControlled())
	{
		if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
		{
			PC->ClientStartCameraShake(KickCameraShake, KickShakeScale);
		}
	}

	// 6. 히트스탑 (로컬 클라이언트만 — 서버에서 GlobalAnimRateScale 변경 시 리플리케이션 문제)
	if (HitStopDuration > 0.f && Hero->IsLocallyControlled())
	{
		if (USkeletalMeshComponent* Mesh = Hero->GetMesh())
		{
			Mesh->GlobalAnimRateScale = 0.0f;

			FTimerHandle HitStopTimer;
			TWeakObjectPtr<AHellunaHeroCharacter> WeakHero = Hero;
			Hero->GetWorld()->GetTimerManager().SetTimer(HitStopTimer,
				FTimerDelegate::CreateWeakLambda(Hero, [WeakHero]()
				{
					if (WeakHero.IsValid())
					{
						if (USkeletalMeshComponent* M = WeakHero->GetMesh())
						{
							M->GlobalAnimRateScale = 1.0f;
						}
					}
				}), HitStopDuration, false);

			UE_LOG(LogMeleeKick, Log, TEXT("[MeleeKick] 히트스탑 %.3f초"), HitStopDuration);
		}
	}
}

// ═══════════════════════════════════════════════════════════
// OnKickImpactEvent — 노티파이 시점 (AOE 전용 + 소켓 미히트 폴백)
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::OnKickImpactEvent(FGameplayEventData Payload)
{
	AHellunaHeroCharacter* Hero = GetHeroCharacterFromActorInfo();
	if (!Hero || !Hero->HasAuthority() || bKickImpactProcessed) return;
	bKickImpactProcessed = true;

	AHellunaEnemyCharacter* Target = KickTarget.Get();

	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] KickImpact — 타겟=%s, SocketHit=%s, Frame=%llu"),
		Target ? *Target->GetName() : TEXT("nullptr"),
		bSocketHitOccurred ? TEXT("YES") : TEXT("NO(폴백)"),
		GFrameCounter);

	// 하이브리드 판정: 소켓 히트가 없었으면 → 기존 방식 폴백
	if (!bSocketHitOccurred && Target)
	{
		UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 소켓 히트 없음 → Sphere Overlap 폴백 데미지"));

		// 폴백 1. 타겟 데미지
		if (KickDamage > 0.f)
		{
			if (UHellunaHealthComponent* HC = Target->FindComponentByClass<UHellunaHealthComponent>())
			{
				const float NewHP = FMath::Max(0.f, HC->GetHealth() - KickDamage);
				HC->SetHealth(NewHP);
				UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 폴백 데미지 %.0f → HP=%.1f"), KickDamage, NewHP);
			}
		}

		// 폴백 2. 타겟 Staggered 해제 + 넉다운
		if (UHellunaAbilitySystemComponent* TargetASC = Target->GetHellunaAbilitySystemComponent())
		{
			TargetASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_Staggered);
		}

		FVector KnockDir = (Target->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal();
		KnockDir.Z = 0.5f;
		KnockDir.Normalize();
		Target->LaunchCharacter(KnockDir * KickAOEKnockback * 1.5f, true, false);

		// 폴백 카메라 셰이크
		if (KickCameraShake && Hero->IsLocallyControlled())
		{
			if (APlayerController* PC = Cast<APlayerController>(Hero->GetController()))
			{
				PC->ClientStartCameraShake(KickCameraShake, KickShakeScale);
			}
		}
	}

	// 3. AOE: 주변 적 넉백 + Staggered 연쇄
	if (KickAOERadius > 0.f)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Hero);
		if (Target) Params.AddIgnoredActor(Target);

		Hero->GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Hero->GetActorLocation(),
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeSphere(KickAOERadius),
			Params
		);

		int32 AOECount = 0;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AHellunaEnemyCharacter* NearbyEnemy = Cast<AHellunaEnemyCharacter>(Overlap.GetActor());
			if (!NearbyEnemy) continue;

			if (UHellunaHealthComponent* HC = NearbyEnemy->FindComponentByClass<UHellunaHealthComponent>())
			{
				if (HC->IsDead()) continue;

				// AOE 데미지
				if (KickAOEDamage > 0.f)
				{
					const float NewHP = FMath::Max(0.f, HC->GetHealth() - KickAOEDamage);
					HC->SetHealth(NewHP);
				}
			}

			// AOE 넉백
			FVector KnockDir = (NearbyEnemy->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal();
			KnockDir.Z = 0.3f;
			KnockDir.Normalize();
			NearbyEnemy->LaunchCharacter(KnockDir * KickAOEKnockback, true, false);

			// 연쇄 Staggered 부여
			if (KickAOEStaggerDuration > 0.f)
			{
				if (UHellunaAbilitySystemComponent* NearbyASC = NearbyEnemy->GetHellunaAbilitySystemComponent())
				{
					NearbyASC->AddStateTag(HellunaGameplayTags::Enemy_State_Staggered);
					AOECount++;

					// [Multicast] Stagger 비주얼 ON
					NearbyEnemy->Multicast_SetStaggerVisual(StaggerOverlayMaterial, StaggerMontage, true);

					FTimerHandle StaggerTimer;
					TWeakObjectPtr<AHellunaEnemyCharacter> WeakEnemy = NearbyEnemy;
					const float Duration = KickAOEStaggerDuration;
					Hero->GetWorld()->GetTimerManager().SetTimer(StaggerTimer,
						FTimerDelegate::CreateWeakLambda(NearbyEnemy, [WeakEnemy]()
						{
							if (WeakEnemy.IsValid())
							{
								if (UHellunaAbilitySystemComponent* ASC = WeakEnemy->GetHellunaAbilitySystemComponent())
								{
									ASC->RemoveStateTag(HellunaGameplayTags::Enemy_State_Staggered);
								}
								// [Multicast] Stagger 비주얼 OFF
								WeakEnemy->Multicast_SetStaggerVisual(nullptr, nullptr, false);
							}
						}), Duration, false);
				}
			}
		}

		if (AOECount > 0)
		{
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] AOE 넉백 — 주변 적 %d마리, 반경=%.0f, 강도=%.0f"),
				AOECount, KickAOERadius, KickAOEKnockback);
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] AOE Staggered 부여 — %d마리 (연쇄 가능!)"), AOECount);
		}
	}

}

// ═══════════════════════════════════════════════════════════
// 몽타주 콜백
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::OnKickMontageCompleted()
{
	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 몽타주 완료"));
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UHeroGameplayAbility_MeleeKick::OnKickMontageInterrupted()
{
	UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 몽타주 중단"));
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
}

// ═══════════════════════════════════════════════════════════
// EndAbility
// ═══════════════════════════════════════════════════════════

void UHeroGameplayAbility_MeleeKick::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 카메라 즉시 복귀 (취소/정상 종료 모두)
	if (bKickCameraActive && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		AHellunaHeroCharacter* CamHero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get());
		if (CamHero && CamHero->IsLocallyControlled())
		{
			if (USpringArmComponent* SA = CamHero->FindComponentByClass<USpringArmComponent>())
			{
				SA->TargetArmLength = SavedKickArmLength;
				SA->SocketOffset = SavedKickSocketOffset;
			}
			if (APlayerController* PC = Cast<APlayerController>(CamHero->GetController()))
			{
				if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetFOV(SavedKickFOV);
			}
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 카메라 즉시 복귀"));
		}
		bKickCameraActive = false;
	}

	// Kicking 태그 해제
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get()))
		{
			// FullBody 몽타주 해제
			Hero->PlayFullBody = false;
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Hero.PlayFullBody = false"));

			// 이동+회전 복원
			if (UCharacterMovementComponent* CMC = Hero->GetCharacterMovement())
			{
				CMC->SetMovementMode(MOVE_Walking);
				CMC->bOrientRotationToMovement = bSavedOrientRotation;
			}
			UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] 이동+회전 잠금 해제"));

			if (UHellunaAbilitySystemComponent* HeroASC = Hero->GetHellunaAbilitySystemComponent())
			{
				HeroASC->RemoveStateTag(HellunaGameplayTags::Hero_State_Kicking);
				HeroASC->RemoveStateTag(HellunaGameplayTags::Player_State_Invincible);
				UE_LOG(LogMeleeKick, Warning, TEXT("[MeleeKick] Kicking + Invincible 태그 해제"));
			}
		}
	}

	// 소켓 트레이스 종료 + 서버 AnimTick 복원
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (AHellunaHeroCharacter* TraceHero = Cast<AHellunaHeroCharacter>(ActorInfo->AvatarActor.Get()))
		{
			// MeleeTraceComponent 정리
			if (UMeleeTraceComponent* TraceComp = TraceHero->GetMeleeTraceComponent())
			{
				TraceComp->OnMeleeHit.RemoveDynamic(this, &ThisClass::OnSocketHit);
				TraceComp->StopTrace();
			}

			// 서버 AnimInstance Tick 옵션 복원
			if (TraceHero->HasAuthority())
			{
				if (USkeletalMeshComponent* Mesh = TraceHero->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = SavedAnimTickOption;
					UE_LOG(LogMeleeKick, Log, TEXT("[MeleeKick] 서버 AnimInstance Tick 복원"));
				}
			}

			// 히트스탑 안전장치: GlobalAnimRateScale이 0인 채 끝나면 복원
			if (USkeletalMeshComponent* Mesh = TraceHero->GetMesh())
			{
				if (Mesh->GlobalAnimRateScale < 1.0f)
				{
					Mesh->GlobalAnimRateScale = 1.0f;
				}
			}
		}
	}

	KickTarget = nullptr;
	bKickImpactProcessed = false;
	bSocketHitOccurred = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
