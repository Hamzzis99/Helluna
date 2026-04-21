// Capstone Project Helluna

#include "BossEvent/NBodySingularityZone.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

ANBodySingularityZone::ANBodySingularityZone()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ANBodySingularityZone::ActivateZone()
{
	if (!OwnerEnemy) { NotifyPatternFinished(false); return; }

	bZoneActive = true;
	InitializeSingularities();
	SetActorTickEnabled(true);

	GetWorldTimerManager().SetTimer(PatternEndTimerHandle, [this]()
	{
		DeactivateZone();
	}, PatternDuration, false);
}

void ANBodySingularityZone::DeactivateZone()
{
	if (!bZoneActive) return;
	bZoneActive = false;
	SetActorTickEnabled(false);

	// VFX 컴포넌트 정리
	for (FSingularity& S : Singularities)
	{
		if (S.VFXComp.IsValid())
		{
			S.VFXComp->DestroyComponent();
		}
	}
	Singularities.Empty();

	GetWorldTimerManager().ClearTimer(PatternEndTimerHandle);
	NotifyPatternFinished(false);
}

void ANBodySingularityZone::InitializeSingularities()
{
	Singularities.Empty();
	Singularities.Reserve(SingularityCount);

	const FVector Center = GetActorLocation();

	for (int32 i = 0; i < SingularityCount; ++i)
	{
		const float Angle = (2.f * PI / SingularityCount) * i;
		const float Cos = FMath::Cos(Angle);
		const float Sin = FMath::Sin(Angle);

		FSingularity S;
		S.Position = Center + FVector(Cos * InitialRadius, Sin * InitialRadius, 150.f);
		// 접선 방향 (반시계): (-sin, cos)
		S.Velocity = FVector(-Sin * InitialTangentialSpeed, Cos * InitialTangentialSpeed, 0.f);
		S.Mass = Mass;
		S.bAlive = true;

		// VFX 스폰
		if (SingularityVFX)
		{
			UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), SingularityVFX, S.Position, FRotator::ZeroRotator,
				FVector(SingularityVFXScale), false, true, ENCPoolMethod::None);
			S.VFXComp = NC;
		}

		Singularities.Add(S);
	}
}

void ANBodySingularityZone::ComputeForces()
{
	// O(N²) 쌍별 중력 계산
	const float SoftSq = Softening * Softening;

	for (FSingularity& S : Singularities)
	{
		S.Acceleration = FVector::ZeroVector;
	}

	for (int32 i = 0; i < Singularities.Num(); ++i)
	{
		if (!Singularities[i].bAlive) continue;
		for (int32 j = i + 1; j < Singularities.Num(); ++j)
		{
			if (!Singularities[j].bAlive) continue;

			const FVector Delta = Singularities[j].Position - Singularities[i].Position;
			const float RSq = Delta.SizeSquared() + SoftSq;
			const float R = FMath::Sqrt(RSq);
			if (R < KINDA_SMALL_NUMBER) continue;

			// F = G * m_i * m_j / r²
			// a_i = F / m_i = G * m_j / r² (방향: Delta)
			const FVector Direction = Delta / R;
			const float ForcePerMass_i = GravityConstant * Singularities[j].Mass / RSq;
			const float ForcePerMass_j = GravityConstant * Singularities[i].Mass / RSq;

			Singularities[i].Acceleration += Direction * ForcePerMass_i;
			Singularities[j].Acceleration -= Direction * ForcePerMass_j;
		}
	}

	// 최대 가속도 클램프
	for (FSingularity& S : Singularities)
	{
		if (S.Acceleration.SizeSquared() > MaxAcceleration * MaxAcceleration)
		{
			S.Acceleration = S.Acceleration.GetSafeNormal() * MaxAcceleration;
		}
	}
}

void ANBodySingularityZone::IntegrateMotion(float DeltaTime)
{
	// Semi-implicit Euler: v += a*dt, p += v*dt
	for (FSingularity& S : Singularities)
	{
		if (!S.bAlive) continue;
		S.Velocity += S.Acceleration * DeltaTime;
		S.Position += S.Velocity * DeltaTime;
	}
}

void ANBodySingularityZone::EnforceBoundaryReflection()
{
	const FVector Center = GetActorLocation();
	const float MaxDistSq = MaxOrbitRadius * MaxOrbitRadius;

	for (FSingularity& S : Singularities)
	{
		if (!S.bAlive) continue;
		const FVector ToCenter = Center - S.Position;
		const float DistSq = ToCenter.SizeSquared2D();
		if (DistSq > MaxDistSq)
		{
			const FVector Inward = FVector(ToCenter.X, ToCenter.Y, 0.f).GetSafeNormal();
			// 현재 속도의 바깥 방향 성분을 반사
			const float VDotN = FVector::DotProduct(S.Velocity, -Inward);
			if (VDotN > 0.f)
			{
				S.Velocity -= 2.f * VDotN * (-Inward);
			}
			// 위치도 경계 안으로 당기기
			const float CurDist = FMath::Sqrt(DistSq);
			const FVector Clamped = Center - Inward * MaxOrbitRadius;
			S.Position.X = Clamped.X;
			S.Position.Y = Clamped.Y;
		}
	}
}

