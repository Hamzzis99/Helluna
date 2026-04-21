// Capstone Project Helluna

#include "BossEvent/CelestialBarrageZone.h"

#include "Character/HellunaEnemyCharacter.h"
#include "Character/HellunaHeroCharacter.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

#define CB_LOG(Fmt, ...) UE_LOG(LogTemp, Warning, TEXT("[CelestialBarrage] " Fmt), ##__VA_ARGS__)

ACelestialBarrageZone::ACelestialBarrageZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
}

// -----------------------------------------------------------------
// ActivateZone
// -----------------------------------------------------------------
void ACelestialBarrageZone::ActivateZone()
{
	Super::ActivateZone();

	if (bZoneActive) return;
	bZoneActive = true;
	ElapsedTime = 0.f;
	CurrentPhase = 0;
	RotationDirection = 1.f;
	Beams.Empty();
	PendingLightnings.Empty();

	CB_LOG("=== ActivateZone (Duration=%.1f) ===", PatternDuration);

	// 포탈 VFX (하늘에 열리는 포탈 — 보스 위 높이에 스폰)
	if (PortalVFX && OwnerEnemy)
	{
		const FVector PortalLoc = GetActorLocation() + FVector(0.f, 0.f, 1500.f);
		OwnerEnemy->MulticastPlayEffect(PortalLoc, PortalVFX, PortalVFXScale, false);
	}

	// Phase 1 즉시 시작
	UpdatePhase();

	// 패턴 종료 타이머
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PatternEndTimerHandle, this,
			&ACelestialBarrageZone::OnPatternTimeUp,
			PatternDuration, false
		);
	}

	SetActorTickEnabled(true);
}

// -----------------------------------------------------------------
// DeactivateZone
// -----------------------------------------------------------------
void ACelestialBarrageZone::DeactivateZone()
{
	Super::DeactivateZone();

	bZoneActive = false;
	SetActorTickEnabled(false);
	CurrentPhase = 0;
	Beams.Empty();
	PendingLightnings.Empty();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LightningTimerHandle);
		World->GetTimerManager().ClearTimer(PatternEndTimerHandle);
	}
}

// -----------------------------------------------------------------
// Tick
// -----------------------------------------------------------------
void ACelestialBarrageZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 게임플레이 로직(데미지/VFX 트리거)은 서버에서만. 클라는 멀티캐스트 수신만.
	if (!HasAuthority()) return;

	if (!bZoneActive) return;

	ElapsedTime += DeltaTime;

	UpdatePhase();
	UpdateBeamRotation(DeltaTime);
	ProcessBeamHits();
	ProcessBeamIntersections();
	ProcessPendingLightnings();
}

// -----------------------------------------------------------------
// UpdatePhase — 페이즈 전환
// -----------------------------------------------------------------
void ACelestialBarrageZone::UpdatePhase()
{
	int32 NewPhase;
	if (ElapsedTime < Phase1Duration)
	{
		NewPhase = 1;
	}
	else if (ElapsedTime < Phase1Duration + Phase2Duration)
	{
		NewPhase = 2;
	}
	else
	{
		NewPhase = 3;
	}

	if (NewPhase == CurrentPhase) return;

	CurrentPhase = NewPhase;
	CB_LOG("=== Phase %d START (t=%.1f) ===", CurrentPhase, ElapsedTime);

	// 레이저 수 업데이트
	int32 BeamCount;
	switch (CurrentPhase)
	{
	case 1: BeamCount = 1; CurrentRotationSpeed = Phase1RotationSpeed; break;
	case 2: BeamCount = 2; CurrentRotationSpeed = Phase2RotationSpeed; break;
	case 3: BeamCount = 4; CurrentRotationSpeed = Phase3RotationSpeed; break;
	default: BeamCount = 1; CurrentRotationSpeed = Phase1RotationSpeed; break;
	}

	// 기존 빔의 각도 보존 + 새 빔 추가 (균등 분배)
	const float FirstBeamAngle = Beams.Num() > 0 ? Beams[0].AngleDeg : 0.f;
	Beams.Empty();
	Beams.SetNum(BeamCount);
	for (int32 i = 0; i < BeamCount; ++i)
	{
		Beams[i].AngleDeg = FirstBeamAngle + (360.f * static_cast<float>(i) / static_cast<float>(BeamCount));
		if (Beams[i].AngleDeg >= 360.f) Beams[i].AngleDeg -= 360.f;
	}

	// 페이즈 전환 VFX/사운드
	if (PhaseTransitionSound && OwnerEnemy)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(PhaseTransitionSound, GetActorLocation());
	}

	// 빔 VFX (Phase 전환 시 갱신 — fire-and-forget, BP에서 빔 수에 맞게 설정)
	if (BeamVFX && OwnerEnemy)
	{
		OwnerEnemy->MulticastPlayEffect(GetActorLocation(), BeamVFX, BeamVFXScale, false);
	}

	// 빔 사운드
	if (BeamSound && OwnerEnemy && CurrentPhase == 1)
	{
		OwnerEnemy->Multicast_PlaySoundAtLocation(BeamSound, GetActorLocation());
	}

	// 번개 타이머 (Phase 2부터)
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(LightningTimerHandle);

		float LightningInterval = 0.f;
		if (CurrentPhase == 2) LightningInterval = Phase2LightningInterval;
		else if (CurrentPhase == 3) LightningInterval = Phase3LightningInterval;

		if (LightningInterval > 0.f)
		{
			World->GetTimerManager().SetTimer(
				LightningTimerHandle, this,
				&ACelestialBarrageZone::SpawnLightning,
				LightningInterval, true,
				0.5f // 첫 번개까지 약간의 딜레이
			);
		}
	}
}

