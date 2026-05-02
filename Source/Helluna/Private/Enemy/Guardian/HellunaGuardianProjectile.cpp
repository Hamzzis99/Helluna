// File: Source/Helluna/Private/Enemy/Guardian/HellunaGuardianProjectile.cpp
#include "Enemy/Guardian/HellunaGuardianProjectile.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Block.h"
#include "Character/HellunaHeroCharacter.h"
#include "Enemy/Guardian/HellunaDamageType_PhysicsImpact.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

AHellunaGuardianProjectile::AHellunaGuardianProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	RootComponent = CollisionBox;

	CollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionBox->SetGenerateOverlapEvents(false);
	CollisionBox->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionBox->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	CollisionBox->OnComponentBeginOverlap.AddDynamic(this, &AHellunaGuardianProjectile::OnBeginOverlap);

	MoveComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MoveComp"));
	MoveComp->bRotationFollowsVelocity = true;
	MoveComp->bShouldBounce = false;
	MoveComp->ProjectileGravityScale = 0.f; // 가디언 레이저는 직진
	MoveComp->bInitialVelocityInLocalSpace = false;

	TrailFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("TrailFX"));
	TrailFX->SetupAttachment(RootComponent);
	TrailFX->bAutoActivate = true;
}

void AHellunaGuardianProjectile::SetProjectileCollisionEnabled(bool bEnabled)
{
	if (!CollisionBox)
	{
		return;
	}

	CollisionBox->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	CollisionBox->SetGenerateOverlapEvents(bEnabled);
}

void AHellunaGuardianProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (CollisionBox)
	{
		CollisionBox->SetBoxExtent(OverlapBoxExtent);

		if (AActor* OwnerActor = GetOwner())
		{
			OriginalOwnerActor = OwnerActor;
			CollisionBox->IgnoreActorWhenMoving(OwnerActor, true);
		}
	}

	// 비행 중 TrailFX 활성화 (bAutoActivate 만으로 안 켜지는 NS 대응)
	if (TrailFX)
	{
		TrailFX->Activate(true);
	}

	SetProjectileCollisionEnabled(bInitialized);
}

void AHellunaGuardianProjectile::InitProjectile(
	float InDamage,
	float InRadius,
	bool bInFalloff,
	const FVector& InVelocity,
	float InLifeSeconds)
{
	Damage = InDamage;
	Radius = InRadius;
	bFalloff = bInFalloff;
	bInitialized = true;

	if (MoveComp)
	{
		const float Speed = InVelocity.Size();
		MoveComp->Velocity = InVelocity;
		MoveComp->InitialSpeed = Speed;
		MoveComp->MaxSpeed = Speed;
	}

	if (CollisionBox)
	{
		CollisionBox->SetBoxExtent(OverlapBoxExtent);
		if (AActor* OwnerActor = GetOwner())
		{
			if (!OriginalOwnerActor.IsValid())
			{
				OriginalOwnerActor = OwnerActor;
			}
			CollisionBox->IgnoreActorWhenMoving(OwnerActor, true);
		}
	}

	SetProjectileCollisionEnabled(true);

	// 수명 만료 시 공중 폭발
	SetLifeSpan(FMath::Max(InLifeSeconds, 0.01f));
}

void AHellunaGuardianProjectile::OnBeginOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !bInitialized || bExploded)
	{
		return;
	}

	if (!OtherActor || OtherActor == this || OtherActor == GetOwner())
	{
		return;
	}

	if (bCanBePerfectBlocked && bReflectOnPerfectBlock && !bReflected && OtherActor->IsA<AHellunaHeroCharacter>())
	{
		bool bPerfectBlock = false;
		if (UHeroGameplayAbility_Block::EvaluateBlock(OtherActor, this, bPerfectBlock) && bPerfectBlock)
		{
			UHeroGameplayAbility_Block::ExecuteBlockCue(OtherActor, this, true);
			if (TryReflectFromPerfectBlock(OtherActor))
			{
				return;
			}
		}
	}

	FVector HitLoc = GetActorLocation();
	FVector HitNormal = -GetActorForwardVector();
	if (bFromSweep)
	{
		HitLoc = SweepResult.ImpactPoint;
		HitNormal = SweepResult.ImpactNormal;
	}

	Explode(HitLoc, HitNormal);
}

