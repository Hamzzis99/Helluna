// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_TimeDistortion.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

UEnemyGameplayAbility_TimeDistortion::UEnemyGameplayAbility_TimeDistortion()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
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
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 공격 중 상태 태그 추가 (StateTree 전환 방지)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(AttackingTag);
	}

	// 이동 잠금
	Enemy->LockMovementAndFaceTarget(nullptr);

	// 시전 준비 VFX 스폰 (보스에 부착)
	if (ChargingVFX)
	{
		ActiveChargingVFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			ChargingVFX,
			Enemy->GetRootComponent(),
			NAME_None,
			Enemy->GetActorLocation(),
			FRotator::ZeroRotator,
			FVector(ChargingVFXScale),
			EAttachLocation::KeepWorldPosition,
			true,
			ENCPoolMethod::None,
			true
		);
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

	// SlowStartDelay 후 슬로우 적용 타이머
	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		HandleFinished(true);
		return;
	}

	World->GetTimerManager().SetTimer(
		SlowStartTimerHandle,
		this,
		&UEnemyGameplayAbility_TimeDistortion::ApplyTimeSlow,
		SlowStartDelay,
		false
	);
}

// ─────────────────────────────────────────────────────────────
// ApplyTimeSlow — 슬로우 시작
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::ApplyTimeSlow()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		HandleFinished(true);
		return;
	}

	// 시전 준비 VFX 끄기
	if (ActiveChargingVFXComp)
	{
		ActiveChargingVFXComp->DeactivateImmediate();
		ActiveChargingVFXComp = nullptr;
	}

	// 범위 내 플레이어 수집
	TArray<AHellunaHeroCharacter*> PlayersInRange;
	GatherPlayersInRadius(PlayersInRange);

	// 각 플레이어의 CustomTimeDilation 저장 후 감속 적용
	SlowedPlayers.Reset();
	for (AHellunaHeroCharacter* Player : PlayersInRange)
	{
		if (!IsValid(Player))
		{
			continue;
		}

		SlowedPlayers.Add(Player, Player->CustomTimeDilation);
		Player->CustomTimeDilation = TimeDilationScale;
	}

	// 슬로우 영역 VFX 스폰 (보스에 부착, 지속형)
	if (SlowAreaVFX)
	{
		ActiveSlowAreaVFXComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
			SlowAreaVFX,
			Enemy->GetRootComponent(),
			NAME_None,
			Enemy->GetActorLocation(),
			FRotator::ZeroRotator,
			FVector(SlowAreaVFXScale),
			EAttachLocation::KeepWorldPosition,
			true,
			ENCPoolMethod::None,
			true
		);
	}

	// 슬로우 시작 사운드
	if (SlowStartSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			Enemy->GetWorld(), SlowStartSound, Enemy->GetActorLocation()
		);
	}

	// SlowDuration 후 폭발 + 시간 복원
	UWorld* World = Enemy->GetWorld();
	if (World)
	{
		World->GetTimerManager().SetTimer(
			DetonationTimerHandle,
			this,
			&UEnemyGameplayAbility_TimeDistortion::DetonateAndRestore,
			SlowDuration,
			false
		);
	}
}

