// Fill out your copyright notice in the Description page of Project Settings.


#include "ECS/EnemyTemplateActor.h"
#include "Components/SkeletalMeshComponent.h"

AEnemyTemplateActor::AEnemyTemplateActor()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
    RootComponent = Mesh;

    // 초기 검증은 애셋 하나만 연결해도 충분 (ABP 없어도 됨)
    // 에디터에서 디테일 패널로 SkeletalMesh/AnimBP를 바꿔도 됩니다.
    Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Mesh->bCastDynamicShadow = false;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}