bool AHellunaGuardianProjectile::TryReflectFromPerfectBlock(AActor* BlockingActor)
{
	if (!HasAuthority() || !IsValid(BlockingActor) || !MoveComp || bExploded)
	{
		return false;
	}

	AActor* PreviousOwner = GetOwner();
	const FVector CurrentVelocity = MoveComp->Velocity;
	const float BaseSpeed = FMath::Max(CurrentVelocity.Size(), MoveComp->MaxSpeed);
	const float ReflectedSpeed = FMath::Max(BaseSpeed * ReflectedSpeedMultiplier, 100.f);

	// [BOTW식 반사] 들어온 속도 벡터의 정확한 반대 — 투사체를 그대로 돌려보낸다.
	// Guardian 위치 추적이 아니라 입사 방향 정반대로 반사.
	FVector ReflectedDirection = -CurrentVelocity.GetSafeNormal();
	if (ReflectedDirection.IsNearlyZero() && OriginalOwnerActor.IsValid())
	{
		// fallback 1: 속도가 거의 0이면 발사자 방향 (정지 상태 투사체 보호)
		ReflectedDirection = (OriginalOwnerActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	}
	if (ReflectedDirection.IsNearlyZero())
	{
		// fallback 2: 그래도 0이면 액터 forward 반대
		ReflectedDirection = -GetActorForwardVector();
	}

	if (CollisionBox)
	{
		if (PreviousOwner)
		{
			CollisionBox->IgnoreActorWhenMoving(PreviousOwner, false);
		}
		CollisionBox->IgnoreActorWhenMoving(BlockingActor, true);
	}

	SetOwner(BlockingActor);
	if (APawn* BlockingPawn = Cast<APawn>(BlockingActor))
	{
		SetInstigator(BlockingPawn);
	}

	const FVector SafeOffset = ReflectedDirection * FMath::Max(OverlapBoxExtent.GetMax() * 2.f, 32.f);
	SetActorLocation(GetActorLocation() + SafeOffset, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(ReflectedDirection.Rotation());

	MoveComp->Velocity = ReflectedDirection * ReflectedSpeed;
	MoveComp->InitialSpeed = ReflectedSpeed;
	MoveComp->MaxSpeed = ReflectedSpeed;
	MoveComp->UpdateComponentVelocity();

	bReflected = true;
	// [Perfect Reflect] 반사 후엔 가디언/터렛 즉사 목적의 직격 데미지로 갱신.
	// 폭발 falloff 적용을 받지만 가디언이 폭발 중심이면 풀 데미지가 들어가 즉사.
	Damage = PerfectBlockReflectedDamage;
	SetLifeSpan(FMath::Max(ReflectedLifeSeconds, 0.1f));

	Multicast_OnPerfectBlockReflected(
		FVector_NetQuantize(GetActorLocation()),
		FVector_NetQuantizeNormal(ReflectedDirection));

	return true;
}

void AHellunaGuardianProjectile::LifeSpanExpired()
{
	// 공중에서 수명 만료 → 현재 위치에서 폭발 (서퍼스 Normal 은 이동 반대방향)
	if (HasAuthority() && bInitialized && !bExploded)
	{
		const FVector AirNormal = -GetActorForwardVector();
		Explode(GetActorLocation(), AirNormal);
		return;
	}

	Super::LifeSpanExpired();
}

void AHellunaGuardianProjectile::Explode(const FVector& ExplosionLocation, const FVector& SurfaceNormal)
{
	if (!HasAuthority() || bExploded)
	{
		return;
	}

	bExploded = true;
	SetProjectileCollisionEnabled(false);
	SetActorLocation(ExplosionLocation, false, nullptr, ETeleportType::TeleportPhysics);

	UWorld* World = GetWorld();
	if (World && Radius > 0.f)
	{
		ApplyShieldAwareExplosionDamage(ExplosionLocation);

		if (bDebugDrawRadialDamage)
		{
			DrawDebugSphere(World, ExplosionLocation, Radius, 24, FColor::Red, false, 1.0f, 0, 1.5f);
		}
	}

	// 모든 클라에 폭발 FX 전파
	Multicast_SpawnExplosionFX(
		FVector_NetQuantize(ExplosionLocation),
		FVector_NetQuantizeNormal(SurfaceNormal));

	// 즉시 Destroy 하면 Multicast RPC 가 일부 클라에 도달 전 소멸 가능 → 숨긴 뒤 짧은 LifeSpan
	if (MoveComp)
	{
		MoveComp->StopMovementImmediately();
	}
	SetActorHiddenInGame(true);
	SetLifeSpan(0.2f);
}

void AHellunaGuardianProjectile::ApplyShieldAwareExplosionDamage(const FVector& ExplosionLocation)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || Radius <= 0.f || Damage <= 0.f)
	{
		return;
	}

	TMap<AActor*, TArray<FHitResult>> HitMap;
	BuildExplosionDamageHitMap(ExplosionLocation, HitMap);
	if (HitMap.IsEmpty())
	{
		return;
	}

	TArray<AHellunaHeroCharacter*> BlockingHeroes;
	if (bCanBeBlocked)
	{
		for (const TPair<AActor*, TArray<FHitResult>>& Pair : HitMap)
		{
			AHellunaHeroCharacter* Hero = Cast<AHellunaHeroCharacter>(Pair.Key);
			if (IsBlockingExplosion(Hero, ExplosionLocation))
			{
				BlockingHeroes.Add(Hero);
			}
		}
	}

	for (const TPair<AActor*, TArray<FHitResult>>& Pair : HitMap)
	{
		AActor* Victim = Pair.Key;
		if (!IsValid(Victim) || Victim == this || Victim == GetOwner() || !Victim->CanBeDamaged())
		{
			continue;
		}

		const TArray<FHitResult>& ComponentHits = Pair.Value;
		if (ComponentHits.IsEmpty())
		{
			continue;
		}

		AHellunaHeroCharacter* VictimHero = Cast<AHellunaHeroCharacter>(Victim);
		const bool bSelfBlocked = VictimHero && BlockingHeroes.Contains(VictimHero);
		if (!bSelfBlocked)
		{
			if (AHellunaHeroCharacter* CoveringHero = FindShieldCoveringHeroFor(Victim, BlockingHeroes, ExplosionLocation))
			{
				if (bDebugDrawShieldBlock)
				{
					DrawDebugLine(World, ExplosionLocation, Victim->GetActorLocation(), FColor::Cyan, false, 1.0f, 0, 2.0f);
					DrawDebugLine(World, ExplosionLocation, CoveringHero->GetActorLocation(), FColor::Blue, false, 1.0f, 0, 2.0f);
				}
				continue;
			}
		}

		const float DamageMultiplier = bSelfBlocked
			? FMath::Clamp(BlockedExplosionDamageMultiplier, 0.f, 1.f)
			: 1.f;
		const float FinalBaseDamage = Damage * DamageMultiplier;
		if (FinalBaseDamage <= 0.f)
		{
			continue;
		}

		TSubclassOf<UDamageType> DamageTypeClass = bSelfBlocked
			? UDamageType::StaticClass()
			: UHellunaDamageType_PhysicsImpact::StaticClass();

		ApplyRadialDamageEventToActor(
			Victim,
			FinalBaseDamage,
			ExplosionLocation,
			ComponentHits,
			DamageTypeClass);

		if (bSelfBlocked)
		{
			UHeroGameplayAbility_Block::ExecuteBlockCue(VictimHero, this, false);
			if (bDebugDrawShieldBlock)
			{
				DrawDebugLine(World, ExplosionLocation, Victim->GetActorLocation(), FColor::Yellow, false, 1.0f, 0, 2.0f);
			}
		}
	}
}

