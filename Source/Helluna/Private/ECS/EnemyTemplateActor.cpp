// Fill out your copyright notice in the Description page of Project Settings.


#include "ECS/EnemyTemplateActor.h"
#include "Components/SkeletalMeshComponent.h"

AEnemyTemplateActor::AEnemyTemplateActor()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
    RootComponent = Mesh;

    Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Mesh->bCastDynamicShadow = false;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}