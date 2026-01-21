// Capstone Project Helluna


#include "Weapon/HellunaFarmingWeapon.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"


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

	const float AppliedDamage = UGameplayStatics::ApplyPointDamage(
		Target,
		Damage,
		ShotDir,
		Hit,
		InstigatorController,
		InstigatorActor,
		UDamageType::StaticClass()
	);

	if (FarmHitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FarmHitSound, Target->GetActorLocation());
	}

	Debug::Print(FString::Printf(TEXT("[Pickaxe Farm] Target=%s Damage=%.1f"), *Target->GetName(), AppliedDamage), FColor::Green);
}