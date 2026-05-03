// Capstone Project Helluna — 하늘 무드 데이터 액터 (per-map, runtime)

#include "Sky/HellunaSkyMoodSettings.h"

AHellunaSkyMoodSettings::AHellunaSkyMoodSettings()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
}
