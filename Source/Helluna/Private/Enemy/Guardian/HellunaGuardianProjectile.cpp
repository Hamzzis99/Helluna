// File: Source/Helluna/Private/Enemy/Guardian/HellunaGuardianProjectile.cpp
#include "Enemy/Guardian/HellunaGuardianProjectile.h"
#include "AbilitySystem/HeroAbility/HeroGameplayAbility_Block.h"
#include "Character/HellunaHeroCharacter.h"
#include "Enemy/Guardian/HellunaDamageType_PhysicsImpact.h"

#include "Components/BoxComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
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

	if (bCanBePerfectBlocked && !bReflected && OtherActor->IsA<AHellunaHeroCharacter>())
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

	FVector ReflectedDirection = FVector::ZeroVector;
	if (OriginalOwnerActor.IsValid())
	{
		ReflectedDirection = (OriginalOwnerActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	}
	if (ReflectedDirection.IsNearlyZero())
	{
		ReflectedDirection = -CurrentVelocity.GetSafeNormal();
	}
	if (ReflectedDirection.IsNearlyZero())
	{
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
	SetLifeSpan(FMath::Max(ReflectedLifeSeconds, 0.1f));

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

	UWorld* World = GetWorld();
	if (World && Radius > 0.f)
	{
		TArray<AActor*> Ignore;
		Ignore.Add(this);
		if (AActor* OwnerActor = GetOwner())
		{
			Ignore.Add(OwnerActor);
		}

		// ApplyRadialDamage: Hero / Enemy 구분 없이 모두 데미지.
		// DamagePreventionChannel 로 벽 차폐 (bIgnoreWorldStatic=false 시).
		const ECollisionChannel PreventionChannel = bIgnoreWorldStatic ? ECC_MAX : BlockTraceChannel.GetValue();
		UGameplayStatics::ApplyRadialDamage(
			this,
			Damage,
			ExplosionLocation,
			Radius,
			UHellunaDamageType_PhysicsImpact::StaticClass(),
			Ignore,
			GetOwner(), /*DamageCauser=*/
			nullptr,   /*InstigatorController=*/
			/*bDoFullDamage=*/!bFalloff,
			PreventionChannel);

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
