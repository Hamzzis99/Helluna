// Capstone Project Helluna

#include "BossEvent/BoidPredatorSwarmZone.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/HellunaEnemyCharacter.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

ABoidPredatorSwarmZone::ABoidPredatorSwarmZone()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ABoidPredatorSwarmZone::ActivateZone()
{
	if (!OwnerEnemy) { NotifyPatternFinished(false); return; }

	bZoneActive = true;
	bInDiveMode = false;
	DiveTimer = 0.f;
	DiveElapsed = 0.f;
	HashCellSize = FMath::Max(NeighborRadius, 100.f);

	SpawnBoids();
	SetActorTickEnabled(true);

	GetWorldTimerManager().SetTimer(PatternEndTimerHandle, [this]()
	{
		DeactivateZone();
	}, PatternDuration, false);
}

void ABoidPredatorSwarmZone::DeactivateZone()
{
	if (!bZoneActive) return;
	bZoneActive = false;
	SetActorTickEnabled(false);

	for (FBoid& B : Boids)
	{
		if (B.VFXComp.IsValid())
		{
			B.VFXComp->DestroyComponent();
		}
	}
	Boids.Empty();
	SpatialHash.Empty();
	GetWorldTimerManager().ClearTimer(PatternEndTimerHandle);
	NotifyPatternFinished(false);
}

void ABoidPredatorSwarmZone::SpawnBoids()
{
	Boids.Empty();
	Boids.Reserve(BoidCount);
	const FVector Center = GetActorLocation();

	for (int32 i = 0; i < BoidCount; ++i)
	{
		FBoid B;
		const float Ang = FMath::FRandRange(0.f, 2.f * PI);
		const float R = FMath::FRandRange(0.f, SpawnRadius);
		B.Position = Center + FVector(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R, FlightHeight);
		const FVector InitDir(FMath::Cos(Ang + PI * 0.5f), FMath::Sin(Ang + PI * 0.5f), 0.f);
		B.Velocity = InitDir * BaseSpeed;

		if (BoidVFX)
		{
			UNiagaraComponent* NC = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), BoidVFX, B.Position, FRotator::ZeroRotator,
				FVector(BoidVFXScale), false, true, ENCPoolMethod::None);
			B.VFXComp = NC;
		}

		Boids.Add(B);
	}
}

int64 ABoidPredatorSwarmZone::HashKey(const FVector& Pos) const
{
	const int32 x = FMath::FloorToInt(Pos.X / HashCellSize);
	const int32 y = FMath::FloorToInt(Pos.Y / HashCellSize);
	return (int64)x * 73856093LL ^ (int64)y * 19349663LL;
}

void ABoidPredatorSwarmZone::BuildSpatialHash()
{
	SpatialHash.Empty();
	for (int32 i = 0; i < Boids.Num(); ++i)
	{
		const int64 Key = HashKey(Boids[i].Position);
		SpatialHash.FindOrAdd(Key).Add(i);
	}
}

void ABoidPredatorSwarmZone::GatherNeighbors(int32 BoidIdx, TArray<int32>& OutNeighbors) const
{
	OutNeighbors.Reset();
	const FVector Pos = Boids[BoidIdx].Position;
	const int32 cx = FMath::FloorToInt(Pos.X / HashCellSize);
	const int32 cy = FMath::FloorToInt(Pos.Y / HashCellSize);

	const float NeighborSq = NeighborRadius * NeighborRadius;

	for (int32 dx = -1; dx <= 1; ++dx)
	{
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			const int64 Key = (int64)(cx + dx) * 73856093LL ^ (int64)(cy + dy) * 19349663LL;
			const TArray<int32>* Bucket = SpatialHash.Find(Key);
			if (!Bucket) continue;
			for (int32 j : *Bucket)
			{
				if (j == BoidIdx) continue;
				if (FVector::DistSquared(Boids[j].Position, Pos) <= NeighborSq)
				{
					OutNeighbors.Add(j);
				}
			}
		}
	}
}

FVector ABoidPredatorSwarmZone::ComputeSeparation(int32 Idx, const TArray<int32>& Neighbors) const
{
	FVector Steer = FVector::ZeroVector;
	int32 Count = 0;
	const float SepSq = SeparationRadius * SeparationRadius;
	for (int32 n : Neighbors)
	{
		const FVector Diff = Boids[Idx].Position - Boids[n].Position;
		const float DistSq = Diff.SizeSquared();
		if (DistSq > 0.f && DistSq < SepSq)
		{
			Steer += Diff.GetSafeNormal() / FMath::Sqrt(DistSq + 1.f);
			++Count;
		}
	}
	if (Count > 0) Steer /= (float)Count;
	return Steer * MaxForce;
}

FVector ABoidPredatorSwarmZone::ComputeAlignment(int32 Idx, const TArray<int32>& Neighbors) const
{
	if (Neighbors.Num() == 0) return FVector::ZeroVector;
	FVector AvgVel = FVector::ZeroVector;
	for (int32 n : Neighbors) AvgVel += Boids[n].Velocity;
	AvgVel /= (float)Neighbors.Num();
	AvgVel = AvgVel.GetSafeNormal() * MaxSpeed;
	FVector Steer = AvgVel - Boids[Idx].Velocity;
	return LimitVector(Steer, MaxForce);
}

