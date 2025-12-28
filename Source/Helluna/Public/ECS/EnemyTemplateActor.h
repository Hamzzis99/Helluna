// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyTemplateActor.generated.h"

UCLASS()
class AEnemyTemplateActor : public AActor
{
    GENERATED_BODY()
public:
    AEnemyTemplateActor();

protected:
    UPROPERTY(VisibleAnywhere)
    class USkeletalMeshComponent* Mesh;
};