// Capstone Project Helluna

#include "BossEvent/KaleidoscopeArsenalZone.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

AKaleidoscopeArsenalZone::AKaleidoscopeArsenalZone()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AKaleidoscopeArsenalZone::ActivateZone()
{
	if (!OwnerEnemy) { NotifyPatternFinished(false); return; }

	bZoneActive = true;
	CurrentMirrorAngle = 0.f;
	SeedAccumulator = 0.f;
	ActiveBeams.Empty();

	SetActorTickEnabled(true);

	// 최초 시드 빔 1개 즉시 발사
	FireSeedBeam();

	// 패턴 지속시간 타이머
	FTimerHandle EndHandle;
	GetWorldTimerManager().SetTimer(EndHandle, [this]()
	{
		DeactivateZone();
	}, PatternDuration, false);
}

void AKaleidoscopeArsenalZone::DeactivateZone()
{
	if (!bZoneActive) return;
	bZoneActive = false;
	SetActorTickEnabled(false);
	ActiveBeams.Empty();
	NotifyPatternFinished(false);
}

void AKaleidoscopeArsenalZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bZoneActive) return;

	// 만화경 회전
	CurrentMirrorAngle += MirrorSpinSpeed * DeltaTime;
	if (CurrentMirrorAngle > 360.f) CurrentMirrorAngle -= 360.f;

	// 시드 빔 주기 발사
	SeedAccumulator += DeltaTime;
	if (SeedAccumulator >= SeedInterval)
	{
		SeedAccumulator = 0.f;
		FireSeedBeam();
	}

	ProcessDamage();
	CleanupBeams();
}

FVector AKaleidoscopeArsenalZone::GetMirrorNormal(int32 MirrorIdx) const
{
	// 거울은 보스 중심을 지나는 수직 평면. 평면마다 법선이 있다.
	const float AngleStep = 180.f / FMath::Max(1, MirrorCount);
	const float AngleDeg = CurrentMirrorAngle + MirrorIdx * AngleStep + 90.f;
	const float Rad = FMath::DegreesToRadians(AngleDeg);
	return FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.f);
}

bool AKaleidoscopeArsenalZone::IntersectMirrors(const FVector& Origin, const FVector& Direction,
                                                float MaxLen, FVector& OutIntersect, FVector& OutNormal,
                                                int32 IgnoreMirrorIdx, int32& OutHitMirrorIdx) const
{
	const FVector Center = GetActorLocation();
	float BestT = FLT_MAX;
	int32 BestMirror = -1;
	FVector BestNormal = FVector::ZeroVector;

	for (int32 m = 0; m < MirrorCount; ++m)
	{
		if (m == IgnoreMirrorIdx) continue;
		const FVector N = GetMirrorNormal(m);
		const float DenomN = FVector::DotProduct(Direction, N);
		if (FMath::Abs(DenomN) < KINDA_SMALL_NUMBER) continue;

		// 평면: (P - Center) · N = 0
		const float T = FVector::DotProduct(Center - Origin, N) / DenomN;
		if (T <= KINDA_SMALL_NUMBER || T > MaxLen) continue;

		const FVector HitPoint = Origin + Direction * T;
		// 거울은 유한 반지름. 중심에서의 거리 체크.
		const float DistToCenter = FVector::Dist2D(HitPoint, Center);
		if (DistToCenter > MirrorRadius) continue;

		if (T < BestT)
		{
			BestT = T;
			BestMirror = m;
			BestNormal = N;
		}
	}

	if (BestMirror >= 0)
	{
		OutIntersect = Origin + Direction * BestT;
		OutNormal = BestNormal;
		OutHitMirrorIdx = BestMirror;
		return true;
	}
	return false;
}

void AKaleidoscopeArsenalZone::TraceReflectedBeam(const FVector& Origin, const FVector& Direction,
                                                  TArray<FBeamSegment>& OutSegments) const
{
	FVector CurOrigin = Origin;
	FVector CurDir = Direction.GetSafeNormal();
	int32 LastMirror = -1;

	for (int32 Bounce = 0; Bounce <= BounceDepth; ++Bounce)
	{
		FVector HitPoint, HitNormal;
		int32 HitMirror = -1;
		const bool bHit = IntersectMirrors(CurOrigin, CurDir, MaxSegmentLength,
		                                   HitPoint, HitNormal, LastMirror, HitMirror);

		if (!bHit)
		{
			// 그냥 뻗어나감
			FBeamSegment Seg;
			Seg.Start = CurOrigin;
			Seg.End = CurOrigin + CurDir * MaxSegmentLength;
			OutSegments.Add(Seg);
			break;
		}

		// 반사점까지 세그먼트 추가
		FBeamSegment Seg;
		Seg.Start = CurOrigin;
		Seg.End = HitPoint;
		OutSegments.Add(Seg);

		// 반사 벡터 계산: D' = D - 2 (D · N) N
		const float Dot = FVector::DotProduct(CurDir, HitNormal);
		CurDir = (CurDir - 2.f * Dot * HitNormal).GetSafeNormal();
		CurOrigin = HitPoint;
		LastMirror = HitMirror;

		// VFX는 서버 권한 쪽에서만 — BP에서 처리 가능하므로 생략
	}
}