// -----------------------------------------------------------------
// UpdateBeamRotation — 회전 + 방향 전환
// -----------------------------------------------------------------
void ACelestialBarrageZone::UpdateBeamRotation(float DeltaTime)
{
	// 방향 전환
	if (DirectionChangePeriod > 0.f)
	{
		// 주기마다 방향 전환 (삼각 함수로 부드러운 가감속)
		const float PhaseInPeriod = FMath::Fmod(ElapsedTime, DirectionChangePeriod) / DirectionChangePeriod;
		// 전반부: 정방향, 후반부: 역방향
		RotationDirection = PhaseInPeriod < 0.5f ? 1.f : -1.f;

		// 전환 구간에서 가감속 (전환 직전/직후 10%에서 감속)
		const float TransitionZone = 0.1f;
		float SpeedFactor = 1.f;
		if (PhaseInPeriod > 0.5f - TransitionZone && PhaseInPeriod < 0.5f + TransitionZone)
		{
			// 전환 구간: 감속 → 가속
			const float Dist = FMath::Abs(PhaseInPeriod - 0.5f) / TransitionZone;
			SpeedFactor = FMath::Lerp(0.2f, 1.f, Dist); // 전환점에서 20%까지 감속
		}

		const float DeltaAngle = CurrentRotationSpeed * RotationDirection * SpeedFactor * DeltaTime;
		for (FBeamState& Beam : Beams)
		{
			Beam.AngleDeg += DeltaAngle;
			if (Beam.AngleDeg >= 360.f) Beam.AngleDeg -= 360.f;
			if (Beam.AngleDeg < 0.f) Beam.AngleDeg += 360.f;
		}
	}
	else
	{
		// 단방향 회전
		const float DeltaAngle = CurrentRotationSpeed * DeltaTime;
		for (FBeamState& Beam : Beams)
		{
			Beam.AngleDeg += DeltaAngle;
			if (Beam.AngleDeg >= 360.f) Beam.AngleDeg -= 360.f;
		}
	}
}

// -----------------------------------------------------------------
// ProcessBeamHits — 직선-점 거리 기반 판정
// -----------------------------------------------------------------
void ACelestialBarrageZone::ProcessBeamHits()
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const double CurrentTime = World->GetTimeSeconds();

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Player = *It;
		if (!IsValid(Player)) continue;

		const FVector PlayerLoc = Player->GetActorLocation();
		TWeakObjectPtr<AActor> WeakPlayer(Player);

		for (FBeamState& Beam : Beams)
		{
			if (!IsPointInBeam(PlayerLoc, Beam)) continue;

			// 쿨타임 체크
			const double* LastHit = Beam.LastHitTime.Find(WeakPlayer);
			if (LastHit && (CurrentTime - *LastHit) < BeamHitCooldown) continue;

			Beam.LastHitTime.FindOrAdd(WeakPlayer) = CurrentTime;

			UGameplayStatics::ApplyDamage(
				Player, BeamDamage,
				OwnerEnemy->GetController(), OwnerEnemy,
				UDamageType::StaticClass()
			);

			CB_LOG("Beam hit [%s] (angle=%.0f)", *Player->GetName(), Beam.AngleDeg);
		}
	}
}