void AHellunaGuardianProjectile::BuildExplosionDamageHitMap(
	const FVector& ExplosionLocation,
	TMap<AActor*, TArray<FHitResult>>& OutHitMap) const
{
	OutHitMap.Reset();

	UWorld* World = GetWorld();
	if (!World || Radius <= 0.f)
	{
		return;
	}

	FCollisionQueryParams SphereParams(SCENE_QUERY_STAT(GuardianProjectileExplosionOverlap), false, this);
	SphereParams.AddIgnoredActor(this);
	if (AActor* OwnerActor = GetOwner())
	{
		SphereParams.AddIgnoredActor(OwnerActor);
	}

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		ExplosionLocation,
		FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllDynamicObjects),
		FCollisionShape::MakeSphere(Radius),
		SphereParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.GetActor();
		UPrimitiveComponent* VictimComp = Overlap.GetComponent();
		if (!IsValid(Victim) || !IsValid(VictimComp) || Victim == this || Victim == GetOwner() || !Victim->CanBeDamaged())
		{
			continue;
		}

		FHitResult Hit;
		if (IsExplosionDamageableFrom(VictimComp, ExplosionLocation, Hit))
		{
			OutHitMap.FindOrAdd(Victim).Add(Hit);
		}
	}
}

bool AHellunaGuardianProjectile::IsExplosionDamageableFrom(
	UPrimitiveComponent* VictimComp,
	const FVector& ExplosionLocation,
	FHitResult& OutHitResult) const
{
	if (!IsValid(VictimComp))
	{
		return false;
	}

	UWorld* World = VictimComp->GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector TraceEnd = VictimComp->Bounds.Origin;
	FVector TraceStart = ExplosionLocation;
	if (TraceStart.Equals(TraceEnd))
	{
		TraceStart.Z += 0.01f;
	}

	const ECollisionChannel TraceChannel = bIgnoreWorldStatic ? ECC_MAX : BlockTraceChannel.GetValue();
	if (TraceChannel != ECC_MAX)
	{
		FCollisionQueryParams LineParams(SCENE_QUERY_STAT(GuardianProjectileExplosionVisibility), true, this);
		LineParams.AddIgnoredActor(this);
		if (AActor* OwnerActor = GetOwner())
		{
			LineParams.AddIgnoredActor(OwnerActor);
		}

		if (World->LineTraceSingleByChannel(OutHitResult, TraceStart, TraceEnd, TraceChannel, LineParams))
		{
			return OutHitResult.Component.Get() == VictimComp;
		}
	}

	const FVector FakeHitLoc = VictimComp->GetComponentLocation();
	const FVector FakeHitNorm = (ExplosionLocation - FakeHitLoc).GetSafeNormal();
	OutHitResult = FHitResult(VictimComp->GetOwner(), VictimComp, FakeHitLoc, FakeHitNorm);
	return true;
}

