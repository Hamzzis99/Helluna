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

	// 서버에서는 카메라 매니저/스켈레탈 메시가 틱하지 않아 머즐 소켓 위치가 부정확함.
	// ControlRotation(서버 동기화됨) + GetPawnViewLocation(BaseEyeHeight 기반)으로 안정적인 스폰 위치 계산.
	const FRotator ViewRot = InstigatorController->GetControlRotation();
	// 눈 높이에서 아래로 내려 어깨 높이에서 발사 (BaseEyeHeight 기준 약 30cm 아래)
	const FVector ViewLoc = Pawn->GetPawnViewLocation() - FVector(0.f, 0.f, 30.f);

	// 어깨 위치에서 조준 방향으로 오프셋을 준 위치에서 스폰
	const FVector SpawnLoc = ViewLoc + (ViewRot.Vector() * FallbackSpawnOffset);
	const FRotator SpawnRot = ViewRot;

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