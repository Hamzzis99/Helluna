// Capstone Project Helluna

#include "AbilitySystem/EnemyAbility/EnemyGameplayAbility_TimeDistortion.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameplayTagContainer.h"

#include "AbilitySystem/EnemyAbility/TimeDistortionOrb.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

// 디버그 로그 매크로
#define TD_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[TimeDistortion] " Fmt), ##__VA_ARGS__)

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
	TD_LOG("=== ActivateAbility START ===");

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bPatternBroken = false;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		TD_LOG("FAIL: Enemy is null, ending ability");
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	TD_LOG("Enemy: %s, Location: %s", *Enemy->GetName(), *Enemy->GetActorLocation().ToString());

	// 공격 중 상태 태그 추가 (StateTree 전환 방지)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->AddLooseGameplayTags(AttackingTag);
		TD_LOG("Added State.Enemy.Attacking tag");
	}
	else
	{
		TD_LOG("WARNING: ASC is null, could not add attacking tag");
	}

	// 이동 잠금
	Enemy->LockMovementAndFaceTarget(nullptr);
	TD_LOG("Movement locked");

	// 시전 준비 VFX 스폰 (멀티캐스트 — 모든 클라이언트)
	if (ChargingVFX)
	{
		Enemy->Multicast_SpawnPersistentVFX(0, ChargingVFX, ChargingVFXScale);
		TD_LOG("ChargingVFX multicast spawned (Slot 0), Scale=%.2f", ChargingVFXScale);
	}
	else
	{
		TD_LOG("WARNING: ChargingVFX is null, skipping");
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
			TD_LOG("CastMontage started: %s", *CastMontage->GetName());
		}
		else
		{
			TD_LOG("WARNING: MontageTask creation failed");
		}
	}
	else
	{
		TD_LOG("WARNING: CastMontage is null, no animation will play");
	}

	// SlowStartDelay 후 슬로우 적용 타이머
	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		TD_LOG("FAIL: World is null, aborting");
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
	TD_LOG("SlowStart timer set: %.2f seconds", SlowStartDelay);
	TD_LOG("=== ActivateAbility END ===");
}

