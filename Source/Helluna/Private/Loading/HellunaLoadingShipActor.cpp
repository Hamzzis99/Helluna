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

void AHellunaLoadingShipActor::BeginDampenForRelease()
{
	if (bDampening)
	{
		return;
	}
	bDampening = true;
	bRestoring = false;
	DampenElapsed = 0.f;
	ActiveDuration = ShipDampenDuration;
	// Dampen 경로는 OnSettleComplete를 쏘지 않는다 — 가드를 미리 true로 세팅.
	bSettleCompleteFired = true;
}

void AHellunaLoadingShipActor::BeginSettleForTravel()
{
	if (bDampening)
	{
		return;
	}
	bDampening = true;
	bRestoring = false;
	DampenElapsed = 0.f;
	ActiveDuration = ShipSettleDuration;
	bSettleCompleteFired = false;
}

void AHellunaLoadingShipActor::ApplyPoseSnapshot(float InTimeAccum, float InAmpScale)
{
	TimeAccum = InTimeAccum;
	bDampening = false;

	bRestoring = true;
	RestoreElapsed = 0.f;
	RestoreStartAmp = FMath::Clamp(InAmpScale, 0.f, 1.f);
	bSettleCompleteFired = true;
}

float AHellunaLoadingShipActor::GetCurrentAmpScale() const
{
	if (bDampening)
	{
		const float Alpha = FMath::Clamp(DampenElapsed / FMath::Max(ActiveDuration, 0.01f), 0.f, 1.f);
		const float OneMinusAlpha = 1.f - Alpha;
		return OneMinusAlpha * OneMinusAlpha * OneMinusAlpha;
	}
	if (bRestoring)
	{
		const float Alpha = FMath::Clamp(RestoreElapsed / FMath::Max(ShipRestoreDuration, 0.01f), 0.f, 1.f);
		return FMath::Lerp(RestoreStartAmp, 1.f, Alpha * Alpha);
	}
	return 1.f;
}

void AHellunaLoadingShipActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TimeAccum += DeltaSeconds;

	float AmpScale = 1.f;

	if (bDampening)
	{
		DampenElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(DampenElapsed / FMath::Max(ActiveDuration, 0.01f), 0.f, 1.f);
		const float OneMinusAlpha = 1.f - Alpha;
		AmpScale = OneMinusAlpha * OneMinusAlpha * OneMinusAlpha;
		if (Alpha >= 1.f)
		{
			bDampening = false;
			AmpScale = 0.f;
			if (!bSettleCompleteFired)
			{
				bSettleCompleteFired = true;
				OnSettleComplete.Broadcast();
			}
		}
	}
	else if (bRestoring)
	{
		RestoreElapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(RestoreElapsed / FMath::Max(ShipRestoreDuration, 0.01f), 0.f, 1.f);
		AmpScale = FMath::Lerp(RestoreStartAmp, 1.f, Alpha * Alpha);
		if (Alpha >= 1.f)
		{
			bRestoring = false;
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
