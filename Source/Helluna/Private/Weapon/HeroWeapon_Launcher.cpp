// Capstone Project Helluna

#include "Weapon/HeroWeapon_Launcher.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Weapon/Projectile/HellunaProjectile_Launcher.h"

#include "DebugHelper.h"

bool AHeroWeapon_Launcher::BuildCachedAimData(AController* InstigatorController)
{
	if (!InstigatorController)
	{
		bHasCachedAim = false;
		return false;
	}

	APawn* Pawn = InstigatorController->GetPawn();
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		bHasCachedAim = false;
		return false;
	}

	const FRotator AimRotation = InstigatorController->GetControlRotation();
	const FVector ViewLocation = Pawn->GetPawnViewLocation();
	const FVector AimDirection = AimRotation.Vector().GetSafeNormal();
	const FVector TraceStart = ViewLocation;
	const FVector TraceEnd = TraceStart + (AimDirection * FMath::Max(Range, 1.f));

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(LauncherAimTrace), false);
	TraceParams.AddIgnoredActor(this);
	TraceParams.AddIgnoredActor(Pawn);

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		TraceEnd,
		TraceChannel,
		TraceParams
	);

	CachedAimStart = TraceStart;
	CachedAimPoint = bHit ? Hit.ImpactPoint : TraceEnd;
	CachedAimDirection = AimDirection;
	bHasCachedAim = true;

	UE_LOG(LogTemp, Warning,
		TEXT("[LauncherAimTrace] Start=%s Target=%s Dir=%s Hit=%s"),
		*CachedAimStart.ToCompactString(),
		*CachedAimPoint.ToCompactString(),
		*CachedAimDirection.ToCompactString(),
		bHit ? TEXT("Y") : TEXT("N"));

	return true;
}

FVector AHeroWeapon_Launcher::ResolveProjectileSpawnLocation(const FVector& ViewLocation, const FVector& AimDirection) const
{
	const FVector AdjustedViewLocation = ViewLocation - FVector(0.f, 0.f, 30.f);
	return AdjustedViewLocation + (AimDirection * FallbackSpawnOffset);
}

void AHeroWeapon_Launcher::CacheAimFromController(AController* InstigatorController)
{
	BuildCachedAimData(InstigatorController);
}

void AHeroWeapon_Launcher::Fire(AController* InstigatorController)
{
	if (!HasAuthority())
		return;

	if (!InstigatorController)
		return;

	APawn* Pawn = InstigatorController->GetPawn();
	if (!Pawn)
		return;

	if (!CanFire())
		return;

	if (!ProjectileClass)
	{
		Debug::Print(TEXT("[Launcher] ProjectileClass is null"), FColor::Red);
		return;
	}

	if (!bHasCachedAim && !BuildCachedAimData(InstigatorController))
		return;

	CurrentMag = FMath::Max(0, CurrentMag - 1);
	BroadcastAmmoChanged();

	const FVector SpawnLoc = ResolveProjectileSpawnLocation(CachedAimStart, CachedAimDirection);
	const FVector ToAimPoint = CachedAimPoint - SpawnLoc;
	const FVector Dir = ToAimPoint.IsNearlyZero() ? CachedAimDirection : ToAimPoint.GetSafeNormal();
	const FRotator SpawnRot = Dir.Rotation();

	UE_LOG(LogTemp, Warning,
		TEXT("[LauncherPredicted] Spawn=%s Target=%s Dir=%s AimStart=%s"),
		*SpawnLoc.ToCompactString(),
		*CachedAimPoint.ToCompactString(),
		*Dir.ToCompactString(),
		*CachedAimStart.ToCompactString());

	const float SafeSpeed = FMath::Max(ProjectileSpeed, 1.f);
	const float SafeRange = FMath::Max(Range, 1.f);
	const FVector Velocity = Dir * SafeSpeed;

	float LifeSeconds = SafeRange / SafeSpeed;
	LifeSeconds = FMath::Max(LifeSeconds, 0.01f);

	const FTransform SpawnTransform(SpawnRot, SpawnLoc);
	AHellunaProjectile_Launcher* Projectile = GetWorld()->SpawnActorDeferred<AHellunaProjectile_Launcher>(
		ProjectileClass,
		SpawnTransform,
		Pawn,
		Pawn,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn
	);

	if (!Projectile)
	{
		bHasCachedAim = false;
		return;
	}

	Projectile->InitProjectile(
		Damage,
		ProjectileExplosionRadius,
		Velocity,
		LifeSeconds
	);
	Projectile->FinishSpawning(SpawnTransform);

	bHasCachedAim = false;
}
