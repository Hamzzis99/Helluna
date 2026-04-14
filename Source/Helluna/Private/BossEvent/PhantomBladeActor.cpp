// Capstone Project Helluna

#include "BossEvent/PhantomBladeActor.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

APhantomBladeActor::APhantomBladeActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BladeVFXComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("BladeVFX"));
	BladeVFXComp->SetupAttachment(SceneRoot);
	BladeVFXComp->SetAutoActivate(false);
}

void APhantomBladeActor::BeginPlay()
{
	Super::BeginPlay();

	if (BladeVFX && BladeVFXComp)
	{
		BladeVFXComp->SetAsset(BladeVFX);
		BladeVFXComp->SetWorldScale3D(FVector(BladeVFXScale));
		BladeVFXComp->Activate(true);
	}
}

void APhantomBladeActor::DeactivateVFX()
{
	if (BladeVFXComp)
	{
		BladeVFXComp->Deactivate();
	}
}
