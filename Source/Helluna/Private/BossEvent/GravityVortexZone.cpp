// Capstone Project Helluna

#include "BossEvent/GravityVortexZone.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

#define GV_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[GravityVortex] " Fmt), ##__VA_ARGS__)

AGravityVortexZone::AGravityVortexZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
}

// -----------------------------------------------------------------
// ActivateZone
// -----------------------------------------------------------------
void AGravityVortexZone::ActivateZone()
{
	Super::ActivateZone();

	if (bZoneActive) return;
	bZoneActive = true;
	ElapsedTime = 0.f;
	CenterDamageAccumulator = 0.f;
	EventHorizonDamageLastTime.Empty();

	GV_LOG("=== ActivateZone (Duration=%.1f, Radius=%.0f→%.0f, Pull=%.0f→%.0f, Orbitals=%d) ===",
		PatternDuration, VortexRadius * StartRadiusRatio, VortexRadius,
		BasePullStrength, MaxPullStrength, OrbitalCount);

	// 궤도 위험물 초기화
	Orbitals.Empty();
	Orbitals.SetNum(OrbitalCount);
	for (int32 i = 0; i < OrbitalCount; ++i)
	{
		Orbitals[i].AngleDeg = 360.f * static_cast<float>(i) / static_cast<float>(OrbitalCount);
		Orbitals[i].HitActorsThisRevolution.Empty();
	}

	// 소용돌이 지속 VFX (블랙홀 이펙트)
	if (VortexVFX && OwnerEnemy)
	{
		OwnerEnemy->Multicast_SpawnPersistentVFX(1, VortexVFX, VortexVFXScale);
	}

	if (PullSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(PullSound, GetActorLocation());
	}

	// 패턴 종료 타이머
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PatternEndTimerHandle, this,
			&AGravityVortexZone::OnPatternTimeUp,
			PatternDuration, false
		);
	}

	SetActorTickEnabled(true);
}

// -----------------------------------------------------------------
// DeactivateZone
// -----------------------------------------------------------------
void AGravityVortexZone::DeactivateZone()
{
	Super::DeactivateZone();

	bZoneActive = false;
	bEventHorizonVFXSpawned = false;
	SetActorTickEnabled(false);
	Orbitals.Empty();

	if (OwnerEnemy)
	{
		OwnerEnemy->Multicast_StopPersistentVFX(1);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PatternEndTimerHandle);
	}
}

// -----------------------------------------------------------------
// Tick
// -----------------------------------------------------------------
void AGravityVortexZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 게임플레이 로직(데미지/VFX 트리거)은 서버에서만. 클라는 멀티캐스트 수신만.
	if (!HasAuthority()) return;

	if (!bZoneActive) return;

	ElapsedTime += DeltaTime;

	ApplyGravityPull(DeltaTime);
	UpdateOrbitals(DeltaTime);
	ProcessCenterDamage(DeltaTime);
	ProcessEventHorizon(DeltaTime);
}

// -----------------------------------------------------------------
// GetCurrentVortexRadius — 시간에 따라 확장
// -----------------------------------------------------------------
float AGravityVortexZone::GetCurrentVortexRadius() const
{
	const float TimeAlpha = FMath::Clamp(ElapsedTime / FMath::Max(PatternDuration, 0.01f), 0.f, 1.f);
	return FMath::Lerp(VortexRadius * StartRadiusRatio, VortexRadius, TimeAlpha);
}

// -----------------------------------------------------------------
// CalculateCurrentPullStrength
// -----------------------------------------------------------------
float AGravityVortexZone::CalculateCurrentPullStrength() const
{
	const float TimeAlpha = FMath::Clamp(ElapsedTime / FMath::Max(PatternDuration, 0.01f), 0.f, 1.f);
	const float TimePull = FMath::Lerp(BasePullStrength, MaxPullStrength, TimeAlpha);
	return TimePull * GetPulseFactor();
}

// -----------------------------------------------------------------
// GetPulseFactor — 비대칭 맥동 (sin 기반)
// -----------------------------------------------------------------
float AGravityVortexZone::GetPulseFactor() const
{
	if (PullPulsePeriod <= 0.f) return 1.f;

	const float Phase = FMath::Fmod(ElapsedTime, PullPulsePeriod) / PullPulsePeriod;

	// Phase 0.0→0.6: 흡인 구간 (factor = 1.0)
	// Phase 0.6→1.0: 해제 구간 (1.0 → MinRatio → 1.0, 대칭 sin 곡선)
	if (Phase < 0.6f)
	{
		return 1.f;
	}

	const float ReleasePhase = (Phase - 0.6f) / 0.4f;
	const float SinValue = FMath::Sin(ReleasePhase * PI);
	return FMath::Lerp(1.f, PullPulseMinRatio, SinValue);
}

