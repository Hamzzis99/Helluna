// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/HellunaDefenseGameMode.h"

#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"                 
#include "GameFramework/PlayerController.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "MDF_Function/MDF_Instance/MDF_GameInstance.h"
#include "Player/HellunaPlayerState.h"

#include "debughelper.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	bUseSeamlessTravel = true; // 모두가 맵이동을 할 시 같이 이동하게 설정하는 것.

	// ============================================
	// 📌 로그인 시스템용 PlayerState 클래스 설정
	// Seamless Travel 후에도 로그인 정보 유지를 위해 필수!
	// ============================================
	PlayerStateClass = AHellunaPlayerState::StaticClass();
}

void AHellunaDefenseGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	// ============================================
	// 📌 디버그: 현재 GameMode의 PlayerStateClass 확인
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] BeginPlay - PlayerStateClass: %s"), 
		PlayerStateClass ? *PlayerStateClass->GetName() : TEXT("nullptr"));

	CacheBossSpawnPoints();

	CacheMonsterSpawnPoints();

	EnterDay();
}

void AHellunaDefenseGameMode::Logout(AController* Exiting)
{
	// ============================================
	// 📌 플레이어 로그아웃 처리
	// PlayerState에서 ID 가져와서 GameInstance에서 제거
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ★★★ Logout 호출됨! Exiting: %s"), Exiting ? *GetNameSafe(Exiting) : TEXT("nullptr"));
	
	if (Exiting)
	{
		// PlayerState 타입 확인
		APlayerState* RawPS = Exiting->GetPlayerState<APlayerState>();
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] RawPlayerState: %s (Class: %s)"), 
			RawPS ? *GetNameSafe(RawPS) : TEXT("nullptr"),
			RawPS ? *RawPS->GetClass()->GetName() : TEXT("N/A"));

		if (AHellunaPlayerState* PS = Exiting->GetPlayerState<AHellunaPlayerState>())
		{
			FString PlayerId = PS->GetPlayerUniqueId();
			bool bIsLoggedIn = PS->IsLoggedIn();
			
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] HellunaPlayerState 찾음!"));
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - PlayerId: '%s'"), *PlayerId);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   - bIsLoggedIn: %s"), bIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));
			
			if (!PlayerId.IsEmpty())
			{
				if (UMDF_GameInstance* GI = Cast<UMDF_GameInstance>(UGameplayStatics::GetGameInstance(GetWorld())))
				{
					GI->RegisterLogout(PlayerId);
					UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ✅ 로그아웃 완료 - ID: %s"), *PlayerId);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] ❌ GameInstance를 찾을 수 없음!"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ⚠️ PlayerId가 비어있음!"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode] ❌ HellunaPlayerState로 Cast 실패! (다른 PlayerState 사용 중)"));
		}
	}

	Super::Logout(Exiting);
}

void AHellunaDefenseGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	// ============================================
	// 📌 SeamlessTravel 시 플레이어 정보 보존
	// LoginController → GameController로 교체될 때
	// PlayerState의 로그인 정보를 유지해야 함!
	// ============================================
	UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] ★ HandleSeamlessTravelPlayer 호출됨!"));
	
	// 기존 로그인 정보 저장
	FString SavedPlayerId;
	bool bSavedIsLoggedIn = false;
	
	if (C)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   Controller: %s"), *GetNameSafe(C));
		
		if (AHellunaPlayerState* OldPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			SavedPlayerId = OldPS->GetPlayerUniqueId();
			bSavedIsLoggedIn = OldPS->IsLoggedIn();
			
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   (저장) PlayerId: '%s', bIsLoggedIn: %s"), 
				*SavedPlayerId, bSavedIsLoggedIn ? TEXT("TRUE") : TEXT("FALSE"));
		}
	}

	// 부모 클래스 호출 (여기서 Controller 교체 발생)
	Super::HandleSeamlessTravelPlayer(C);
	
	// 새 PlayerState에 로그인 정보 복원
	if (C && !SavedPlayerId.IsEmpty())
	{
		if (AHellunaPlayerState* NewPS = C->GetPlayerState<AHellunaPlayerState>())
		{
			NewPS->SetLoginInfo(SavedPlayerId);
			UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode]   (복원) PlayerId: '%s' → 새 PlayerState에 복원 완료!"), *SavedPlayerId);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[DefenseGameMode]   ❌ 새 HellunaPlayerState를 찾을 수 없음!"));
		}
	}
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

void AHellunaDefenseGameMode::CacheMonsterSpawnPoints()
{
	MonsterSpawnPoints.Empty();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

	for (AActor* A : Found)
	{
		ATargetPoint* TP = Cast<ATargetPoint>(A);
		if (!TP) continue;

		// ✅ Tag가 MonsterSpawn 인 TargetPoint만 몬스터 스폰 포인트로 사용
		if (TP->ActorHasTag(MonsterSpawnPointTag))
		{
			MonsterSpawnPoints.Add(TP);
		}
	}

}

