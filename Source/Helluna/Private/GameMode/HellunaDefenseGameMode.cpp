// ════════════════════════════════════════════════════════════════════════════════
// HellunaDefenseGameMode.cpp
// ════════════════════════════════════════════════════════════════════════════════
//
// 게임 로직 전용 GameMode
// 로그인/인벤토리 시스템은 HellunaBaseGameMode.cpp 참고
//
// 🎮 이 파일의 역할:
//    - InitializeGame() : 게임 시작
//    - EnterDay() / EnterNight() : 낮밤 전환
//    - SpawnTestMonsters() : 몬스터 스폰
//    - TrySummonBoss() : 보스 소환
//
// ════════════════════════════════════════════════════════════════════════════════

#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "debughelper.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
    // BaseGameMode에서 기본 설정 처리됨
    // ⚠️ BP에서 덮어쓰는 문제 방지를 위해 로그 추가
    UE_LOG(LogTemp, Warning, TEXT("⭐ [DefenseGameMode] Constructor 호출!"));
    UE_LOG(LogTemp, Warning, TEXT("⭐ PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
    UE_LOG(LogTemp, Warning, TEXT("⭐ DefaultPawnClass: %s"), DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("nullptr"));
}

void AHellunaDefenseGameMode::BeginPlay()
{
    Super::BeginPlay();  // BaseGameMode의 로그인/인벤토리 초기화 호출

    if (!HasAuthority())
        return;

    // 게임 로직 초기화만
    CacheBossSpawnPoints();
    CacheMonsterSpawnPoints();

    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] BeginPlay - 게임 로직 초기화 완료"));
    UE_LOG(LogTemp, Warning, TEXT("  - BossSpawnPoints: %d개"), BossSpawnPoints.Num());
    UE_LOG(LogTemp, Warning, TEXT("  - MonsterSpawnPoints: %d개"), MonsterSpawnPoints.Num());
}

// ════════════════════════════════════════════════════════════════════════════════
// ⭐⭐⭐ InitializeGame - 게임 로직 시작점 ⭐⭐⭐
// ════════════════════════════════════════════════════════════════════════════════
//
// 📌 호출 시점: 첫 번째 플레이어가 로그인 + 캐릭터 소환 완료 후
//
// 📌 이 함수가 호출되면:
//    - 게임이 본격적으로 시작됨
//    - EnterDay()가 호출되어 낮/밤 사이클 시작
//
// ✅ 팀원 작업: 게임 시작 시 필요한 초기화 로직을 여기에 추가하세요!
//    예시:
//    - 배경음악 재생
//    - 튜토리얼 시작
//    - UI 표시
//    - 환경 초기화
//
// ════════════════════════════════════════════════════════════════════════════════
void AHellunaDefenseGameMode::InitializeGame()
{
    if (bGameInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 초기화됨, 스킵"));
        return;
    }

    bGameInitialized = true;

    UE_LOG(LogTemp, Warning, TEXT(""));
    UE_LOG(LogTemp, Warning, TEXT("╔════════════════════════════════════════════════════════════╗"));
    UE_LOG(LogTemp, Warning, TEXT("║     [DefenseGameMode] InitializeGame 🎮                    ║"));
    UE_LOG(LogTemp, Warning, TEXT("║     첫 플레이어 캐릭터 소환 완료! 게임 시작!               ║"));
    UE_LOG(LogTemp, Warning, TEXT("╚════════════════════════════════════════════════════════════╝"));
    UE_LOG(LogTemp, Warning, TEXT(""));

    Debug::Print(TEXT("[DefenseGameMode] InitializeGame - 게임 시작!"), FColor::Green);

    // ════════════════════════════════════════════════════════════════════════════
    // ✅ 팀원 작업 영역 - 게임 시작 시 초기화 로직 추가
    // ════════════════════════════════════════════════════════════════════════════
    //
    // 여기에 게임 시작 시 필요한 코드를 추가하세요!
    //
    // 예시:
    // PlayBackgroundMusic();
    // ShowTutorialWidget();
    // InitializeEnvironment();
    //
    // ════════════════════════════════════════════════════════════════════════════

    // 낮/밤 사이클 시작
    EnterDay();

    // 자동저장 타이머 시작
    StartAutoSaveTimer();
}

// ════════════════════════════════════════════════════════════════════════════════
// 🗺️ 스폰 포인트 캐싱
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::CacheBossSpawnPoints()
{
    BossSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

    for (AActor* A : Found)
    {
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
        {
            if (TP->ActorHasTag(BossSpawnPointTag))
                BossSpawnPoints.Add(TP);
        }
    }
}

