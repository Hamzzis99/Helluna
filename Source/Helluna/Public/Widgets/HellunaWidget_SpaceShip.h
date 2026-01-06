// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HellunaWidget_SpaceShip.generated.h"

/**
 * 
 */

class AResourceUsingObject_SpaceShip;

UCLASS()
class HELLUNA_API UHellunaWidget_SpaceShip : public UUserWidget
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "Repair")
	void SetTargetShip(AResourceUsingObject_SpaceShip* InShip);

protected:
	UPROPERTY()
	TObjectPtr<AResourceUsingObject_SpaceShip> TargetShip;

	UFUNCTION()
	void OnRepairChanged(int32 Current, int32 Need);

	UFUNCTION(BlueprintImplementableEvent, Category = "Repair")
	void BP_UpdateRepair(int32 Current, int32 Need, float Percent);

	virtual void NativeDestruct() override;
	

};
