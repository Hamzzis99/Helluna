// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HellunaBaseResourceUsingObject.generated.h"

class UBoxComponent;

UCLASS()
class HELLUNA_API AHellunaBaseResourceUsingObject : public AActor
{
	GENERATED_BODY()
	
public:	
	AHellunaBaseResourceUsingObject();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ResouceUsing")
	UStaticMeshComponent* ResouceUsingMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ResouceUsing")
	UBoxComponent* ResouceUsingCollisionBox;

	UFUNCTION()
	virtual void CollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	virtual void CollisionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);


	
	
};
