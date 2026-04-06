// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_TimeDistortion.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "AbilitySystem/EnemyAbility/TimeDistortionOrb.h"
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

	bPatternBroken = false;

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
// ApplyTimeSlow — 슬로우 시작 + Orb 스폰
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

	// Orb 스폰 (데코 N개 + 키 1개)
	SpawnOrbs();

	// SlowDuration 후 폭발 + 시간 복원 (파훼 실패 경로)
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
// SpawnOrbs — 보스 주변에 원형으로 Orb 배치
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::SpawnOrbs()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	const FVector BossLocation = Enemy->GetActorLocation();

	// 총 Orb 수 = 데코 + 키 1개
	const int32 TotalOrbs = DecoyOrbCount + 1;

	// 키 Orb가 들어갈 랜덤 인덱스
	const int32 KeyOrbIndex = FMath::RandRange(0, TotalOrbs - 1);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Enemy;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < TotalOrbs; ++i)
	{
		// 원형 배치: 균등 분할 각도
		const float AngleRad = FMath::DegreesToRadians(360.f * static_cast<float>(i) / static_cast<float>(TotalOrbs));
		const FVector Offset(
			FMath::Cos(AngleRad) * OrbSpawnRadius,
			FMath::Sin(AngleRad) * OrbSpawnRadius,
			OrbHeightOffset
		);
		const FVector SpawnLocation = BossLocation + Offset;

		ATimeDistortionOrb* Orb = World->SpawnActor<ATimeDistortionOrb>(
			ATimeDistortionOrb::StaticClass(),
			SpawnLocation,
			FRotator::ZeroRotator,
			SpawnParams
		);

		if (!Orb) continue;

		const bool bIsKey = (i == KeyOrbIndex);

		if (bIsKey)
		{
			Orb->InitOrb(KeyOrbVFX, KeyOrbVFXScale, true);
			Orb->DestroyVFX = KeyOrbDestroyVFX;
			Orb->DestroyVFXScale = KeyOrbDestroyVFXScale;
		}
		else
		{
			Orb->InitOrb(DecoyOrbVFX, DecoyOrbVFXScale, false);
			Orb->DestroyVFX = DecoyOrbDestroyVFX;
			Orb->DestroyVFXScale = DecoyOrbDestroyVFXScale;
		}

		// 파괴 콜백 바인딩
		Orb->OnOrbDestroyed.AddDynamic(this, &UEnemyGameplayAbility_TimeDistortion::OnOrbDestroyed);

		SpawnedOrbs.Add(Orb);
	}
}

// ─────────────────────────────────────────────────────────────
// DestroyAllOrbs — 모든 Orb 제거
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::DestroyAllOrbs()
{
	for (ATimeDistortionOrb* Orb : SpawnedOrbs)
	{
		if (IsValid(Orb))
		{
			Orb->OnOrbDestroyed.RemoveDynamic(this, &UEnemyGameplayAbility_TimeDistortion::OnOrbDestroyed);
			Orb->Destroy();
		}
	}
	SpawnedOrbs.Empty();
}

// ─────────────────────────────────────────────────────────────
// OnOrbDestroyed — Orb 파괴 콜백
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::OnOrbDestroyed(ATimeDistortionOrb* DestroyedOrb)
{
	if (!DestroyedOrb) return;

	// 배열에서 제거
	SpawnedOrbs.Remove(DestroyedOrb);

	// 키 Orb가 파괴되었으면 패턴 파훼
	if (DestroyedOrb->bIsKeyOrb && !bPatternBroken)
	{
		OnPatternBroken();
	}
}

// ─────────────────────────────────────────────────────────────
// OnPatternBroken — 파훼 성공 처리
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::OnPatternBroken()
{
	bPatternBroken = true;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();

	// 폭발 타이머 취소 (데미지 없음)
	if (Enemy)
	{
		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(DetonationTimerHandle);
		}
	}

	// 시간 복원
	RestoreAllTimeDilation();

	// 슬로우 영역 VFX 끄기
	if (ActiveSlowAreaVFXComp)
	{
		ActiveSlowAreaVFXComp->DeactivateImmediate();
		ActiveSlowAreaVFXComp = nullptr;
	}

	// 남은 Orb 전부 제거
	DestroyAllOrbs();

	// 파훼 성공 VFX
	if (Enemy && BreakSuccessVFX)
	{
		Enemy->MulticastPlayEffect(
			Enemy->GetActorLocation(),
			BreakSuccessVFX,
			BreakSuccessVFXScale,
			false
		);
	}

	// 파훼 성공 사운드
	if (Enemy && BreakSuccessSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			Enemy->GetWorld(), BreakSuccessSound, Enemy->GetActorLocation()
		);
	}

	// 어빌리티 종료
	HandleFinished(false);
}

// ─────────────────────────────────────────────────────────────
// DetonateAndRestore — 시간 복원 + 데미지 (파훼 실패)
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::DetonateAndRestore()
{
	// 이미 파훼되었으면 무시
	if (bPatternBroken) return;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();

	// 1) 시간 복원 (먼저 복원해야 플레이어가 정상 속도로 데미지 리액션 수행)
	RestoreAllTimeDilation();

	// 2) 슬로우 영역 VFX 끄기
	if (ActiveSlowAreaVFXComp)
	{
		ActiveSlowAreaVFXComp->DeactivateImmediate();
		ActiveSlowAreaVFXComp = nullptr;
	}

	// 3) Orb 전부 제거
	DestroyAllOrbs();

	if (!Enemy)
	{
		return;
	}

	// 4) 폭발 VFX 재생 (MulticastPlayEffect 활용)
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

	// 5) 범위 내 플레이어에게 데미지 적용
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
	// 이미 파훼되었으면 HandleFinished는 OnPatternBroken에서 호출됨
	if (bPatternBroken) return;

	// 몽타주가 끝나도 슬로우/폭발 타이머가 아직 동작 중이면 대기
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		UWorld* World = Enemy->GetWorld();
		if (World && World->GetTimerManager().IsTimerActive(DetonationTimerHandle))
		{
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

	// 남은 Orb 정리
	DestroyAllOrbs();

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

	// Orb 정리 보장
	DestroyAllOrbs();

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

	bPatternBroken = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