// ─────────────────────────────────────────────────────────────
// ApplyTimeSlow — 슬로우 시작 + Orb 스폰
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::ApplyTimeSlow()
{
	TD_LOG("=== ApplyTimeSlow START ===");

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy)
	{
		TD_LOG("FAIL: Enemy is null");
		HandleFinished(true);
		return;
	}

	// 시전 준비 VFX 끄기 (멀티캐스트)
	Enemy->Multicast_StopPersistentVFX(0);
	TD_LOG("ChargingVFX stopped (Slot 0)");

	// 범위 내 플레이어 수집
	TArray<AHellunaHeroCharacter*> PlayersInRange;
	GatherPlayersInRadius(PlayersInRange);
	TD_LOG("Players in radius (%.0f cm): %d", DistortionRadius, PlayersInRange.Num());

	// 각 플레이어의 이동속도 + 애니메이션 + TimeDilation 저장 후 감속 적용
	SlowedPlayers.Reset();
	for (AHellunaHeroCharacter* Player : PlayersInRange)
	{
		if (!IsValid(Player))
		{
			continue;
		}

		FPlayerSlowState State;
		State.OriginalTimeDilation = Player->CustomTimeDilation;

		// ── 이동속도: MoveSpeedMultiplier 배율 설정 ──
		// GA_Run/GA_Aim 등이 MaxWalkSpeed 설정 시 이 배율을 곱해서 적용한다.
		// 현재 MaxWalkSpeed에도 즉시 반영한다.
		Player->SetMoveSpeedMultiplier(TimeDilationScale);
		if (UCharacterMovementComponent* CMC = Player->GetCharacterMovement())
		{
			const float Before = CMC->MaxWalkSpeed;
			CMC->MaxWalkSpeed *= TimeDilationScale;
			TD_LOG("Player [%s] WalkSpeed: %.0f * %.2f = %.0f (Multiplier set)",
				*Player->GetName(), Before, TimeDilationScale, CMC->MaxWalkSpeed);
		}

		// ── 애니메이션 속도 ──
		USkeletalMeshComponent* Mesh = Player->GetMesh();
		if (Mesh)
		{
			State.OriginalAnimPlayRate = Mesh->GlobalAnimRateScale;
			Mesh->GlobalAnimRateScale *= TimeDilationScale;
			TD_LOG("Player [%s] AnimRate: %.2f * %.2f = %.2f",
				*Player->GetName(), State.OriginalAnimPlayRate, TimeDilationScale, Mesh->GlobalAnimRateScale);
		}

		// ── CustomTimeDilation은 건드리지 않음 ──
		// MoveSpeedMultiplier가 이동속도, GlobalAnimRateScale이 애니메이션을 처리.
		// CustomTimeDilation까지 바꾸면 이중 적용되어 속도가 극단적으로 느려진다.
		TD_LOG("Player [%s] slowed: MoveMultiplier=%.2f, AnimRate=%.2f (CustomTimeDilation unchanged=%.2f)",
			*Player->GetName(), TimeDilationScale, TimeDilationScale, Player->CustomTimeDilation);

		SlowedPlayers.Add(Player, State);
	}

	if (PlayersInRange.Num() == 0)
	{
		TD_LOG("WARNING: No players found in radius! Check DistortionRadius=%.0f", DistortionRadius);
	}

	// 슬로우 영역 VFX 스폰 (멀티캐스트 — 모든 클라이언트)
	if (SlowAreaVFX)
	{
		Enemy->Multicast_SpawnPersistentVFX(1, SlowAreaVFX, SlowAreaVFXScale);
		TD_LOG("SlowAreaVFX multicast spawned (Slot 1), Scale=%.2f", SlowAreaVFXScale);
	}
	else
	{
		TD_LOG("WARNING: SlowAreaVFX is null");
	}

	// 슬로우 시작 사운드 (멀티캐스트)
	if (SlowStartSound)
	{
		Enemy->Multicast_PlaySoundAtLocation(SlowStartSound, Enemy->GetActorLocation());
		TD_LOG("SlowStartSound multicast played");
	}

	// Orb 순차 스폰 시작 (데코 N개 + 키 1개, 간격을 두고 하나씩)
	StartOrbSpawnSequence();

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
		TD_LOG("Detonation timer set: %.2f seconds", SlowDuration);
	}
	TD_LOG("=== ApplyTimeSlow END ===");
}

// ─────────────────────────────────────────────────────────────
// StartOrbSpawnSequence — 순차 스폰 시작
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::StartOrbSpawnSequence()
{
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	OrbSpawnTotalCount = DecoyOrbCount + 1;
	OrbSpawnKeyIndex = FMath::RandRange(0, OrbSpawnTotalCount - 1);
	OrbSpawnCurrentIndex = 0;

	TD_LOG("StartOrbSpawnSequence: Total=%d, KeyIndex=%d, Interval=%.2f",
		OrbSpawnTotalCount, OrbSpawnKeyIndex, OrbSpawnInterval);

	// 첫 번째 Orb 즉시 스폰
	SpawnNextOrb();

	// 나머지는 타이머로 순차 스폰
	if (OrbSpawnCurrentIndex < OrbSpawnTotalCount)
	{
		World->GetTimerManager().SetTimer(
			OrbSpawnTimerHandle,
			this,
			&UEnemyGameplayAbility_TimeDistortion::SpawnNextOrb,
			OrbSpawnInterval,
			true  // 반복
		);
	}
}

