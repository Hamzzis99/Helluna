// ════════════════════════════════════════════════════════════════════════════════
// HellunaLoadingShipActor.cpp
// ════════════════════════════════════════════════════════════════════════════════

#include "Loading/HellunaLoadingShipActor.h"
#include "Components/StaticMeshComponent.h"

AHellunaLoadingShipActor::AHellunaLoadingShipActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = false;
	SetActorEnableCollision(false);

	ShipMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipMesh"));
	ShipMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShipMesh->SetMobility(EComponentMobility::Movable);
	RootComponent = ShipMesh;
}

void AHellunaLoadingShipActor::BeginPlay()
{
	Super::BeginPlay();
	BaseLocation = GetActorLocation();
	BaseRotation = GetActorRotation();
}

void AHellunaLoadingShipActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TimeAccum += DeltaSeconds;

	float AmpScale = 1.f;
	if (bDampening)
	{
		DampenElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(DampenElapsed / FMath::Max(ShipDampenDuration, 0.01f), 0.f, 1.f);
		// ease-out cubic
		const float OneMinusAlpha = 1.f - Alpha;
		AmpScale = OneMinusAlpha * OneMinusAlpha * OneMinusAlpha;
		if (Alpha >= 1.f)
		{
			bDampening = false;
			AmpScale = 0.f;
		}
	}

	const float Roll = FMath::Sin(TimeAccum * RollSpeed) * RollAmplitudeDeg * AmpScale;
	const float Pitch = FMath::Sin(TimeAccum * PitchSpeed) * PitchAmplitudeDeg * AmpScale;
	const float FloatZ = FMath::Sin(TimeAccum * FloatSpeed) * FloatAmplitudeCm * AmpScale;

	FRotator Rot = BaseRotation;
	Rot.Roll += Roll;
	Rot.Pitch += Pitch;
	SetActorRotation(Rot);

	FVector Loc = BaseLocation;
	Loc.Z += FloatZ;
	SetActorLocation(Loc);
}

void AHellunaLoadingShipActor::BeginDampenForRelease()
{
	if (bDampening)
	{
		return;
	}
	bDampening = true;
	DampenElapsed = 0.f;
}