bool AHellunaGuardianProjectile::IsBlockingExplosion(
	const AHellunaHeroCharacter* Hero,
	const FVector& ExplosionLocation) const
{
	if (!bCanBeBlocked || !IsValid(Hero) || !UHeroGameplayAbility_Block::IsBlocking(Hero))
	{
		return false;
	}

	FVector HeroForward = Hero->GetActorForwardVector();
	HeroForward.Z = 0.f;
	if (!HeroForward.Normalize())
	{
		return false;
	}

	FVector ToExplosion = ExplosionLocation - Hero->GetActorLocation();
	ToExplosion.Z = 0.f;
	if (!ToExplosion.Normalize())
	{
		ToExplosion = -GetActorForwardVector();
		ToExplosion.Z = 0.f;
		if (!ToExplosion.Normalize())
		{
			return false;
		}
	}

	return FVector::DotProduct(HeroForward, ToExplosion) >= BlockFrontDotThreshold;
}

AHellunaHeroCharacter* AHellunaGuardianProjectile::FindShieldCoveringHeroFor(
	const AActor* Victim,
	const TArray<AHellunaHeroCharacter*>& BlockingHeroes,
	const FVector& ExplosionLocation) const
{
	if (!IsValid(Victim))
	{
		return nullptr;
	}

	for (AHellunaHeroCharacter* BlockingHero : BlockingHeroes)
	{
		if (IsActorCoveredByShield(Victim, BlockingHero, ExplosionLocation))
		{
			return BlockingHero;
		}
	}

	return nullptr;
}

bool AHellunaGuardianProjectile::IsActorCoveredByShield(
	const AActor* Victim,
	const AHellunaHeroCharacter* BlockingHero,
	const FVector& ExplosionLocation) const
{
	if (!IsValid(Victim) || !IsValid(BlockingHero) || Victim == BlockingHero)
	{
		return false;
	}

	if (!IsBlockingExplosion(BlockingHero, ExplosionLocation))
	{
		return false;
	}

	const FVector BlockerLocation = BlockingHero->GetActorLocation();
	const FVector VictimLocation = Victim->GetActorLocation();
	const float ExplosionToBlockerDistance = FVector::Dist2D(ExplosionLocation, BlockerLocation);
	const float ExplosionToVictimDistance = FVector::Dist2D(ExplosionLocation, VictimLocation);
	if (ExplosionToVictimDistance <= ExplosionToBlockerDistance + 1.f)
	{
		return false;
	}

	FVector ExplosionToBlocker = BlockerLocation - ExplosionLocation;
	ExplosionToBlocker.Z = 0.f;
	FVector ExplosionToVictim = VictimLocation - ExplosionLocation;
	ExplosionToVictim.Z = 0.f;
	if (!ExplosionToBlocker.Normalize() || !ExplosionToVictim.Normalize())
	{
		return false;
	}

	if (FVector::DotProduct(ExplosionToBlocker, ExplosionToVictim) < ShieldCoverDotThreshold)
	{
		return false;
	}

	FVector BlockerToVictim = VictimLocation - BlockerLocation;
	BlockerToVictim.Z = 0.f;
	const float BackDistance = BlockerToVictim.Size();
	if (BackDistance > ShieldCoverLength)
	{
		return false;
	}

	FVector BlockerForward = BlockingHero->GetActorForwardVector();
	BlockerForward.Z = 0.f;
	if (!BlockerForward.Normalize())
	{
		return false;
	}

	const FVector BlockerToVictimDir = BlockerToVictim.GetSafeNormal();
	if (FVector::DotProduct(BlockerForward, BlockerToVictimDir) >= 0.f)
	{
		return false;
	}

	FVector BlockerRight = BlockingHero->GetActorRightVector();
	BlockerRight.Z = 0.f;
	if (!BlockerRight.Normalize())
	{
		return false;
	}

	return FMath::Abs(FVector::DotProduct(BlockerToVictim, BlockerRight)) <= ShieldCoverHalfWidth;
}