// ─────────────────────────────────────────────────────────────
// SpawnNextOrb — 다음 Orb 하나 스폰 (타이머 콜백)
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::SpawnNextOrb()
{
	// 패턴이 이미 파훼됐으면 중단
	if (bPatternBroken)
	{
		AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
		if (Enemy)
		{
			if (UWorld* World = Enemy->GetWorld())
			{
				World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
			}
		}
		return;
	}

	if (OrbSpawnCurrentIndex >= OrbSpawnTotalCount)
	{
		// 모든 Orb 스폰 완료 → 타이머 중지
		AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
		if (Enemy)
		{
			if (UWorld* World = Enemy->GetWorld())
			{
				World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
			}
		}
		TD_LOG("All orbs spawned: %d total", SpawnedOrbs.Num());
		return;
	}

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (!Enemy) return;

	UWorld* World = Enemy->GetWorld();
	if (!World) return;

	const FVector BossLocation = Enemy->GetActorLocation();
	const int32 i = OrbSpawnCurrentIndex;

	// 원형 배치: 균등 분할 각도
	const float AngleRad = FMath::DegreesToRadians(360.f * static_cast<float>(i) / static_cast<float>(OrbSpawnTotalCount));
	const FVector Offset(
		FMath::Cos(AngleRad) * OrbSpawnRadius,
		FMath::Sin(AngleRad) * OrbSpawnRadius,
		OrbHeightOffset
	);
	const FVector SpawnLocation = BossLocation + Offset;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Enemy;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATimeDistortionOrb* Orb = World->SpawnActor<ATimeDistortionOrb>(
		ATimeDistortionOrb::StaticClass(),
		SpawnLocation,
		FRotator::ZeroRotator,
		SpawnParams
	);

	if (!Orb)
	{
		TD_LOG("WARNING: Failed to spawn Orb at index %d", i);
		OrbSpawnCurrentIndex++;
		return;
	}

	const bool bIsKey = (i == OrbSpawnKeyIndex);

	if (bIsKey)
	{
		Orb->InitOrb(KeyOrbVFX, KeyOrbVFXScale, true);
		Orb->DestroyVFX = KeyOrbDestroyVFX;
		Orb->DestroyVFXScale = KeyOrbDestroyVFXScale;
		TD_LOG("Orb[%d/%d] = KEY Orb at %s", i, OrbSpawnTotalCount, *SpawnLocation.ToString());
	}
	else
	{
		Orb->InitOrb(DecoyOrbVFX, DecoyOrbVFXScale, false);
		Orb->DestroyVFX = DecoyOrbDestroyVFX;
		Orb->DestroyVFXScale = DecoyOrbDestroyVFXScale;
		TD_LOG("Orb[%d/%d] = Decoy Orb at %s", i, OrbSpawnTotalCount, *SpawnLocation.ToString());
	}

	// 파괴 콜백 바인딩
	Orb->OnOrbDestroyed.AddDynamic(this, &UEnemyGameplayAbility_TimeDistortion::OnOrbDestroyed);
	SpawnedOrbs.Add(Orb);

	OrbSpawnCurrentIndex++;
}

// ─────────────────────────────────────────────────────────────
// DestroyAllOrbs — 모든 Orb 제거
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::DestroyAllOrbs()
{
	TD_LOG("DestroyAllOrbs: %d orbs to destroy", SpawnedOrbs.Num());

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

	TD_LOG("OnOrbDestroyed: %s, bIsKeyOrb=%s, bPatternBroken=%s",
		*DestroyedOrb->GetName(),
		DestroyedOrb->bIsKeyOrb ? TEXT("TRUE") : TEXT("FALSE"),
		bPatternBroken ? TEXT("TRUE") : TEXT("FALSE"));

	// 배열에서 제거
	SpawnedOrbs.Remove(DestroyedOrb);
	TD_LOG("Remaining orbs: %d", SpawnedOrbs.Num());

	// 키 Orb가 파괴되었으면 패턴 파훼
	if (DestroyedOrb->bIsKeyOrb && !bPatternBroken)
	{
		TD_LOG(">>> KEY ORB destroyed! Pattern broken!");
		OnPatternBroken();
	}
}

