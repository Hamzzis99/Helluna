// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "HellunaDefenseGameState.generated.h"

/**
 * 
 */

UENUM(BlueprintType)
enum class EDefensePhase : uint8
{
	Day,
	Night
};

class AResourceUsingObject_SpaceShip;

UCLASS()
class HELLUNA_API AHellunaDefenseGameState : public AGameStateBase
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintPure, Category = "Defense")
	AResourceUsingObject_SpaceShip* GetSpaceShip() const { return SpaceShip; }

	void RegisterSpaceShip(AResourceUsingObject_SpaceShip* InShip);

	UFUNCTION(BlueprintPure, Category = "Defense")
	EDefensePhase GetPhase() const { return Phase; }

	void SetPhase(EDefensePhase NewPhase);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPrintNight(int32 Current, int32 Need);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPrintDay();

protected:
	UPROPERTY(Replicated)
	TObjectPtr<AResourceUsingObject_SpaceShip> SpaceShip;

	UPROPERTY(Replicated)
	EDefensePhase Phase = EDefensePhase::Day;


	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	
	
};
