// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/HellunaDefenseGameState.h"
#include "Net/UnrealNetwork.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

#include "DebugHelper.h"

void AHellunaDefenseGameState::RegisterSpaceShip(AResourceUsingObject_SpaceShip* InShip)
{
	if (!HasAuthority())
		return;

	SpaceShip = InShip;
}


void AHellunaDefenseGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHellunaDefenseGameState, SpaceShip);
	DOREPLIFETIME(AHellunaDefenseGameState, Phase);
}

void AHellunaDefenseGameState::SetPhase(EDefensePhase NewPhase)
{
	if (!HasAuthority())
		return;

	Phase = NewPhase;
}

void AHellunaDefenseGameState::MulticastPrintNight_Implementation(int32 Current, int32 Need)
{
	Debug::Print(FString::Printf(TEXT("Night! SpaceShip Repair: %d / %d"), Current, Need));
}

void AHellunaDefenseGameState::MulticastPrintDay_Implementation()
{
	Debug::Print(TEXT("Day!"));
}