// ─────────────────────────────────────────────────────────────
// OnPatternBroken — 파훼 성공 처리
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::OnPatternBroken()
{
	TD_LOG("=== OnPatternBroken START ===");
	bPatternBroken = true;

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();

	// 폭발 타이머 취소 (데미지 없음)
	if (Enemy)
	{
		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(DetonationTimerHandle);
			TD_LOG("Detonation timer cancelled");
		}
	}

	// 시간 복원
	RestoreAllTimeDilation();

	// 슬로우 영역 VFX 끄기 (멀티캐스트)
	if (Enemy)
	{
		Enemy->Multicast_StopPersistentVFX(1);
		TD_LOG("SlowAreaVFX stopped (Slot 1)");
	}

	// 남은 Orb 전부 제거
	DestroyAllOrbs();

	// 파훼 성공 VFX (멀티캐스트)
	if (Enemy && BreakSuccessVFX)
	{
		Enemy->MulticastPlayEffect(
			Enemy->GetActorLocation(),
			BreakSuccessVFX,
			BreakSuccessVFXScale,
			false
		);
		TD_LOG("BreakSuccessVFX multicast played");
	}

	// 파훼 성공 사운드 (멀티캐스트)
	if (Enemy && BreakSuccessSound)
	{
		Enemy->Multicast_PlaySoundAtLocation(BreakSuccessSound, Enemy->GetActorLocation());
		TD_LOG("BreakSuccessSound multicast played");
	}

	// 어빌리티 종료
	TD_LOG("=== OnPatternBroken END → HandleFinished ===");
	HandleFinished(false);
}

// ─────────────────────────────────────────────────────────────
// DetonateAndRestore — 시간 복원 + 데미지 (파훼 실패)
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::DetonateAndRestore()
{
	TD_LOG("=== DetonateAndRestore START ===");

	// 이미 파훼되었으면 무시
	if (bPatternBroken)
	{
		TD_LOG("Already broken, skipping detonation");
		return;
	}

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();

	// 1) 시간 복원 (먼저 복원해야 플레이어가 정상 속도로 데미지 리액션 수행)
	RestoreAllTimeDilation();

	// 2) 슬로우 영역 VFX 끄기 (멀티캐스트)
	if (Enemy)
	{
		Enemy->Multicast_StopPersistentVFX(1);
		TD_LOG("SlowAreaVFX stopped (Slot 1)");
	}

	// 3) Orb 전부 제거
	DestroyAllOrbs();

	if (!Enemy)
	{
		TD_LOG("FAIL: Enemy is null after restore");
		return;
	}

	// 4) 폭발 VFX 재생 (멀티캐스트)
	if (DetonationVFX)
	{
		Enemy->MulticastPlayEffect(
			Enemy->GetActorLocation(),
			DetonationVFX,
			DetonationVFXScale,
			false
		);
		TD_LOG("DetonationVFX multicast played");
	}

	// 폭발 사운드 (멀티캐스트)
	if (DetonationSound)
	{
		Enemy->Multicast_PlaySoundAtLocation(DetonationSound, Enemy->GetActorLocation());
		TD_LOG("DetonationSound multicast played");
	}

	// 5) 범위 내 플레이어에게 데미지 적용
	if (DetonationDamage > 0.f)
	{
		TArray<AHellunaHeroCharacter*> PlayersInRange;
		GatherPlayersInRadius(PlayersInRange);

		TD_LOG("Detonation damage: %.1f to %d players", DetonationDamage, PlayersInRange.Num());

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
			TD_LOG("Damage applied to %s: %.1f", *Player->GetName(), DetonationDamage);
		}
	}
	TD_LOG("=== DetonateAndRestore END → HandleFinished ===");

	// ★ 추가: 폭발 후 어빌리티 종료 (몽타주가 이미 완료/취소된 경우 여기서 종료)
	HandleFinished(false);
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
		TD_LOG("GatherPlayers: Enemy is null");
		return;
	}

	const FVector BossLocation = Enemy->GetActorLocation();
	const float RadiusSq = DistortionRadius * DistortionRadius;

	UWorld* World = Enemy->GetWorld();
	if (!World)
	{
		return;
	}

	int32 TotalPlayers = 0;
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
			TD_LOG("GatherPlayers: PC [%s] has no HeroCharacter pawn", *PC->GetName());
			continue;
		}

		TotalPlayers++;
		const float DistSq = FVector::DistSquared(BossLocation, Hero->GetActorLocation());
		const float Dist = FMath::Sqrt(DistSq);

		if (DistSq <= RadiusSq)
		{
			OutPlayers.Add(Hero);
			TD_LOG("GatherPlayers: [%s] IN range (dist=%.0f, radius=%.0f)", *Hero->GetName(), Dist, DistortionRadius);
		}
		else
		{
			TD_LOG("GatherPlayers: [%s] OUT of range (dist=%.0f, radius=%.0f)", *Hero->GetName(), Dist, DistortionRadius);
		}
	}

	TD_LOG("GatherPlayers: %d total players, %d in range", TotalPlayers, OutPlayers.Num());
}

