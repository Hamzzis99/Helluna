// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/HellunaDefenseGameMode.h"

#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"                 
#include "GameFramework/PlayerController.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"

#include "debughelper.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AHellunaDefenseGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	CacheBossSpawnPoints();

	EnterDay();
}

void AHellunaDefenseGameMode::CacheBossSpawnPoints()
{
	BossSpawnPoints.Empty();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

	for (AActor* A : Found)
	{
		ATargetPoint* TP = Cast<ATargetPoint>(A);
		if (!TP) continue;

		if (TP->ActorHasTag(BossSpawnPointTag))
			BossSpawnPoints.Add(TP);
	}
}

void AHellunaDefenseGameMode::TrySummonBoss()
{
	if (!HasAuthority())
		return;

	if (!BossClass)
		return;

	if (BossSpawnPoints.IsEmpty())
		return;

	ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];

	const FVector SpawnLoc = TP->GetActorLocation() + FVector(0, 0, SpawnZOffset);
	const FRotator SpawnRot = TP->GetActorRotation();

	FActorSpawnParameters Param;
	Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* Boss = GetWorld()->SpawnActor<APawn>(BossClass, SpawnLoc, SpawnRot, Param);

	if (Boss)
	{
		bBossReady = false;
	}
}

void AHellunaDefenseGameMode::RestartGame()
{
	if (!HasAuthority())
		return;

	GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));       //�Ŀ� �����ؾ��� ��?
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
	if (!HasAuthority())
		return;

	bBossReady = bReady;

	if (bBossReady)
	{
		TrySummonBoss();
	}
}

void AHellunaDefenseGameMode::EnterDay()
{
	if (!HasAuthority())
		return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (GS)
	{
		GS->SetPhase(EDefensePhase::Day);
		GS->MulticastPrintDay();
	}

	GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
	GetWorldTimerManager().SetTimer(
		TimerHandle_ToNight,
		this,
		&ThisClass::EnterNight,
		TestDayDuration,
		false
	);
}

void AHellunaDefenseGameMode::EnterNight()
{
	if (!HasAuthority())
		return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (GS)
	{
		GS->SetPhase(EDefensePhase::Night);
	}

	int32 Current = 0;
	int32 Need = 0;
	const bool bRepaired = IsSpaceShipFullyRepaired(Current, Need);

	if (GS)
	{
		GS->MulticastPrintNight(Current, Need);
	}

	if (bRepaired)
	{
		SetBossReady(true);  
		return;
	}

	GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
	GetWorldTimerManager().SetTimer(
		TimerHandle_ToDay,
		this,
		&ThisClass::EnterDay,
		TestNightFailToDayDelay,
		false
	);
}

bool AHellunaDefenseGameMode::IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const
{
	OutCurrent = 0;
	OutNeed = 0;

	const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS)
		return false;

	AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
	if (!Ship)
		return false;

	OutCurrent = Ship->GetCurrentResource();
	OutNeed = Ship->GetNeedResource();

	return (OutNeed > 0) && (OutCurrent >= OutNeed);
}