void AHellunaDefenseGameMode::CacheMonsterSpawnPoints()
{
    MonsterSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);

    for (AActor* A : Found)
    {
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
        {
            if (TP->ActorHasTag(MonsterSpawnPointTag))
                MonsterSpawnPoints.Add(TP);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// 🌅🌙 낮/밤 시스템
// ════════════════════════════════════════════════════════════════════════════════

// 🌅 EnterDay - 낮 시작
void AHellunaDefenseGameMode::EnterDay()
{
    if (!bGameInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] EnterDay 스킵 - 게임 미초기화"));
        return;
    }

    Debug::Print(TEXT("[DefenseGameMode] EnterDay - 낮 시작!"), FColor::Yellow);

    AliveMonsters.Empty();

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Day);
        GS->SetAliveMonsterCount(0);
        GS->MulticastPrintDay();
    }

    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);
}

// 🌙 EnterNight - 밤 시작
void AHellunaDefenseGameMode::EnterNight()
{
    if (!HasAuthority() || !bGameInitialized) return;

    Debug::Print(TEXT("[DefenseGameMode] EnterNight - 밤 시작!"), FColor::Purple);

    AliveMonsters.Empty();

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Night);
        GS->SetAliveMonsterCount(0);
    }

    int32 Current = 0, Need = 0;
    if (IsSpaceShipFullyRepaired(Current, Need))
    {
        SetBossReady(true);
        return;
    }

    SpawnTestMonsters();
}

// ════════════════════════════════════════════════════════════════════════════════
// 👾 몬스터/보스 스폰
// ════════════════════════════════════════════════════════════════════════════════

// 👾 SpawnTestMonsters - 몬스터 스폰
void AHellunaDefenseGameMode::SpawnTestMonsters()
{
    if (!HasAuthority() || !bGameInitialized) return;

    if (!TestMonsterClass)
    {
        Debug::Print(TEXT("[Defense] TestMonsterClass is null"), FColor::Red);
        return;
    }

    if (MonsterSpawnPoints.IsEmpty())
    {
        Debug::Print(TEXT("[Defense] No MonsterSpawn TargetPoints"), FColor::Red);
        return;
    }

    for (int32 i = 0; i < TestMonsterSpawnCount; ++i)
    {
        ATargetPoint* TP = MonsterSpawnPoints[FMath::RandRange(0, MonsterSpawnPoints.Num() - 1)];
        if (!TP) continue;

        FActorSpawnParameters Param;
        Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        GetWorld()->SpawnActor<APawn>(TestMonsterClass, TP->GetActorLocation(), TP->GetActorRotation(), Param);
    }
}

void AHellunaDefenseGameMode::TrySummonBoss()
{
    if (!HasAuthority() || !bGameInitialized || !BossClass || BossSpawnPoints.IsEmpty())
        return;

    ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
    const FVector SpawnLoc = TP->GetActorLocation() + FVector(0, 0, SpawnZOffset);

    FActorSpawnParameters Param;
    Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    APawn* Boss = GetWorld()->SpawnActor<APawn>(BossClass, SpawnLoc, TP->GetActorRotation(), Param);
    if (Boss) bBossReady = false;
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
    if (!HasAuthority() || bBossReady == bReady) return;
    bBossReady = bReady;
    if (bBossReady) TrySummonBoss();
}

// ════════════════════════════════════════════════════════════════════════════════
// 📊 몬스터 관리
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::RegisterAliveMonster(AActor* Monster)
{
    if (!HasAuthority() || !Monster || !bGameInitialized) return;

    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS || GS->GetPhase() != EDefensePhase::Night) return;

    if (AliveMonsters.Contains(Monster)) return;

    AliveMonsters.Add(Monster);
    GS->SetAliveMonsterCount(AliveMonsters.Num());
}

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
    if (!HasAuthority() || !DeadMonster || !bGameInitialized) return;

    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return;

    AliveMonsters.Remove(TWeakObjectPtr<AActor>(DeadMonster));
    GS->SetAliveMonsterCount(AliveMonsters.Num());

    if (AliveMonsters.Num() <= 0)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// 🚀 우주선 상태 체크
// ════════════════════════════════════════════════════════════════════════════════

bool AHellunaDefenseGameMode::IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const
{
    OutCurrent = 0;
    OutNeed = 0;

    const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return false;

    AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
    if (!Ship) return false;

    OutCurrent = Ship->GetCurrentResource();
    OutNeed = Ship->GetNeedResource();

    return (OutNeed > 0) && (OutCurrent >= OutNeed);
}

// ════════════════════════════════════════════════════════════════════════════════
// 🔄 게임 재시작
// ════════════════════════════════════════════════════════════════════════════════

void AHellunaDefenseGameMode::RestartGame()
{
    if (!HasAuthority()) return;

    bGameInitialized = false; // 리셋
    GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}