// -----------------------------------------------------------------
// IsPointInBeam — 직선 판정 (중심에서 BeamLength까지의 직선)
// -----------------------------------------------------------------
bool ACelestialBarrageZone::IsPointInBeam(const FVector& Point, const FBeamState& Beam) const
{
	const FVector Center = GetActorLocation();

	// 2D 거리 (높이 무시)
	const FVector2D PointXY(Point.X, Point.Y);
	const FVector2D CenterXY(Center.X, Center.Y);

	const float DistFromCenter = FVector2D::Distance(CenterXY, PointXY);

	// 내부 안전 반지름 체크
	if (DistFromCenter < BeamInnerRadius) return false;
	// 빔 길이 밖이면 무시
	if (DistFromCenter > BeamLength) return false;

	// 높이 체크 (빔의 높이 허용 범위)
	const float HeightDiff = FMath::Abs(Point.Z - Center.Z);
	if (HeightDiff > 400.f) return false; // 넉넉한 높이 허용

	// 빔 방향 벡터
	const float AngleRad = FMath::DegreesToRadians(Beam.AngleDeg);
	const FVector2D BeamDir(FMath::Cos(AngleRad), FMath::Sin(AngleRad));

	// 중심→포인트 벡터
	const FVector2D ToPoint = PointXY - CenterXY;

	// 빔 방향에 대한 수직 거리 (직선-점 거리)
	// Cross product: |ToPoint × BeamDir| = 수직 거리
	const float PerpDist = FMath::Abs(ToPoint.X * BeamDir.Y - ToPoint.Y * BeamDir.X);

	// 빔 폭의 절반 이내면 판정
	if (PerpDist > BeamWidth * 0.5f) return false;

	// 빔이 한 방향으로만 뻗으므로, 내적이 양수인지 확인
	const float DotProduct = FVector2D::DotProduct(ToPoint, BeamDir);
	if (DotProduct < 0.f) return false;

	return true;
}

// -----------------------------------------------------------------
// SpawnLightning — 번개 경고 생성
// -----------------------------------------------------------------
void ACelestialBarrageZone::SpawnLightning()
{
	if (!bZoneActive || !OwnerEnemy) return;

	UWorld* World = GetWorld();
	if (!World) return;

	const FVector Center = GetActorLocation();
	FVector TargetLoc = Center;

	// 플레이어 추적 / 랜덤
	TArray<AHellunaHeroCharacter*> Players;
	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		if (IsValid(*It)) Players.Add(*It);
	}

	const bool bTargetPlayer = Players.Num() > 0 && FMath::FRand() < LightningPlayerTargetRatio;

	if (bTargetPlayer)
	{
		const int32 Idx = FMath::RandRange(0, Players.Num() - 1);
		TargetLoc = Players[Idx]->GetActorLocation();
		// 약간의 오프셋
		TargetLoc += FVector(FMath::FRandRange(-100.f, 100.f), FMath::FRandRange(-100.f, 100.f), 0.f);
	}
	else
	{
		const float Angle = FMath::FRandRange(0.f, 360.f);
		const float Dist = FMath::FRandRange(LightningPatternRadius * 0.2f, LightningPatternRadius);
		TargetLoc = Center + FVector(
			FMath::Cos(FMath::DegreesToRadians(Angle)) * Dist,
			FMath::Sin(FMath::DegreesToRadians(Angle)) * Dist, 0.f
		);
	}

	// 경고 VFX
	if (LightningWarningVFX)
	{
		OwnerEnemy->MulticastPlayEffect(TargetLoc, LightningWarningVFX, LightningWarningVFXScale, false);
	}

	// 낙뢰 예약
	FPendingLightning PL;
	PL.Location = TargetLoc;
	PL.StrikeWorldTime = World->GetTimeSeconds() + LightningWarningDuration;
	PendingLightnings.Add(MoveTemp(PL));
}

