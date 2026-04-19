// Capstone Project Helluna

#include "BossEvent/ArcaneSigilZone.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

#define AS_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[ArcaneSigil] " Fmt), ##__VA_ARGS__)

AArcaneSigilZone::AArcaneSigilZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
}

// -----------------------------------------------------------------
// ActivateZone
// -----------------------------------------------------------------
void AArcaneSigilZone::ActivateZone()
{
	Super::ActivateZone();

	if (bZoneActive) return;
	bZoneActive = true;
	ElapsedTime = 0.f;
	TimeSinceLastLock = 0.f;
	CurrentLockCount = 0;
	bLockWarningActive = false;
	bIsLockWarning = false;
	DangerDamageLastTime.Empty();
	OutsideDamageLastTime.Empty();

	AS_LOG("=== ActivateZone (Rings=%d, Sectors=%d, LockInterval=%.1f) ===",
		RingCount, BaseSectorCount, LockInterval);

	InitializeRings();

	// 마법진 VFX
	if (SigilVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), SigilVFX, SigilVFXScale, false);
	}

	// 회전 사운드
	if (RotationSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(RotationSound, GetActorLocation());
	}

	SetActorTickEnabled(true);
}

// -----------------------------------------------------------------
// DeactivateZone
// -----------------------------------------------------------------
void AArcaneSigilZone::DeactivateZone()
{
	Super::DeactivateZone();

	bZoneActive = false;
	bLockWarningActive = false;
	bIsLockWarning = false;
	SetActorTickEnabled(false);
	Rings.Empty();
	DangerDamageLastTime.Empty();
	OutsideDamageLastTime.Empty();
}

// -----------------------------------------------------------------
// InitializeRings — 링 초기 구성
// -----------------------------------------------------------------
void AArcaneSigilZone::InitializeRings()
{
	Rings.Empty();
	Rings.SetNum(RingCount);

	const float TotalWidth = OuterRadius - InnerSafeRadius;
	const float RingWidth = TotalWidth / static_cast<float>(RingCount);

	for (int32 i = 0; i < RingCount; ++i)
	{
		FRingState& Ring = Rings[i];
		Ring.InnerRadius = InnerSafeRadius + RingWidth * static_cast<float>(i);
		Ring.OuterRadius = Ring.InnerRadius + RingWidth;
		Ring.SectorCount = BaseSectorCount;
		Ring.CurrentAngleOffset = FMath::FRandRange(0.f, 360.f); // 초기 랜덤 각도

		// 링마다 회전 속도와 방향을 다르게 — 홀수 링은 역방향
		const float SpeedMultiplier = 1.f + 0.3f * static_cast<float>(i); // 바깥 링일수록 빠름
		const float Direction = (i % 2 == 0) ? 1.f : -1.f;
		Ring.RotationSpeed = BaseRotationSpeed * SpeedMultiplier * Direction;

		Ring.CurrentSafeRatio = SafeSectorRatio;

		AS_LOG("Ring %d: r=[%.0f, %.0f], sectors=%d, speed=%.1f",
			i, Ring.InnerRadius, Ring.OuterRadius, Ring.SectorCount, Ring.RotationSpeed);
	}
}

// -----------------------------------------------------------------
// Tick
// -----------------------------------------------------------------
void AArcaneSigilZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 게임플레이 로직(데미지/VFX 트리거)은 서버에서만. 클라는 멀티캐스트 수신만.
	if (!HasAuthority()) return;

	if (!bZoneActive) return;

	ElapsedTime += DeltaTime;

	// 봉인 경고 중이면 회전 멈춤
	if (bLockWarningActive)
	{
		UWorld* World = GetWorld();
		if (World && (World->GetTimeSeconds() - LockWarningStartTime) >= LockWarningDuration)
		{
			// 경고 종료 → 폭발
			if (CurrentLockCount >= MaxLockCount - 1)
			{
				ExecuteFinalLock();
			}
			else
			{
				ExecuteLock();
			}
		}
	}
	else
	{
		// 정상 회전
		UpdateRotation(DeltaTime);

		// 봉인 타이머
		TimeSinceLastLock += DeltaTime;
		if (TimeSinceLastLock >= LockInterval)
		{
			BeginLockWarning();
		}
	}

	// 섹터 데미지 (봉인 경고 중에도 적용)
	ProcessSectorDamage(DeltaTime);
}

