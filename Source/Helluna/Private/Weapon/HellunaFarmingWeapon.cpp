// Capstone Project Helluna


#include "Weapon/HellunaFarmingWeapon.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "Resource/Inv_FarmingDamageType.h"


#include "DebugHelper.h"

void AHellunaFarmingWeapon::Farm(AController* InstigatorController, AActor* Target)
{
	if (!Target) return;

	AActor* InstigatorActor = GetOwner();
	if (!InstigatorActor) return;

	// ✅ 최소 Hit 구성
	FHitResult Hit;
	Hit.Location = Target->GetActorLocation();
	Hit.ImpactPoint = Hit.Location;
	Hit.TraceStart = InstigatorActor->GetActorLocation();
	Hit.TraceEnd = Hit.Location;
	Hit.bBlockingHit = true;

	const FVector ShotDir = (Target->GetActorLocation() - InstigatorActor->GetActorLocation()).GetSafeNormal();

	// 채집 전용 데미지 타입 — 자원(광석)은 이 타입의 데미지만 채집으로 인정한다.
	const float AppliedDamage = UGameplayStatics::ApplyPointDamage(
		Target,
		Damage,
		ShotDir,
		Hit,
		InstigatorController,
		InstigatorActor,
		UInv_FarmingDamageType::StaticClass()
	);

	if (FarmHitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FarmHitSound, Target->GetActorLocation());
	}

	Debug::Print(FString::Printf(TEXT("[Pickaxe Farm] Target=%s Damage=%.1f"), *Target->GetName(), AppliedDamage), FColor::Green);
}