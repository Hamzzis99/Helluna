#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "ECS/Spawner/HellunaEnemyMassSpawner.h"
#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "debughelper.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Constructor"));
    UE_LOG(LogTemp, Warning, TEXT("  PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
    UE_LOG(LogTemp, Warning, TEXT("  DefaultPawnClass: %s"),      DefaultPawnClass      ? *DefaultPawnClass->GetName()      : TEXT("nullptr"));
}

void AHellunaDefenseGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    CacheBossSpawnPoints();
    CacheMonsterSpawnPoints();

    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] BeginPlay 완료 — BossSpawn: %d / MonsterSpawn: %d"),
        BossSpawnPoints.Num(), MonsterSpawnPoints.Num());
}

// ============================================================
// InitializeGame
// ============================================================
void AHellunaDefenseGameMode::InitializeGame()
{
    if (bGameInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 초기화됨, 스킵"));
        return;
    }
    bGameInitialized = true;

    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 게임 시작!"));
    Debug::Print(TEXT("[DefenseGameMode] InitializeGame - 게임 시작!"), FColor::Green);

    EnterDay();
    StartAutoSave();
}

// ============================================================
// 스폰 포인트 캐싱
// ============================================================
void AHellunaDefenseGameMode::CacheBossSpawnPoints()
{
    BossSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);
    for (AActor* A : Found)
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
            if (TP->ActorHasTag(BossSpawnPointTag))
                BossSpawnPoints.Add(TP);
}

void AHellunaDefenseGameMode::CacheMonsterSpawnPoints()
{
    MonsterSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);
    for (AActor* A : Found)
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
            if (TP->ActorHasTag(MonsterSpawnTag))
                MonsterSpawnPoints.Add(TP);

    Debug::Print(FString::Printf(
        TEXT("[CacheMonsterSpawnPoints] TargetPoint %d개 (태그: %s)"),
        MonsterSpawnPoints.Num(), *MonsterSpawnTag.ToString()),
        MonsterSpawnPoints.Num() > 0 ? FColor::Green : FColor::Red);
}

// ============================================================
// 낮/밤 시스템
// ============================================================
void AHellunaDefenseGameMode::EnterDay()
{
    if (!bGameInitialized) return;

    Debug::Print(TEXT("[EnterDay] 낮 시작"), FColor::Yellow);
    AliveMonsters.Empty();

    // 낮 전환 시 MassSpawner 의 대기 중인 스폰 타이머도 취소
    for (AHellunaEnemyMassSpawner* Spawner : CachedMassSpawners)
    {
        if (IsValid(Spawner))
            Spawner->CancelPendingSpawn();
    }

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Day);
        GS->SetAliveMonsterCount(0);
        GS->MulticastPrintDay();
        GS->NetMulticast_OnDawnPassed(TestDayDuration);
    }

    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);
}

void AHellunaDefenseGameMode::EnterNight()
{
    if (!HasAuthority() || !bGameInitialized) return;

    Debug::Print(TEXT("[EnterNight] 밤 시작"), FColor::Purple);

    AliveMonsters.Empty();
    TotalSpawnedThisNight      = 0;
    RemainingMonstersThisNight = 0;

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

    TriggerMassSpawning();
}

// ============================================================
// MassSpawner 트리거
// ============================================================
void AHellunaDefenseGameMode::TriggerMassSpawning()
{
    Debug::Print(TEXT("[TriggerMassSpawning] 진입"), FColor::Cyan);

    if (!HasAuthority()) return;

    if (!MassSpawnerClass)
    {
        Debug::Print(TEXT("[TriggerMassSpawning] MassSpawnerClass null — BP 에서 설정하세요"), FColor::Red);
        return;
    }

    // 첫 번째 밤: TargetPoint 위치마다 MassSpawner 동적 생성
    if (CachedMassSpawners.IsEmpty())
    {
        if (MonsterSpawnPoints.IsEmpty())
        {
            Debug::Print(FString::Printf(
                TEXT("[TriggerMassSpawning] MonsterSpawnPoints 없음 — TargetPoint 에 태그 '%s' 추가하세요"),
                *MonsterSpawnTag.ToString()), FColor::Red);
            return;
        }

        for (ATargetPoint* TP : MonsterSpawnPoints)
        {
            if (!IsValid(TP)) continue;

            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            AHellunaEnemyMassSpawner* Spawner = GetWorld()->SpawnActor<AHellunaEnemyMassSpawner>(
                MassSpawnerClass, TP->GetActorLocation(), TP->GetActorRotation(), Params);

            if (!IsValid(Spawner))
            {
                Debug::Print(TEXT("[TriggerMassSpawning] SpawnActor 실패"), FColor::Red);
                continue;
            }

            CachedMassSpawners.Add(Spawner);
            Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] MassSpawner 생성: %s"), *Spawner->GetName()), FColor::Green);
        }
    }

    // 매 밤: RequestSpawn() 호출
    for (AHellunaEnemyMassSpawner* Spawner : CachedMassSpawners)
    {
        if (!IsValid(Spawner)) continue;
        Spawner->RequestSpawn();
        Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] RequestSpawn: %s"), *Spawner->GetName()), FColor::Green);
    }
}

// ============================================================
// 몬스터 관리
// ============================================================
void AHellunaDefenseGameMode::AddSpawnedCount(int32 Count)
{
    TotalSpawnedThisNight      += Count;
    RemainingMonstersThisNight += Count;

    Debug::Print(FString::Printf(
        TEXT("[스폰 완료] 이번 밤 총 소환: %d"), TotalSpawnedThisNight),
        FColor::Cyan);
}

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

    // 카운터 차감 후 출력
    RemainingMonstersThisNight = FMath::Max(0, RemainingMonstersThisNight - 1);
    Debug::Print(FString::Printf(
        TEXT("죽은 몬스터: %s  |  남은 몬스터: %d / %d"),
        *DeadMonster->GetName(),
        RemainingMonstersThisNight,
        TotalSpawnedThisNight),
        FColor::Orange);

    if (AliveMonsters.Num() <= 0)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// ============================================================
// 보스 스폰
// ============================================================
void AHellunaDefenseGameMode::TrySummonBoss()
{
    if (!HasAuthority() || !bGameInitialized || !BossClass || BossSpawnPoints.IsEmpty()) return;

    ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
    FActorSpawnParameters Param;
    Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    APawn* Boss = GetWorld()->SpawnActor<APawn>(BossClass, TP->GetActorLocation() + FVector(0,0,SpawnZOffset), TP->GetActorRotation(), Param);
    if (Boss) bBossReady = false;
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
    if (!HasAuthority() || bBossReady == bReady) return;
    bBossReady = bReady;
    if (bBossReady) TrySummonBoss();
}

// ============================================================
// 우주선 상태 체크
// ============================================================
bool AHellunaDefenseGameMode::IsSpaceShipFullyRepaired(int32& OutCurrent, int32& OutNeed) const
{
    OutCurrent = OutNeed = 0;
    const AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return false;
    AResourceUsingObject_SpaceShip* Ship = GS->GetSpaceShip();
    if (!Ship) return false;
    OutCurrent = Ship->GetCurrentResource();
    OutNeed    = Ship->GetNeedResource();
    return (OutNeed > 0) && (OutCurrent >= OutNeed);
}

// ============================================================
// 게임 재시작
// ============================================================
void AHellunaDefenseGameMode::RestartGame()
{
    if (!HasAuthority()) return;
    bGameInitialized = false;
    GetWorld()->ServerTravel(TEXT("/Game/Minwoo/MinwooTestMap?listen"));
}