// -----------------------------------------------------------------
// ApplyGravityPull — 확장 반경 사용
// -----------------------------------------------------------------
void AGravityVortexZone::ApplyGravityPull(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector Center = GetActorLocation();
	const float CurrentPull = CalculateCurrentPullStrength();
	const float CurrentRadius = GetCurrentVortexRadius();

	if (CurrentPull <= 0.f) return;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		const FVector PlayerLoc = Player->GetActorLocation();
		const float Distance2D = FVector::Dist2D(Center, PlayerLoc);

		if (Distance2D > CurrentRadius) continue;
		if (Distance2D < 10.f) continue;

		// 거리에 따른 계수: 가까울수록 강함 (선형)
		const float DistanceFactor = 1.f - (Distance2D / CurrentRadius);

		// 사건의 지평선 내부면 흡인력 배율 적용
		float HorizonMultiplier = 1.f;
		if (EventHorizonRadius > 0.f && Distance2D < EventHorizonRadius)
		{
			HorizonMultiplier = EventHorizonPullMultiplier;
		}

		const FVector PullDirection = (Center - PlayerLoc).GetSafeNormal2D();
		const FVector PullVelocity = PullDirection * CurrentPull * DistanceFactor * HorizonMultiplier * DeltaTime;

		Player->LaunchCharacter(PullVelocity, false, false);
	}
}

// -----------------------------------------------------------------
// UpdateOrbitals — 거리 기반 판정 (Overlap 없음)
// -----------------------------------------------------------------
void AGravityVortexZone::UpdateOrbitals(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const FVector Center = GetActorLocation();

	const float PulseFactor = GetPulseFactor();
	const bool bInRelease = PulseFactor < 0.8f;
	const float SpeedMultiplier = bInRelease ? OrbitalReleaseSpeedMultiplier : 1.f;
	const float EffectiveSpeed = OrbitalSpeed * SpeedMultiplier;

	// 궤도 위치 갱신
	TArray<FVector> OrbitalPositions;
	OrbitalPositions.SetNum(Orbitals.Num());

	for (int32 i = 0; i < Orbitals.Num(); ++i)
	{
		FOrbitalHazard& Orbital = Orbitals[i];

		Orbital.AngleDeg += EffectiveSpeed * DeltaTime;
		if (Orbital.AngleDeg >= 360.f)
		{
			Orbital.AngleDeg -= 360.f;
			Orbital.HitActorsThisRevolution.Empty();
		}

		const float AngleRad = FMath::DegreesToRadians(Orbital.AngleDeg);
		OrbitalPositions[i] = Center + FVector(
			FMath::Cos(AngleRad) * OrbitalRadius,
			FMath::Sin(AngleRad) * OrbitalRadius,
			OrbitalHeightOffset
		);
	}

	// 플레이어 1회 순회 → 모든 궤도와 거리 체크
	const float HitRadiusSq = OrbitalHitRadius * OrbitalHitRadius;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		const FVector PlayerLoc = Player->GetActorLocation();
		TWeakObjectPtr<AActor> WeakPlayer(Player);

		for (int32 i = 0; i < Orbitals.Num(); ++i)
		{
			if (Orbitals[i].HitActorsThisRevolution.Contains(WeakPlayer)) continue;

			if (FVector::DistSquared(PlayerLoc, OrbitalPositions[i]) > HitRadiusSq) continue;

			Orbitals[i].HitActorsThisRevolution.Add(WeakPlayer);

			UGameplayStatics::ApplyDamage(
				Player, OrbitalDamage,
				OwnerEnemy->GetController(), OwnerEnemy,
				UDamageType::StaticClass()
			);

			GV_LOG("Orbital[%d] hit [%s] for %.0f (angle=%.0f)",
				i, *Player->GetName(), OrbitalDamage, Orbitals[i].AngleDeg);
		}
	}
}