// -----------------------------------------------------------------
// UpdateRotation — 각 링 독립 회전
// -----------------------------------------------------------------
void AArcaneSigilZone::UpdateRotation(float DeltaTime)
{
	for (FRingState& Ring : Rings)
	{
		Ring.CurrentAngleOffset += Ring.RotationSpeed * DeltaTime;
		Ring.CurrentAngleOffset = NormalizeAngle(Ring.CurrentAngleOffset);
	}
}

// -----------------------------------------------------------------
// ProcessSectorDamage — 위험 섹터 / 외부 DPS
// -----------------------------------------------------------------
void AArcaneSigilZone::ProcessSectorDamage(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const double CurrentTime = World->GetTimeSeconds();
	const float DPSInterval = 0.25f; // 0.25초마다 데미지 적용

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		const FVector PlayerLoc = Player->GetActorLocation();
		TWeakObjectPtr<AActor> WeakPlayer(Player);

		// 높이 체크
		const float HeightDiff = FMath::Abs(PlayerLoc.Z - GetActorLocation().Z);
		if (HeightDiff > 500.f) continue;

		// 외부 영역 체크
		if (IsOutsideSigil(PlayerLoc))
		{
			const double* LastTime = OutsideDamageLastTime.Find(WeakPlayer);
			if (!LastTime || (CurrentTime - *LastTime) >= DPSInterval)
			{
				OutsideDamageLastTime.FindOrAdd(WeakPlayer) = CurrentTime;

				UGameplayStatics::ApplyDamage(
					Player, OutsideDPS * DPSInterval,
					OwnerEnemy->GetController(), OwnerEnemy,
					UDamageType::StaticClass()
				);
			}
			continue;
		}

		// 위험 섹터 체크
		int32 RingIdx = -1;
		if (IsInDangerSector(PlayerLoc, RingIdx))
		{
			const double* LastTime = DangerDamageLastTime.Find(WeakPlayer);
			if (!LastTime || (CurrentTime - *LastTime) >= DPSInterval)
			{
				DangerDamageLastTime.FindOrAdd(WeakPlayer) = CurrentTime;

				UGameplayStatics::ApplyDamage(
					Player, DangerDPS * DPSInterval,
					OwnerEnemy->GetController(), OwnerEnemy,
					UDamageType::StaticClass()
				);
			}
		}
	}
}

// -----------------------------------------------------------------
// BeginLockWarning — 봉인 경고 시작 (회전 멈춤)
// -----------------------------------------------------------------
void AArcaneSigilZone::BeginLockWarning()
{
	UWorld* World = GetWorld();
	if (!World) return;

	bLockWarningActive = true;
	bIsLockWarning = true;
	LockWarningStartTime = World->GetTimeSeconds();
	TimeSinceLastLock = 0.f;

	AS_LOG("=== LOCK WARNING %d/%d ===", CurrentLockCount + 1, MaxLockCount);

	// 경고 VFX
	if (LockWarningVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), LockWarningVFX, LockWarningVFXScale, false);
	}

	// 경고 사운드
	if (LockWarningSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(LockWarningSound, GetActorLocation());
	}
}