// ─────────────────────────────────────────────────────────────
// RestoreAllTimeDilation
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::RestoreAllTimeDilation()
{
	TD_LOG("RestoreAllTimeDilation: %d players to restore", SlowedPlayers.Num());

	for (auto& Pair : SlowedPlayers)
	{
		AHellunaHeroCharacter* Player = Pair.Key.Get();
		if (!IsValid(Player))
		{
			TD_LOG("WARNING: Player reference invalid, skipping restore");
			continue;
		}

		const FPlayerSlowState& State = Pair.Value;

		// ── 이동속도: 배율 리셋 + 현재 MaxWalkSpeed 역산 ──
		Player->SetMoveSpeedMultiplier(1.f);
		if (UCharacterMovementComponent* CMC = Player->GetCharacterMovement())
		{
			const float Before = CMC->MaxWalkSpeed;
			if (TimeDilationScale > KINDA_SMALL_NUMBER)
			{
				CMC->MaxWalkSpeed /= TimeDilationScale;
			}
			TD_LOG("Restoring [%s] WalkSpeed: %.0f / %.2f = %.0f (Multiplier reset to 1.0)",
				*Player->GetName(), Before, TimeDilationScale, CMC->MaxWalkSpeed);
		}

		// ── 애니메이션 속도 복원 ──
		if (USkeletalMeshComponent* Mesh = Player->GetMesh())
		{
			TD_LOG("Restoring [%s] AnimRate: %.2f -> %.2f",
				*Player->GetName(), Mesh->GlobalAnimRateScale, State.OriginalAnimPlayRate);
			Mesh->GlobalAnimRateScale = State.OriginalAnimPlayRate;
		}

		TD_LOG("Restoring [%s] complete (MoveMultiplier=1.0)", *Player->GetName());
	}
	SlowedPlayers.Reset();
}

// ─────────────────────────────────────────────────────────────
// 몽타주 콜백
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::OnMontageCompleted()
{
	TD_LOG("OnMontageCompleted: bPatternBroken=%s", bPatternBroken ? TEXT("TRUE") : TEXT("FALSE"));

	// 이미 파훼되었으면 HandleFinished는 OnPatternBroken에서 호출됨
	if (bPatternBroken) return;

	// 몽타주가 끝나도 슬로우 시작/폭발 타이머가 아직 동작 중이면 대기
	// ★ 수정: SlowStartTimerHandle도 체크 — 몽타주가 SlowStartDelay보다 짧으면
	//   ApplyTimeSlow가 아직 실행 안 됐을 수 있다
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		UWorld* World = Enemy->GetWorld();
		if (World)
		{
			if (World->GetTimerManager().IsTimerActive(SlowStartTimerHandle))
			{
				TD_LOG("Montage completed but SlowStart timer still active, waiting...");
				return;
			}
			if (World->GetTimerManager().IsTimerActive(DetonationTimerHandle))
			{
				TD_LOG("Montage completed but Detonation timer still active, waiting...");
				return;
			}
			if (World->GetTimerManager().IsTimerActive(OrbSpawnTimerHandle))
			{
				TD_LOG("Montage completed but OrbSpawn timer still active, waiting...");
				return;
			}
		}
	}

	TD_LOG("Montage completed → HandleFinished");
	HandleFinished(false);
}