// ─────────────────────────────────────────────────────────────
// DetonateAndRestore — 시간 복원 + 데미지
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::DetonateAndRestore()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();

	// 1) 시간 복원 (먼저 복원해야 플레이어가 정상 속도로 데미지 리액션 수행)
	RestoreAllTimeDilation();

	// 2) 슬로우 영역 VFX 끄기
	if (ActiveSlowAreaVFXComp)
	{
		ActiveSlowAreaVFXComp->DeactivateImmediate();
		ActiveSlowAreaVFXComp = nullptr;
	}

	if (!Enemy)
	{
		return;
	}

	// 3) 폭발 VFX 재생 (MulticastPlayEffect 활용)
	if (DetonationVFX)
	{
		Enemy->MulticastPlayEffect(
			Enemy->GetActorLocation(),
			DetonationVFX,
			DetonationVFXScale,
			false
		);
	}

	// 폭발 사운드
	if (DetonationSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			Enemy->GetWorld(), DetonationSound, Enemy->GetActorLocation()
		);
	}

	// 4) 범위 내 플레이어에게 데미지 적용
	if (DetonationDamage > 0.f)
	{
		TArray<AHellunaHeroCharacter*> PlayersInRange;
		GatherPlayersInRadius(PlayersInRange);

		for (AHellunaHeroCharacter* Player : PlayersInRange)
		{
			if (!IsValid(Player))
			{
				continue;
			}

			UGameplayStatics::ApplyDamage(
				Player,
				DetonationDamage,
				Enemy->GetController(),
				Enemy,
				UDamageType::StaticClass()
			);
		}
	}
}

// ─────────────────────────────────────────────────────────────
// GatherPlayersInRadius
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::GatherPlayersInRadius(
	TArray<AHellunaHeroCharacter*>& OutPlayers) const
{
	AHellunaEnemyCharacter* Enemy = const_cast<UEnemyGameplayAbility_TimeDistortion*>(this)
		->GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		return;
	}

	const FVector BossLocation = Enemy->GetActorLocation();
	const float RadiusSq = DistortionRadius * DistortionRadius;

	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		return;
	}

	// 모든 플레이어 캐릭터를 순회하여 범위 내 수집
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}

		AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(PC->GetPawn());
		if (!Hero)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(BossLocation, Hero->GetActorLocation());
		if (DistSq <= RadiusSq)
		{
			OutPlayers.Add(Hero);
		}
	}
}

// ─────────────────────────────────────────────────────────────
// RestoreAllTimeDilation
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::RestoreAllTimeDilation()
{
	for (auto& Pair : SlowedPlayers)
	{
		AHellunaHeroCharacter* Player = Pair.Key.Get();
		if (IsValid(Player))
		{
			Player->CustomTimeDilation = Pair.Value;
		}
	}
	SlowedPlayers.Reset();
}

// ─────────────────────────────────────────────────────────────
// 몽타주 콜백
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::OnMontageCompleted()
{
	// 몽타주가 끝나도 슬로우/폭발 타이머가 아직 동작 중이면 대기
	// 타이머가 이미 완료되었으면 바로 종료
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		UWorld* World = Enemy->GetWorld();
		if (World && World->GetTimerManager().IsTimerActive(DetonationTimerHandle))
		{
			// 폭발이 아직 안 터짐 → 폭발 타이머가 끝나면 HandleFinished 호출하도록 대기
			// (타이머 콜백에서 직접 HandleFinished를 호출하지 않으므로
			//  여기서 별도 타이머로 종료를 예약한다)
			return;
		}
	}

	HandleFinished(false);
}

void UEnemyGameplayAbility_TimeDistortion::OnMontageCancelled()
{
	HandleFinished(true);
}

// ─────────────────────────────────────────────────────────────
// HandleFinished — 공통 종료 처리
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::HandleFinished(bool bWasCancelled)
{
	// 안전하게 시간 복원 (중복 호출해도 안전)
	RestoreAllTimeDilation();

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
	// 시간 복원 보장
	RestoreAllTimeDilation();

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		Enemy->UnlockMovement();

		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(SlowStartTimerHandle);
			World->GetTimerManager().ClearTimer(DetonationTimerHandle);
		}
	}

	// VFX 정리
	if (ActiveSlowAreaVFXComp)
	{
		ActiveSlowAreaVFXComp->DeactivateImmediate();
		ActiveSlowAreaVFXComp = nullptr;
	}
	if (ActiveChargingVFXComp)
	{
		ActiveChargingVFXComp->DeactivateImmediate();
		ActiveChargingVFXComp = nullptr;
	}

	// State.Enemy.Attacking 태그 제거
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(AttackingTag);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