// -----------------------------------------------------------------
// ExecuteLock — 봉인 폭발
// -----------------------------------------------------------------
void AArcaneSigilZone::ExecuteLock()
{
	AS_LOG("=== LOCK EXPLOSION %d ===", CurrentLockCount + 1);

	bLockWarningActive = false;
	bIsLockWarning = false;

	// 위험 섹터에 있는 플레이어에게 폭발 데미지
	UWorld* World = GetWorld();
	if (World && OwnerEnemy)
	{
		for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
		{
			AHellunaHeroCharacter* Player = *It;
			if (!IsValid(Player)) continue;

			int32 RingIdx = -1;
			if (IsInDangerSector(Player->GetActorLocation(), RingIdx))
			{
				UGameplayStatics::ApplyDamage(
					Player, LockExplosionDamage,
					OwnerEnemy->GetController(), OwnerEnemy,
					UDamageType::StaticClass()
				);
				AS_LOG("Lock hit [%s] in ring %d", *Player->GetName(), RingIdx);
			}

			// 외부에 있어도 데미지
			if (IsOutsideSigil(Player->GetActorLocation()))
			{
				UGameplayStatics::ApplyDamage(
					Player, LockExplosionDamage * 0.5f,
					OwnerEnemy->GetController(), OwnerEnemy,
					UDamageType::StaticClass()
				);
			}
		}
	}

	// 폭발 VFX — 각 링의 위험 섹터 중심에서
	if (SectorExplodeVFX && OwnerEnemy)
	{
		for (int32 i = 0; i < Rings.Num(); ++i)
		{
			TArray<FVector> Centers = GetDangerSectorCenters(i);
			for (const FVector& Center : Centers)
			{
				OwnerEnemy->MulticastPlayEffect(Center, SectorExplodeVFX, SectorExplodeVFXScale, false);
			}
		}
	}

	// 폭발 사운드
	if (ExplosionSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(ExplosionSound, GetActorLocation());
	}

	CurrentLockCount++;

	// 에스컬레이션
	EscalateRings();
}

// -----------------------------------------------------------------
// ExecuteFinalLock — 최종 봉인 (중심만 안전)
// -----------------------------------------------------------------
void AArcaneSigilZone::ExecuteFinalLock()
{
	AS_LOG("=== FINAL LOCK ===");

	bLockWarningActive = false;
	bIsLockWarning = false;

	// 중심 안전 반지름 밖의 모든 플레이어에게 데미지
	UWorld* World = GetWorld();
	if (World && OwnerEnemy)
	{
		const FVector Center = GetActorLocation();

		for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
		{
			AHellunaHeroCharacter* Player = *It;
			if (!IsValid(Player)) continue;

			const float Dist2D = FVector::Dist2D(Center, Player->GetActorLocation());

			if (Dist2D > InnerSafeRadius)
			{
				UGameplayStatics::ApplyDamage(
					Player, FinalExplosionDamage,
					OwnerEnemy->GetController(), OwnerEnemy,
					UDamageType::StaticClass()
				);
				AS_LOG("Final lock hit [%s] at dist=%.0f", *Player->GetName(), Dist2D);
			}
		}
	}

	// 최종 VFX
	if (FinalSigilVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), FinalSigilVFX, FinalSigilVFXScale, false);
	}

	// 사운드
	if (ExplosionSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(ExplosionSound, GetActorLocation());
	}

	// 패턴 종료
	bZoneActive = false;
	SetActorTickEnabled(false);
	NotifyPatternFinished(false);
}

// -----------------------------------------------------------------
// EscalateRings — 봉인 후 난이도 증가
// -----------------------------------------------------------------
void AArcaneSigilZone::EscalateRings()
{
	for (FRingState& Ring : Rings)
	{
		// 회전 속도 증가
		Ring.RotationSpeed *= SpeedMultiplierPerLock;

		// 안전 비율 감소
		Ring.CurrentSafeRatio = FMath::Max(0.15f, Ring.CurrentSafeRatio - SafeRatioDecayPerLock);
	}

	AS_LOG("Escalated: speed x%.2f, safe ratio -%.2f (now=%.2f)",
		SpeedMultiplierPerLock, SafeRatioDecayPerLock,
		Rings.Num() > 0 ? Rings[0].CurrentSafeRatio : 0.f);
}