void UEnemyGameplayAbility_TimeDistortion::OnMontageCancelled()
{
	TD_LOG("OnMontageCancelled called");

	// ★ 수정: 몽타주가 취소/인터럽트되어도 슬로우 타이머가 진행 중이면
	//   어빌리티를 종료하지 않는다. 시간 왜곡 패턴은 몽타주와 독립적으로 동작한다.
	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		UWorld* World = Enemy->GetWorld();
		if (World)
		{
			if (World->GetTimerManager().IsTimerActive(SlowStartTimerHandle))
			{
				TD_LOG("Montage cancelled but SlowStart timer still active, continuing pattern...");
				return;
			}
			if (World->GetTimerManager().IsTimerActive(DetonationTimerHandle))
			{
				TD_LOG("Montage cancelled but Detonation timer still active, continuing pattern...");
				return;
			}
			if (World->GetTimerManager().IsTimerActive(OrbSpawnTimerHandle))
			{
				TD_LOG("Montage cancelled but OrbSpawn timer still active, continuing pattern...");
				return;
			}
		}
	}

	TD_LOG("Montage cancelled, no active timers → HandleFinished(cancelled)");
	HandleFinished(true);
}

// ─────────────────────────────────────────────────────────────
// HandleFinished — 공통 종료 처리
// ─────────────────────────────────────────────────────────────
void UEnemyGameplayAbility_TimeDistortion::HandleFinished(bool bWasCancelled)
{
	TD_LOG("=== HandleFinished START (bCancelled=%s) ===",
		bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

	// 안전하게 시간 복원 (중복 호출해도 안전)
	RestoreAllTimeDilation();

	// 남은 Orb 정리
	DestroyAllOrbs();

	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, bWasCancelled);

	TD_LOG("=== HandleFinished END ===");
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
	TD_LOG("=== EndAbility START (bCancelled=%s) ===",
		bWasCancelled ? TEXT("TRUE") : TEXT("FALSE"));

	// 시간 복원 보장
	RestoreAllTimeDilation();

	// Orb 정리 보장
	DestroyAllOrbs();

	AHellunaEnemyCharacter* Enemy = GetEnemyCharacterFromActorInfo();
	if (Enemy)
	{
		Enemy->UnlockMovement();
		TD_LOG("Movement unlocked");

		if (UWorld* World = Enemy->GetWorld())
		{
			World->GetTimerManager().ClearTimer(SlowStartTimerHandle);
			World->GetTimerManager().ClearTimer(DetonationTimerHandle);
			World->GetTimerManager().ClearTimer(OrbSpawnTimerHandle);
			TD_LOG("All timers cleared");
		}

		// 지속형 VFX 정리 (멀티캐스트)
		Enemy->Multicast_StopPersistentVFX(0);
		Enemy->Multicast_StopPersistentVFX(1);
		TD_LOG("All persistent VFX stopped");
	}

	// State.Enemy.Attacking 태그 제거
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackingTag;
		AttackingTag.AddTag(FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking")));
		ASC->RemoveLooseGameplayTags(AttackingTag);
		TD_LOG("Removed State.Enemy.Attacking tag");
	}

	bPatternBroken = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	TD_LOG("=== EndAbility COMPLETE ===");
}

#undef TD_LOG