void AHellunaGuardianProjectile::ApplyRadialDamageEventToActor(
	AActor* Victim,
	float BaseDamage,
	const FVector& ExplosionLocation,
	const TArray<FHitResult>& ComponentHits,
	TSubclassOf<UDamageType> DamageTypeClass)
{
	if (!HasAuthority() || !IsValid(Victim) || BaseDamage <= 0.f || ComponentHits.IsEmpty())
	{
		return;
	}

	FRadialDamageEvent DamageEvent;
	DamageEvent.DamageTypeClass = DamageTypeClass;
	if (!DamageEvent.DamageTypeClass)
	{
		DamageEvent.DamageTypeClass = UDamageType::StaticClass();
	}
	DamageEvent.Origin = ExplosionLocation;
	DamageEvent.Params = FRadialDamageParams(
		BaseDamage,
		0.f,
		0.f,
		Radius,
		bFalloff ? 1.f : 0.f);
	DamageEvent.ComponentHits = ComponentHits;

	Victim->TakeDamage(BaseDamage, DamageEvent, GetInstigatorController(), this);
}

void AHellunaGuardianProjectile::RestartTrailFXAfterReflection()
{
	if (!bResetTrailOnPerfectBlockReflect || !TrailFX)
	{
		return;
	}

	TrailFX->DeactivateImmediate();
	TrailFX->Activate(true);
}

void AHellunaGuardianProjectile::Multicast_OnPerfectBlockReflected_Implementation(
	FVector_NetQuantize ReflectionLocation,
	FVector_NetQuantizeNormal ReflectedDirection)
{
	const FVector Direction = FVector(ReflectedDirection).GetSafeNormal();
	if (!Direction.IsNearlyZero())
	{
		SetActorRotation(Direction.Rotation());
	}

	SetActorLocation(FVector(ReflectionLocation), false, nullptr, ETeleportType::TeleportPhysics);

	if (IsRunningDedicatedServer())
	{
		return;
	}

	RestartTrailFXAfterReflection();
}

void AHellunaGuardianProjectile::Multicast_SpawnExplosionFX_Implementation(
	FVector_NetQuantize ExplosionLocation, FVector_NetQuantizeNormal SurfaceNormal)
{
	// 데디서버는 렌더러가 없어 VFX 불필요 (Niagara 컴포넌트 오버헤드 방지)
	if (IsRunningDedicatedServer())
	{
		return;
	}

	if (TrailFX)
	{
		TrailFX->Deactivate();
	}

	const FVector SpawnLocation = FVector(ExplosionLocation);
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this, ExplosionSound, SpawnLocation,
			ExplosionSoundVolume, ExplosionSoundPitch, 0.f,
			ExplosionSoundAttenuation, ExplosionSoundConcurrency);
	}

	if (!ExplosionFX)
	{
		return;
	}

	// 공중 폭발(N_ExplosionAir 등) 은 월드 기준 수직으로 재생되어야 자연스러움.
	// 벽 정렬이 필요하면 bAlignExplosionToSurface=true 로 토글.
	const FVector NormalVec = FVector(SurfaceNormal);
	const FRotator SpawnRot = (bAlignExplosionToSurface && !NormalVec.IsNearlyZero())
		? NormalVec.Rotation()
		: FRotator::ZeroRotator;

	UNiagaraComponent* Spawned = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		this,
		ExplosionFX,
		SpawnLocation,
		SpawnRot,
		ExplosionFXScale,
		true, true, ENCPoolMethod::AutoRelease);
	if (Spawned)
	{
		Spawned->Activate(true);
		if (!ExplosionFXRadiusParamName.IsNone() && Radius > 0.f)
		{
			Spawned->SetFloatParameter(ExplosionFXRadiusParamName, Radius);
		}
	}
}
