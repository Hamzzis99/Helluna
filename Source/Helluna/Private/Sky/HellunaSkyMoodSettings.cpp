// Capstone Project Helluna — 하늘 무드 데이터 액터 (per-map, runtime)

#include "Sky/HellunaSkyMoodSettings.h"

AHellunaSkyMoodSettings::AHellunaSkyMoodSettings()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);

    // [Cosmic v3] Multi-client 안정성 — World Partition cell streaming/Net Relevancy로
    // 일부 클라에서 actor가 destroy되는 회귀 방지. 데이터 컨테이너라 항상 relevant 필요.
    bReplicates = true;
    bAlwaysRelevant = true;

    // [Cosmic v4] World Partition Always Loaded — cell streaming 영향 차단.
    // bAlwaysRelevant는 NetUpdate에만 적용되고 actor lifecycle은 별개라
    // 일부 클라에서 actor 자체가 destroy되는 회귀가 발생했음.
    // bIsSpatiallyLoaded는 WITH_EDITORONLY_DATA 가드 — Editor에서 설정하면 cook 시 반영되어
    // Client/Server runtime은 cooked data로 동작.
#if WITH_EDITORONLY_DATA
    bIsSpatiallyLoaded = false;
#endif
}
