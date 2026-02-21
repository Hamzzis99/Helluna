// Capstone Project Helluna


#include "Weapon/HeroWeapon_Launcher.h"
#include "Weapon/Projectile/HellunaProjectile_Launcher.h"



#include "DebugHelper.h"

void AHeroWeapon_Launcher::Fire(AController* InstigatorController)
{
	// ✅ 서버에서만 발사(스폰/탄소모/권한 데미지)
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

	// 탄 소모
	CurrentMag = FMath::Max(0, CurrentMag - 1);
	BroadcastAmmoChanged();

	// 카메라 기준
	FVector ViewLoc;
	FRotator ViewRot;
	InstigatorController->GetPlayerViewPoint(ViewLoc, ViewRot);

	// 스폰: 머즐 소켓 우선, 없으면 카메라 앞
	FVector SpawnLoc = ViewLoc + (ViewRot.Vector() * FallbackSpawnOffset);
	FRotator SpawnRot = ViewRot;

	if (WeaponMesh && !MuzzleSocketName.IsNone() && WeaponMesh->DoesSocketExist(MuzzleSocketName))
	{
		const FTransform MuzzleTM = WeaponMesh->GetSocketTransform(MuzzleSocketName);
		SpawnLoc = MuzzleTM.GetLocation();
		SpawnRot = MuzzleTM.GetRotation().Rotator();
	}

	const FVector Dir = SpawnRot.Vector();

	const float SafeSpeed = FMath::Max(ProjectileSpeed, 1.f);
	const float SafeRange = FMath::Max(Range, 1.f);

	const FVector Velocity = Dir * SafeSpeed;

	// 최대거리 -> 수명(초)
	float LifeSeconds = SafeRange / SafeSpeed;
	LifeSeconds = FMath::Max(LifeSeconds, 0.01f);

	FActorSpawnParameters Params;
	Params.Owner = Pawn;
	Params.Instigator = Pawn;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AHellunaProjectile_Launcher* Projectile = GetWorld()->SpawnActor<AHellunaProjectile_Launcher>(
		ProjectileClass,
		SpawnLoc,
		SpawnRot,
		Params
	);

	if (!Projectile)
		return;

	// ✅ 데미지(Damage)는 GunBase의 값을 사용
	// ✅ 반경(ExplosionRadius)은 Launcher 무기에서만 설정
	Projectile->InitProjectile(
		Damage,
		ProjectileExplosionRadius,
		Velocity,
		LifeSeconds
	);
}