FVector ABoidPredatorSwarmZone::ComputeCohesion(int32 Idx, const TArray<int32>& Neighbors) const
{
	if (Neighbors.Num() == 0) return FVector::ZeroVector;
	FVector Center = FVector::ZeroVector;
	for (int32 n : Neighbors) Center += Boids[n].Position;
	Center /= (float)Neighbors.Num();
	FVector Desired = (Center - Boids[Idx].Position).GetSafeNormal() * MaxSpeed;
	FVector Steer = Desired - Boids[Idx].Velocity;
	return LimitVector(Steer, MaxForce);
}

FVector ABoidPredatorSwarmZone::ComputeChase(int32 Idx) const
{
	UWorld* World = GetWorld();
	if (!World) return FVector::ZeroVector;

	AHellunaHeroCharacter* Closest = nullptr;
	float ClosestSq = FLT_MAX;
	const FVector Self = Boids[Idx].Position;

	for (TActorIterator<AHellunaHeroCharacter> It(World); It; ++It)
	{
		AHellunaHeroCharacter* Hero = *It;
		if (!IsValid(Hero)) continue;
		const float DSq = FVector::DistSquared(Hero->GetActorLocation(), Self);
		if (DSq < ClosestSq) { ClosestSq = DSq; Closest = Hero; }
	}
	if (!Closest) return FVector::ZeroVector;

	FVector Target = Closest->GetActorLocation();
	Target.Z = FlightHeight + GetActorLocation().Z;
	FVector Desired = (Target - Self).GetSafeNormal() * MaxSpeed;
	return LimitVector(Desired - Boids[Idx].Velocity, MaxForce);
}

FVector ABoidPredatorSwarmZone::LimitVector(const FVector& V, float MaxLen) const
{
	const float Sq = V.SizeSquared();
	if (Sq > MaxLen * MaxLen)
	{
		return V.GetSafeNormal() * MaxLen;
	}
	return V;
}

void ABoidPredatorSwarmZone::UpdateBoids(float DeltaTime)
{
	BuildSpatialHash();

	const float ChaseW = bInDiveMode ? DiveChaseWeight : ChaseWeight;
	const float SpeedCap = bInDiveMode ? MaxSpeed * DiveSpeedMultiplier : MaxSpeed;

	TArray<int32> Neighbors;
	Neighbors.Reserve(32);

	// 1단계: acceleration 계산
	for (int32 i = 0; i < Boids.Num(); ++i)
	{
		GatherNeighbors(i, Neighbors);

		const FVector Sep = ComputeSeparation(i, Neighbors) * SeparationWeight;
		const FVector Ali = ComputeAlignment(i, Neighbors) * AlignmentWeight;
		const FVector Coh = ComputeCohesion(i, Neighbors) * CohesionWeight;
		const FVector Cha = ComputeChase(i) * ChaseW;

		Boids[i].Acceleration = Sep + Ali + Coh + Cha;
		Boids[i].Acceleration = LimitVector(Boids[i].Acceleration, MaxForce);
	}

	// 2단계: 속도 + 위치 업데이트
	const float FloorZ = GetActorLocation().Z + FlightHeight;
	for (FBoid& B : Boids)
	{
		B.Velocity += B.Acceleration * DeltaTime;
		B.Velocity.Z = 0.f; // 평면 유지
		if (B.Velocity.SizeSquared() > SpeedCap * SpeedCap)
		{
			B.Velocity = B.Velocity.GetSafeNormal() * SpeedCap;
		}
		B.Position += B.Velocity * DeltaTime;
		B.Position.Z = FloorZ; // 평면 유지
	}
}

void ABoidPredatorSwarmZone::UpdateDiveMode(float DeltaTime)
{
	if (!bInDiveMode)
	{
		DiveTimer += DeltaTime;
		if (DiveTimer >= DiveInterval)
		{
			bInDiveMode = true;
			DiveElapsed = 0.f;
			DiveTimer = 0.f;
		}
	}
	else
	{
		DiveElapsed += DeltaTime;
		if (DiveElapsed >= DiveDuration)
		{
			bInDiveMode = false;
		}
	}
}

void ABoidPredatorSwarmZone::ProcessContactDamage()
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
		for (const FBoid& B : Boids)
		{
			if (FVector::DistSquared(B.Position, HeroPos) <= ContactSq)
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

void ABoidPredatorSwarmZone::UpdateVFXPositions()
{
	for (FBoid& B : Boids)
	{
		if (B.VFXComp.IsValid())
		{
			const FRotator Rot = B.Velocity.Rotation();
			B.VFXComp->SetWorldLocationAndRotation(B.Position, Rot);
		}
	}
}

void ABoidPredatorSwarmZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bZoneActive) return;

	UpdateDiveMode(DeltaTime);
	UpdateBoids(DeltaTime);
	ProcessContactDamage();
	UpdateVFXPositions();
}
