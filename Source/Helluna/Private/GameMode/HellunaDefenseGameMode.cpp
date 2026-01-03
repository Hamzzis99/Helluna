// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/HellunaDefenseGameMode.h"

#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"                 
#include "GameFramework/PlayerController.h"

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

	SetState(EDefenseGameModeState::Running);

	// 조건 체크 타이머
	GetWorldTimerManager().SetTimer(
		TimerHandle_ConditionCheck,
		this,
		&ThisClass::EvaluateConditions,
		ConditionCheckInterval,
		true
	);
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

	// 보스 스폰 포인트가 없으면 그냥 빈 배열로 두고, 소환 때 fallback 처리(0,0,0) 방지 가능
}

void AHellunaDefenseGameMode::SetState(EDefenseGameModeState NewState)
{
	if (!HasAuthority())
		return;

	if (CurrentState == NewState)
		return;

	const EDefenseGameModeState Old = CurrentState;
	CurrentState = NewState;

	OnDefenseStateChanged.Broadcast(Old, CurrentState);
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
	if (!HasAuthority())
		return;

	bBossReady = bReady;

	if (bBossReady && CanSummonBoss())
	{
		TrySummonBoss();
	}
}

void AHellunaDefenseGameMode::EvaluateConditions()
{
	if (!HasAuthority())
		return;

	// 이미 끝난 상태면 더 이상 체크하지 않음
	if (CurrentState == EDefenseGameModeState::Lose ||
		CurrentState == EDefenseGameModeState::Victory ||
		CurrentState == EDefenseGameModeState::BossSpawned)
	{
		return;
	}

	// ===== 여기서 "패배 조건"도 같이 체크 가능 =====
	// 예: Base 체력 0이면 TriggerDefeat() 호출
	// 지금은 트리거 액터에서 TriggerDefeat()를 호출하는 방식으로 두자.

	// 보스 소환 조건
	if (CanSummonBoss())
	{
		TrySummonBoss();
	}
}

bool AHellunaDefenseGameMode::CanSummonBoss() const
{
	if (CurrentState != EDefenseGameModeState::Running)
		return false;

	if (!bBossReady)
		return false;

	return true;
}

void AHellunaDefenseGameMode::TrySummonBoss()
{

	UE_LOG(LogTemp, Warning, TEXT("TrySummonBoss: State=%d Ready=%d BossClass=%s SpawnPoints=%d"),
		(int32)CurrentState, bBossReady, *GetNameSafe(BossClass), BossSpawnPoints.Num());

	if (!HasAuthority())
		return;

	if (!BossClass)
	{
		// BossClass 미지정이면 소환 안 함(크래시 방지)
		return;
	}

	// 스폰 위치 선택
	FVector SpawnLoc = FVector::ZeroVector;
	FRotator SpawnRot = FRotator::ZeroRotator;

	if (!BossSpawnPoints.IsEmpty())
	{
		ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
		SpawnLoc = TP->GetActorLocation() + FVector(0, 0, SpawnZOffset);
		SpawnRot = TP->GetActorRotation();
	}
	else
	{
		// 스폰포인트 없으면 월드 원점에 뜨는 사고를 막고 싶으면 여기서 return 해도 됨
		return;
	}

	FActorSpawnParameters Param;
	Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* Boss = GetWorld()->SpawnActor<APawn>(BossClass, SpawnLoc, SpawnRot, Param);
	if (Boss)
	{
		SetState(EDefenseGameModeState::BossSpawned);

		bBossReady = false;
	}
}

void AHellunaDefenseGameMode::TriggerLose()
{
	if (!HasAuthority())
		return;

	SetState(EDefenseGameModeState::Lose);

	RestartCurrentLevelForAll();
}

void AHellunaDefenseGameMode::TriggerVictory()
{
	if (!HasAuthority())
		return;

	SetState(EDefenseGameModeState::Victory);

}

void AHellunaDefenseGameMode::RestartCurrentLevelForAll()
{
	if (!HasAuthority()) return;

	GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}

void AHellunaDefenseGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	RestartPlayer(NewPlayer); // 최대한 빨리 Pawn 스폰/포제션
}
