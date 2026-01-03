// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Object/ResourceUsingObject/HellunaBaseResourceUsingObject.h"
#include "ResourceUsingObject_SpaceShip.generated.h"

/**
 * 
 */
UCLASS()
class HELLUNA_API AResourceUsingObject_SpaceShip : public AHellunaBaseResourceUsingObject
{
	GENERATED_BODY()
	
protected:
	virtual void CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;

	
	
};