void AKaleidoscopeArsenalZone::ApplySymmetryReplication(TArray<FBeamSegment>& Segments) const
{
	if (MirrorCount <= 1) return;

	const int32 OriginalCount = Segments.Num();
	const FVector Center = GetActorLocation();
	const int32 CopyCount = MirrorCount; // C_n 회전 대칭

	for (int32 c = 1; c < CopyCount; ++c)
	{
		const float AngleDeg = 360.f / CopyCount * c;
		const float Rad = FMath::DegreesToRadians(AngleDeg);
		const float CosA = FMath::Cos(Rad);
		const float SinA = FMath::Sin(Rad);

		for (int32 i = 0; i < OriginalCount; ++i)
		{
			const FBeamSegment& Src = Segments[i];
			auto Rotate2D = [&](const FVector& P) -> FVector
			{
				const FVector R = P - Center;
				const float NewX = R.X * CosA - R.Y * SinA;
				const float NewY = R.X * SinA + R.Y * CosA;
				return FVector(Center.X + NewX, Center.Y + NewY, P.Z);
			};
			FBeamSegment Out;
			Out.Start = Rotate2D(Src.Start);
			Out.End = Rotate2D(Src.End);
			Segments.Add(Out);
		}
	}
}

void AKaleidoscopeArsenalZone::FireSeedBeam()
{
	FActiveBeam NewBeam;
	NewBeam.SpawnWorldTime = GetWorld()->GetTimeSeconds();

	// 시드 방향은 랜덤
	const float SeedAngle = FMath::FRandRange(0.f, 360.f);
	const float SeedRad = FMath::DegreesToRadians(SeedAngle);
	const FVector Dir(FMath::Cos(SeedRad), FMath::Sin(SeedRad), 0.f);

	TraceReflectedBeam(GetActorLocation(), Dir, NewBeam.Segments);
	ApplySymmetryReplication(NewBeam.Segments);

	// VFX 스폰
	if (BeamVFX)
	{
		for (const FBeamSegment& Seg : NewBeam.Segments)
		{
			UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), BeamVFX, Seg.Start, FRotator::ZeroRotator,
				FVector(VFXScale), true, true, ENCPoolMethod::AutoRelease);
			if (NC)
			{
				NC->SetVariableVec3(TEXT("BeamStart"), Seg.Start);
				NC->SetVariableVec3(TEXT("BeamEnd"), Seg.End);
			}
		}
	}

	ActiveBeams.Add(MoveTemp(NewBeam));
}

float AKaleidoscopeArsenalZone::PointToSegmentDistSq2D(const FVector2D& P, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const FVector2D AP = P - A;
	const float LenSq = AB.SizeSquared();
	if (LenSq < KINDA_SMALL_NUMBER) return AP.SizeSquared();
	const float T = FMath::Clamp(FVector2D::DotProduct(AP, AB) / LenSq, 0.f, 1.f);
	const FVector2D Closest = A + AB * T;
	return (P - Closest).SizeSquared();
}

void AKaleidoscopeArsenalZone::ProcessDamage()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const double Now = World->GetTimeSeconds();
	const float WidthSq = BeamWidth * BeamWidth;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Hero = *It;
		if (!IsValid(Hero)) continue;

		const FVector HeroPos = Hero->GetActorLocation();
		const FVector2D HeroPos2D(HeroPos.X, HeroPos.Y);

		for (FActiveBeam& Beam : ActiveBeams)
		{
			double& LastTime = Beam.LastHitTime.FindOrAdd(Hero);
			if (Now - LastTime < HitCooldown) continue;

			for (const FBeamSegment& Seg : Beam.Segments)
			{
				const FVector2D A(Seg.Start.X, Seg.Start.Y);
				const FVector2D B(Seg.End.X, Seg.End.Y);
				if (PointToSegmentDistSq2D(HeroPos2D, A, B) <= WidthSq)
				{
					UGameplayStatics::ApplyDamage(
						Hero, BeamDamage,
						OwnerEnemy ? OwnerEnemy->GetController() : nullptr,
						OwnerEnemy, UDamageType::StaticClass());
					LastTime = Now;
					break;
				}
			}
		}
	}
}

void AKaleidoscopeArsenalZone::CleanupBeams()
{
	const double Now = GetWorld()->GetTimeSeconds();
	ActiveBeams.RemoveAll([this, Now](const FActiveBeam& B)
	{
		return (Now - B.SpawnWorldTime) > BeamLifetime;
	});
}