// -----------------------------------------------------------------
// ProcessCenterDamage
// -----------------------------------------------------------------
void AGravityVortexZone::ProcessCenterDamage(float DeltaTime)
{
	CenterDamageAccumulator += DeltaTime;
	if (CenterDamageAccumulator < CenterDamageInterval) return;
	CenterDamageAccumulator -= CenterDamageInterval;

	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const FVector Center = GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(OwnerEnemy);

	World->OverlapMultiByObjectType(
		Overlaps, Center, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(CenterDamageRadius), QueryParams
	);

	TSet<AActor*> Damaged;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Overlap.GetActor());
		if (!IsValid(Player) || Damaged.Contains(Player)) continue;
		Damaged.Add(Player);

		UGameplayStatics::ApplyDamage(
			Player, CenterDamage,
			OwnerEnemy->GetController(), OwnerEnemy,
			UDamageType::StaticClass()
		);

		if (CenterDamageSound)
		{
			OwnerEnemy->Multicast_PlaySoundAtLocation(CenterDamageSound, Center);
		}
	}
}

// -----------------------------------------------------------------
// ProcessEventHorizon — 사건의 지평선 DPS
// -----------------------------------------------------------------
void AGravityVortexZone::ProcessEventHorizon(float DeltaTime)
{
	if (EventHorizonRadius <= 0.f || EventHorizonDPS <= 0.f) return;

	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const FVector Center = GetActorLocation();
	const double CurrentTime = World->GetTimeSeconds();
	const float DPSInterval = 0.25f;

	// 지평선 VFX (1회)
	if (!bEventHorizonVFXSpawned && EventHorizonVFX)
	{
		bEventHorizonVFXSpawned = true;
		OwnerEnemy->MulticastPlayEffect(Center, EventHorizonVFX, EventHorizonVFXScale, false);
	}

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		const float Dist2D = FVector::Dist2D(Center, Player->GetActorLocation());
		if (Dist2D > EventHorizonRadius) continue;
		if (Dist2D < CenterDamageRadius) continue; // 중심 데미지와 중복 방지

		TWeakObjectPtr<AActor> WeakPlayer(Player);
		const double* LastTime = EventHorizonDamageLastTime.Find(WeakPlayer);
		if (LastTime && (CurrentTime - *LastTime) < DPSInterval) continue;

		EventHorizonDamageLastTime.FindOrAdd(WeakPlayer) = CurrentTime;

		UGameplayStatics::ApplyDamage(
			Player, EventHorizonDPS * DPSInterval,
			OwnerEnemy->GetController(), OwnerEnemy,
			UDamageType::StaticClass()
		);
	}
}

// -----------------------------------------------------------------
// OnPatternTimeUp — 폭발 + 넉백 + 종료
// -----------------------------------------------------------------
void AGravityVortexZone::OnPatternTimeUp()
{
	GV_LOG("=== Pattern time up — EXPLOSION ===");

	bZoneActive = false;
	SetActorTickEnabled(false);

	const FVector Center = GetActorLocation();

	// 지속 VFX 정리
	if (OwnerEnemy)
	{
		OwnerEnemy->Multicast_StopPersistentVFX(1);
	}

	// 폭발 VFX
	if (EndExplosionVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(Center, EndExplosionVFX, EndExplosionVFXScale, false);
	}

	// 폭발 사운드
	if (EndExplosionSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(EndExplosionSound, Center);
	}

	// 범위 내 플레이어에게 폭발 데미지 + 넉백
	UWorld* World = GetWorld();
	if (World && OwnerEnemy)
	{
		for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
		{
			AHellunaHeroCharacter* Player = *It;
			if (!IsValid(Player)) continue;

			const float Dist = FVector::Dist2D(Center, Player->GetActorLocation());
			if (Dist > VortexRadius) continue;

			// 데미지
			if (EndExplosionDamage > 0.f)
			{
				UGameplayStatics::ApplyDamage(
					Player, EndExplosionDamage,
					OwnerEnemy->GetController(), OwnerEnemy,
					UDamageType::StaticClass()
				);
			}

			// 넉백 — 중심에서 바깥으로 밀어냄
			if (EndKnockbackStrength > 0.f || EndKnockbackUpward > 0.f)
			{
				const FVector KnockbackDir = (Player->GetActorLocation() - Center).GetSafeNormal2D();
				const FVector KnockbackVel = KnockbackDir * EndKnockbackStrength
					+ FVector(0.f, 0.f, EndKnockbackUpward);
				Player->LaunchCharacter(KnockbackVel, true, true);

				GV_LOG("Knockback [%s]: vel=(%.0f, %.0f, %.0f)",
					*Player->GetName(), KnockbackVel.X, KnockbackVel.Y, KnockbackVel.Z);
			}
		}
	}

	Orbitals.Empty();
	NotifyPatternFinished(false);
}

#undef GV_LOG