void AHellunaDefenseGameMode::SpawnTestMonsters()
{
	// ✅ 서버에서만 스폰
	if (!HasAuthority())
		return;

	// ✅ 클래스 지정 안 했으면 스폰 불가
	if (!TestMonsterClass)
	{
		Debug::Print(TEXT("[Defense] TestMonsterClass is null (GameMode BP에서 지정하세요)"), FColor::Red);
		return;
	}

	// ✅ 스폰 포인트가 없으면 스폰 불가
	if (MonsterSpawnPoints.IsEmpty())
	{
		Debug::Print(TEXT("[Defense] No MonsterSpawn TargetPoints (Tag=MonsterSpawn)"), FColor::Red);
		return;
	}

	// ✅ TestMonsterSpawnCount 만큼 스폰
	for (int32 i = 0; i < TestMonsterSpawnCount; ++i)
	{
		ATargetPoint* TP = MonsterSpawnPoints[FMath::RandRange(0, MonsterSpawnPoints.Num() - 1)];
		if (!TP) continue;

		const FVector SpawnLoc = TP->GetActorLocation();
		const FRotator SpawnRot = TP->GetActorRotation();

		FActorSpawnParameters Param;
		Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		APawn* Spawned = GetWorld()->SpawnActor<APawn>(TestMonsterClass, SpawnLoc, SpawnRot, Param);
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
	
	if (bBossReady == bReady)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SetBossReady] 이미 동일한 상태입니다. bBossReady=%s, 요청=%s"), 
			bBossReady ? TEXT("true") : TEXT("false"), 
			bReady ? TEXT("true") : TEXT("false"));
		return;
	}

	bBossReady = bReady;

	if (bBossReady)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SetBossReady] 보스 소환 시도!"));
		TrySummonBoss();
	}
}

void AHellunaDefenseGameMode::EnterDay()
{
	AliveMonsters.Empty();

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (GS)
	{
		// ✅ 모든 클라에게 “낮 상태”를 알려줌(Phase replicate)
		GS->SetPhase(EDefensePhase::Day);

		// ✅ 남은 몬스터 수는 0으로
		GS->SetAliveMonsterCount(0);

		// (기존 Debug 출력/수리 진행 출력 유지)
		GS->MulticastPrintDay();
	}

	// ✅ (기존 테스트 로직) 낮 10초 후 밤으로 전환
	GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
	GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);
}

void AHellunaDefenseGameMode::EnterNight()
{
	if (!HasAuthority())
		return;

	AliveMonsters.Empty();

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (GS)
	{
		GS->SetPhase(EDefensePhase::Night);
		GS->SetAliveMonsterCount(0);
	}

	int32 Current = 0, Need = 0;
	const bool bRepaired = IsSpaceShipFullyRepaired(Current, Need);

	if (GS)
		//GS->MulticastPrintNight(Current, Need);

	// ✅ 수리 완료면 보스 로직(기존 유지)
	if (bRepaired)
	{
		SetBossReady(true);
		return;
	}

	SpawnTestMonsters();
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

// ✅ 몬스터 등록: 스폰되자마자 호출해야 "몇 마리가 살아있는지" 카운트가 맞음
void AHellunaDefenseGameMode::RegisterAliveMonster(AActor* Monster)
{
	// ✅ GameMode는 서버 권한에서만 로직이 돌아야 함
	if (!HasAuthority() || !Monster)
		return;

	// ✅ 지금이 “밤”일 때만 추적(낮에는 무시)
	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS || GS->GetPhase() != EDefensePhase::Night)
		return;

	// ✅ 중복 등록 방지 (같은 액터를 2번 등록하면 카운트가 틀어짐)
	if (AliveMonsters.Contains(Monster))
		return;

	// ✅ 살아있는 몬스터 Set에 추가
	AliveMonsters.Add(Monster);

	// ✅ 클라 UI용 값은 GameState에 복제해서 업데이트
	GS->SetAliveMonsterCount(AliveMonsters.Num());

	Debug::Print(FString::Printf(TEXT("[Defense] Register Monster: %s | Alive=%d"),
		*GetNameSafe(Monster), AliveMonsters.Num()));
}

// ✅ 몬스터 사망 통지: HealthComp OnDeath에서 여기 호출됨
void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{

	if (!HasAuthority())
		return;

	if (!DeadMonster)
		return;

	AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
	if (!GS)
		return;

	// ✅ 살아있는 Set에서 제거
	AliveMonsters.Remove(TWeakObjectPtr<AActor>(DeadMonster));

	// ✅ 클라이언트 UI에 보여줄 남은 수 갱신
	GS->SetAliveMonsterCount(AliveMonsters.Num());

	Debug::Print(FString::Printf(
		TEXT("[Defense] Monster Died: %s | Alive=%d"),
		*GetNameSafe(DeadMonster),
		AliveMonsters.Num()
	));

	// ✅ 밤 상태에서 “살아있는 몬스터 수가 0”이 되면 -> 낮으로 전환
	if (AliveMonsters.Num() <= 0)
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);

		GetWorldTimerManager().SetTimer(
			TimerHandle_ToDay,
			this,
			&ThisClass::EnterDay,
			TestNightFailToDayDelay,
			false
		);
	}
}