// -----------------------------------------------------------------
// IsInDangerSector — 섹터 기반 판정
// -----------------------------------------------------------------
bool AArcaneSigilZone::IsInDangerSector(const FVector& WorldPos, int32& OutRingIndex) const
{
	const FVector Center = GetActorLocation();
	const float Dist2D = FVector::Dist2D(Center, WorldPos);

	// 중심 안전 지대
	if (Dist2D <= InnerSafeRadius)
	{
		OutRingIndex = -1;
		return false;
	}

	// 어떤 링에 속하는지 찾기
	for (int32 i = 0; i < Rings.Num(); ++i)
	{
		const FRingState& Ring = Rings[i];
		if (Dist2D >= Ring.InnerRadius && Dist2D <= Ring.OuterRadius)
		{
			OutRingIndex = i;

			// 플레이어의 각도 계산 (중심 기준)
			const FVector2D Diff(WorldPos.X - Center.X, WorldPos.Y - Center.Y);
			float PlayerAngle = FMath::RadiansToDegrees(FMath::Atan2(Diff.Y, Diff.X));
			PlayerAngle = NormalizeAngle(PlayerAngle);

			// 링의 회전 오프셋 적용
			float LocalAngle = NormalizeAngle(PlayerAngle - Ring.CurrentAngleOffset);

			// 섹터 판정
			// 안전 섹터와 위험 섹터가 교대로 배치
			// 안전 섹터 각도 = 360 / SectorCount * SafeRatio
			// 위험 섹터 각도 = 360 / SectorCount * (1 - SafeRatio)
			const float TotalSectorAngle = 360.f / static_cast<float>(Ring.SectorCount);
			const float SafeAngle = TotalSectorAngle * Ring.CurrentSafeRatio;

			// 현재 섹터 내에서의 위치
			const float PosInSector = FMath::Fmod(LocalAngle, TotalSectorAngle);

			// 앞부분이 안전, 뒷부분이 위험
			return PosInSector >= SafeAngle;
		}
	}

	// 어떤 링에도 속하지 않음 (링 사이 갭이나 외부)
	OutRingIndex = -1;
	return false;
}

// -----------------------------------------------------------------
// IsOutsideSigil
// -----------------------------------------------------------------
bool AArcaneSigilZone::IsOutsideSigil(const FVector& WorldPos) const
{
	const float Dist2D = FVector::Dist2D(GetActorLocation(), WorldPos);
	return Dist2D > OuterRadius;
}

// -----------------------------------------------------------------
// GetDangerSectorCenters — VFX 배치용
// -----------------------------------------------------------------
TArray<FVector> AArcaneSigilZone::GetDangerSectorCenters(int32 RingIndex) const
{
	TArray<FVector> Centers;

	if (RingIndex < 0 || RingIndex >= Rings.Num()) return Centers;

	const FRingState& Ring = Rings[RingIndex];
	const FVector ZoneCenter = GetActorLocation();
	const float MidRadius = (Ring.InnerRadius + Ring.OuterRadius) * 0.5f;
	const float TotalSectorAngle = 360.f / static_cast<float>(Ring.SectorCount);
	const float SafeAngle = TotalSectorAngle * Ring.CurrentSafeRatio;

	for (int32 s = 0; s < Ring.SectorCount; ++s)
	{
		// 위험 섹터 중심 각도 = 섹터 시작 + SafeAngle + (위험 각도 / 2) + 링 오프셋
		const float SectorStart = TotalSectorAngle * static_cast<float>(s);
		const float DangerCenter = SectorStart + SafeAngle + (TotalSectorAngle - SafeAngle) * 0.5f;
		const float WorldAngle = NormalizeAngle(DangerCenter + Ring.CurrentAngleOffset);

		const float Rad = FMath::DegreesToRadians(WorldAngle);
		const FVector Pos = ZoneCenter + FVector(
			FMath::Cos(Rad) * MidRadius,
			FMath::Sin(Rad) * MidRadius,
			0.f
		);
		Centers.Add(Pos);
	}

	return Centers;
}

// -----------------------------------------------------------------
// NormalizeAngle
// -----------------------------------------------------------------
float AArcaneSigilZone::NormalizeAngle(float Angle)
{
	Angle = FMath::Fmod(Angle, 360.f);
	if (Angle < 0.f) Angle += 360.f;
	return Angle;
}

#undef AS_LOG