void ANBodySingularityZone::HandleMergers()
{
	const float MergeDistSq = MergerDistance * MergerDistance;

	for (int32 i = 0; i < Singularities.Num(); ++i)
	{
		if (!Singularities[i].bAlive) continue;
		for (int32 j = i + 1; j < Singularities.Num(); ++j)
		{
			if (!Singularities[j].bAlive) continue;

			const float DistSq = FVector::DistSquared(
				Singularities[i].Position, Singularities[j].Position);
			if (DistSq < MergeDistSq)
			{
				// 머저: 질량 합산, 운동량 보존
				const float M1 = Singularities[i].Mass;
				const float M2 = Singularities[j].Mass;
				const float TotalMass = M1 + M2;
				const FVector MergedPos = (Singularities[i].Position * M1 + Singularities[j].Position * M2) / TotalMass;
				const FVector MergedVel = (Singularities[i].Velocity * M1 + Singularities[j].Velocity * M2) / TotalMass;

				SpawnMergerExplosion(MergedPos);

				Singularities[i].Position = MergedPos;
				Singularities[i].Velocity = MergedVel;
				Singularities[i].Mass = TotalMass;
				Singularities[j].bAlive = false;

				// j의 VFX 제거
				if (Singularities[j].VFXComp.IsValid())
				{
					Singularities[j].VFXComp->DestroyComponent();
				}

				// 범위 데미지
				UWorld* World = GetWorld();
				if (World)
				{
					const float RSq = MergerExplosionRadius * MergerExplosionRadius;
					for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
					{
						AHellunaHeroCharacter* Hero = *It;
						if (!IsValid(Hero)) continue;
						if (FVector::DistSquared(Hero->GetActorLocation(), MergedPos) <= RSq)
						{
							UGameplayStatics::ApplyDamage(
								Hero, MergerExplosionDamage,
								OwnerEnemy ? OwnerEnemy->GetController() : nullptr,
								OwnerEnemy, UDamageType::StaticClass());
						}
					}
				}
			}
		}
	}
}

void ANBodySingularityZone::SpawnMergerExplosion(const FVector& Location)
{
	if (MergerExplosionVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), MergerExplosionVFX, Location, FRotator::ZeroRotator,
			FVector(MergerExplosionVFXScale), true, true, ENCPoolMethod::AutoRelease);
	}
}

void ANBodySingularityZone::ApplyGravityToPlayers(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World) return;

	const float SoftSq = Softening * Softening;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Hero = *It;
		if (!IsValid(Hero)) continue;

		const FVector HeroPos = Hero->GetActorLocation();
		FVector TotalAccel = FVector::ZeroVector;

		for (const FSingularity& S : Singularities)
		{
			if (!S.bAlive) continue;
			const FVector Delta = S.Position - HeroPos;
			const float RSq = Delta.SizeSquared2D() + SoftSq;
			const FVector Dir = FVector(Delta.X, Delta.Y, 0.f).GetSafeNormal();
			const float Accel = GravityConstant * S.Mass / RSq * PlayerPullMultiplier;
			TotalAccel += Dir * Accel;
		}

		if (TotalAccel.SizeSquared() > MaxAcceleration * MaxAcceleration)
		{
			TotalAccel = TotalAccel.GetSafeNormal() * MaxAcceleration;
		}

		Hero->LaunchCharacter(TotalAccel * DeltaTime, false, false);
	}
}

void ANBodySingularityZone::ProcessContactDamage()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const double Now = World->GetTimeSeconds();
	const float ContactSq = ContactRadius * ContactRadius;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Hero = *It;
		if (!IsValid(Hero)) continue;

		double& LastTime = LastContactTime.FindOrAdd(Hero);
		if (Now - LastTime < ContactCooldown) continue;

		const FVector HeroPos = Hero->GetActorLocation();
		for (const FSingularity& S : Singularities)
		{
			if (!S.bAlive) continue;
			if (FVector::DistSquared(S.Position, HeroPos) <= ContactSq)
			{
				UGameplayStatics::ApplyDamage(
					Hero, ContactDamage,
					OwnerEnemy ? OwnerEnemy->GetController() : nullptr,
					OwnerEnemy, UDamageType::StaticClass());
				LastTime = Now;
				break;
			}
		}
	}
}

void ANBodySingularityZone::UpdateVFXPositions()
{
	for (FSingularity& S : Singularities)
	{
		if (!S.bAlive) continue;
		if (S.VFXComp.IsValid())
		{
			S.VFXComp->SetWorldLocation(S.Position);
		}
	}
}

void ANBodySingularityZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bZoneActive) return;

	// 수치 안정성을 위해 서브 스텝
	const int32 Substeps = 2;
	const float SubDt = DeltaTime / Substeps;
	for (int32 s = 0; s < Substeps; ++s)
	{
		ComputeForces();
		IntegrateMotion(SubDt);
		HandleMergers();
		EnforceBoundaryReflection();
	}

	ApplyGravityToPlayers(DeltaTime);
	ProcessContactDamage();
	UpdateVFXPositions();
}