// -----------------------------------------------------------------
// ProcessPendingLightnings — 번개 착탄
// -----------------------------------------------------------------
void ACelestialBarrageZone::ProcessPendingLightnings()
{
	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const double CurrentTime = World->GetTimeSeconds();

	for (int32 i = PendingLightnings.Num() - 1; i >= 0; --i)
	{
		if (CurrentTime < PendingLightnings[i].StrikeWorldTime) continue;

		const FVector& Loc = PendingLightnings[i].Location;

		// 번개 VFX
		if (LightningStrikeVFX)
		{
			OwnerEnemy->MulticastPlayEffect(Loc, LightningStrikeVFX, LightningStrikeVFXScale, false);
		}

		// 사운드
		if (LightningSound)
		{
			OwnerEnemy->Multicast_PlaySoundAtLocation(LightningSound, Loc);
		}

		// 데미지 (Overlap)
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);
		QueryParams.AddIgnoredActor(OwnerEnemy);

		World->OverlapMultiByObjectType(
			Overlaps, Loc, FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(LightningRadius), QueryParams
		);

		TSet<AActor*> Hit;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Overlap.GetActor());
			if (!IsValid(Player) || Hit.Contains(Player)) continue;
			Hit.Add(Player);

			UGameplayStatics::ApplyDamage(
				Player, LightningDamage,
				OwnerEnemy->GetController(), OwnerEnemy,
				UDamageType::StaticClass()
			);
			CB_LOG("Lightning hit [%s] for %.0f", *Player->GetName(), LightningDamage);
		}

		PendingLightnings.RemoveAt(i);
	}
}

// -----------------------------------------------------------------
// ProcessBeamIntersections — 빔 교차점 폭발
// -----------------------------------------------------------------
void ACelestialBarrageZone::ProcessBeamIntersections()
{
	// Phase 1에서는 빔 1개이므로 교차 없음
	if (CurrentPhase < 2 || Beams.Num() < 2) return;
	if (IntersectionDamage <= 0.f) return;

	UWorld* World = GetWorld();
	if (!World || !OwnerEnemy) return;

	const double CurrentTime = World->GetTimeSeconds();
	if ((CurrentTime - LastIntersectionTime) < IntersectionInterval) return;

	const FVector Center = GetActorLocation();

	// 빔은 중심에서 뻗어나가므로 교차점은 없지만,
	// "빔 끝점 근처"에서 에너지 폭발이 발생하는 것으로 구현.
	// 빔 끝점들을 계산하고, 인접 빔 끝점 사이의 중점에서 폭발.
	for (int32 i = 0; i < Beams.Num(); ++i)
	{
		const int32 Next = (i + 1) % Beams.Num();

		const float AngleA = FMath::DegreesToRadians(Beams[i].AngleDeg);
		const float AngleB = FMath::DegreesToRadians(Beams[Next].AngleDeg);

		// 빔 끝점의 중간 거리 (BeamLength의 60% 지점)
		const float MidDist = BeamLength * 0.6f;

		const FVector EndA = Center + FVector(FMath::Cos(AngleA), FMath::Sin(AngleA), 0.f) * MidDist;
		const FVector EndB = Center + FVector(FMath::Cos(AngleB), FMath::Sin(AngleB), 0.f) * MidDist;
		const FVector MidPoint = (EndA + EndB) * 0.5f;

		// VFX
		if (IntersectionVFX)
		{
			OwnerEnemy->MulticastPlayEffect(MidPoint, IntersectionVFX, IntersectionVFXScale, false);
		}

		// 범위 데미지
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);
		QueryParams.AddIgnoredActor(OwnerEnemy);

		World->OverlapMultiByObjectType(
			Overlaps, MidPoint, FQuat::Identity,
			FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(IntersectionRadius),
			QueryParams
		);

		TSet<AActor*> Hit;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AHellunaHeroCharacter* Player = Cast<AHellunaHeroCharacter>(Overlap.GetActor());
			if (!IsValid(Player) || Hit.Contains(Player)) continue;
			Hit.Add(Player);

			UGameplayStatics::ApplyDamage(
				Player, IntersectionDamage,
				OwnerEnemy->GetController(), OwnerEnemy,
				UDamageType::StaticClass()
			);
			CB_LOG("Intersection hit [%s]", *Player->GetName());
		}
	}

	LastIntersectionTime = CurrentTime;
}

// -----------------------------------------------------------------
// OnPatternTimeUp
// -----------------------------------------------------------------
void ACelestialBarrageZone::OnPatternTimeUp()
{
	CB_LOG("=== Pattern time up ===");

	bZoneActive = false;
	SetActorTickEnabled(false);
	Beams.Empty();
	PendingLightnings.Empty();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LightningTimerHandle);
	}

	NotifyPatternFinished(false);
}

#undef CB_LOG
