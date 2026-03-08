#include "GameMode/HellunaDefenseGameMode.h"
#include "GameMode/HellunaDefenseGameState.h"
#include "Object/ResourceUsingObject/ResourceUsingObject_SpaceShip.h"
#include "ECS/Spawner/HellunaEnemyMassSpawner.h"
#include "Character/HellunaEnemyCharacter.h"
#include "Engine/TargetPoint.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "debughelper.h"
#include "AIController.h"
#include "Components/StateTreeComponent.h"

// Phase 7 게임 종료 + 결과 반영
#include "Lobby/Database/HellunaSQLiteSubsystem.h"
#include "Player/HellunaPlayerState.h"
#include "Controller/HellunaHeroController.h"
#include "Character/HellunaHeroCharacter.h"
#include "Character/EnemyComponent/HellunaHealthComponent.h"
#include "InventoryManagement/Components/Inv_InventoryComponent.h"
#include "Player/Inv_PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Chat/HellunaChatTypes.h"
#include "Login/Controller/HellunaLoginController.h"  // [Fix50]
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

AHellunaDefenseGameMode::AHellunaDefenseGameMode()
{
#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] Constructor"));
    UE_LOG(LogTemp, Warning, TEXT("  PlayerControllerClass: %s"), PlayerControllerClass ? *PlayerControllerClass->GetName() : TEXT("nullptr"));
    UE_LOG(LogTemp, Warning, TEXT("  DefaultPawnClass: %s"),      DefaultPawnClass      ? *DefaultPawnClass->GetName()      : TEXT("nullptr"));
#endif
}

void AHellunaDefenseGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority()) return;

    // 커맨드라인 LobbyURL 오버라이드
    FString CmdLobbyURL;
    if (FParse::Value(FCommandLine::Get(), TEXT("-LobbyURL="), CmdLobbyURL))
    {
        LobbyServerURL = CmdLobbyURL;
        UE_LOG(LogHelluna, Warning, TEXT("[DefenseGameMode] 커맨드라인에서 LobbyServerURL 설정: %s"), *LobbyServerURL);
    }
    UE_LOG(LogHelluna, Warning, TEXT("[DefenseGameMode] LobbyServerURL = '%s'"), *LobbyServerURL);

    // Phase 12b: 서버 레지스트리 초기화
    {
        const FString RegistryDir = GetRegistryDirectoryPath();
        IFileManager::Get().MakeDirectory(*RegistryDir, true);
        WriteRegistryFile(TEXT("empty"), 0);
        UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] 서버 레지스트리 초기화 | Port=%d | Path=%s"), GetServerPort(), *GetRegistryFilePath());

        // 30초마다 하트비트
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().SetTimer(RegistryHeartbeatTimer, [this]()
            {
                const FString Status = CurrentPlayerCount > 0 ? TEXT("playing") : TEXT("empty");
                WriteRegistryFile(Status, CurrentPlayerCount);
            }, 30.0f, true);
        }
    }

    CacheBossSpawnPoints();
    CacheMeleeSpawnPoints();
    CacheRangeSpawnPoints();

    // [Phase 16] 유휴 자동 종료 타이머 (접속자 0이면 IdleShutdownSeconds 후 자동 종료)
    if (IdleShutdownSeconds > 0.f)
    {
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().SetTimer(IdleShutdownTimer, this,
                &AHellunaDefenseGameMode::CheckIdleShutdown, IdleShutdownSeconds, false);
            UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] [Phase16] 유휴 종료 타이머 시작 (%.0f초)"), IdleShutdownSeconds);
        }
    }

    // [Phase 19] 빈 상태에서 커맨드 파일 폴링 시작
    StartCommandPollTimer();

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] BeginPlay 완료 — BossSpawn:%d / MeleeSpawn:%d / RangeSpawn:%d"),
        BossSpawnPoints.Num(), MeleeSpawnPoints.Num(), RangeSpawnPoints.Num());
#endif
}

// ============================================================
// InitializeGame
// ============================================================
void AHellunaDefenseGameMode::InitializeGame()
{
    if (bGameInitialized)
    {
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 이미 초기화됨, 스킵"));
#endif
        return;
    }
    bGameInitialized = true;

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] 게임 시작!"));
#endif
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

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[CacheBossSpawnPoints] BossSpawnPoints: %d개 (태그: %s)"),
        BossSpawnPoints.Num(), *BossSpawnPointTag.ToString());
#endif
}

void AHellunaDefenseGameMode::CacheMeleeSpawnPoints()
{
    MeleeSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);
    for (AActor* A : Found)
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
            if (TP->ActorHasTag(MeleeSpawnTag))
                MeleeSpawnPoints.Add(TP);

    Debug::Print(FString::Printf(TEXT("[CacheMeleeSpawnPoints] %d개 (태그: %s)"),
        MeleeSpawnPoints.Num(), *MeleeSpawnTag.ToString()),
        MeleeSpawnPoints.Num() > 0 ? FColor::Green : FColor::Red);
}

void AHellunaDefenseGameMode::CacheRangeSpawnPoints()
{
    RangeSpawnPoints.Empty();
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(this, ATargetPoint::StaticClass(), Found);
    for (AActor* A : Found)
        if (ATargetPoint* TP = Cast<ATargetPoint>(A))
            if (TP->ActorHasTag(RangeSpawnTag))
                RangeSpawnPoints.Add(TP);

    Debug::Print(FString::Printf(TEXT("[CacheRangeSpawnPoints] %d개 (태그: %s)"),
        RangeSpawnPoints.Num(), *RangeSpawnTag.ToString()),
        RangeSpawnPoints.Num() > 0 ? FColor::Green : FColor::Red);
}

// CurrentDay에 맞는 NightSpawnConfig 반환
// FromDay <= CurrentDay 중 FromDay가 가장 큰 항목 선택
const FNightSpawnConfig* AHellunaDefenseGameMode::GetCurrentNightConfig() const
{
    const FNightSpawnConfig* Best = nullptr;
    for (const FNightSpawnConfig& Config : NightSpawnTable)
    {
        if (Config.FromDay <= CurrentDay)
        {
            if (!Best || Config.FromDay > Best->FromDay)
                Best = &Config;
        }
    }
    return Best;
}

// ============================================================
// 낮/밤 시스템
// ============================================================
void AHellunaDefenseGameMode::EnterDay()
{
    if (!bGameInitialized) return;

    // 낮 카운터 증가 (게임 시작 첫 낮은 Day 1)
    CurrentDay++;

    Debug::Print(FString::Printf(TEXT("[EnterDay] %d일차 낮 시작"), CurrentDay), FColor::Yellow);

    RemainingMonstersThisNight = 0;

    // 낮 전환 시 대기 중인 스폰 타이머 전부 취소
    for (AHellunaEnemyMassSpawner* Spawner : CachedMeleeSpawners)
        if (IsValid(Spawner)) Spawner->CancelPendingSpawn();
    for (AHellunaEnemyMassSpawner* Spawner : CachedRangeSpawners)
        if (IsValid(Spawner)) Spawner->CancelPendingSpawn();

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Day);
        GS->SetAliveMonsterCount(0);
        GS->SetCurrentDayForUI(CurrentDay);
        GS->SetDayTimeRemaining(TestDayDuration);
        GS->SetTotalMonstersThisNight(0);
        GS->SetIsBossNight(false);
        GS->MulticastPrintDay();

        // Phase 10: 채팅 시스템 메시지
        GS->BroadcastChatMessage(TEXT(""), TEXT("낮이 시작됩니다"), EChatMessageType::System);

        GS->NetMulticast_OnDawnPassed(TestDayDuration);
    }

    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);

    // 1초마다 남은 낮 시간 감소
    GetWorldTimerManager().ClearTimer(TimerHandle_DayCountdown);
    GetWorldTimerManager().SetTimer(TimerHandle_DayCountdown, this, &ThisClass::TickDayCountdown, 1.f, true);
}

void AHellunaDefenseGameMode::EnterNight()
{
    if (!HasAuthority() || !bGameInitialized) return;

    Debug::Print(FString::Printf(TEXT("[EnterNight] %d일차 밤 시작"), CurrentDay), FColor::Purple);

    RemainingMonstersThisNight = 0;

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Night);

        GS->SetDayTimeRemaining(0.f);   // 밤엔 낮 타이머 0
        // AliveMonsterCount는 TriggerMassSpawning/보스 소환 확정 후 설정

        GS->SetAliveMonsterCount(0);

        // Phase 10: 채팅 시스템 메시지
        GS->BroadcastChatMessage(TEXT(""), TEXT("밤이 시작됩니다"), EChatMessageType::System);

    }

    // 낮 카운트다운 타이머 정지
    GetWorldTimerManager().ClearTimer(TimerHandle_DayCountdown);

    // ── 보스 소환 일 체크 ──────────────────────────────────────────────
    // BossSchedule 배열에서 CurrentDay와 일치하는 항목을 찾는다.
    // 동일 Day 중복 시 첫 번째 항목만 사용.
    const FBossSpawnEntry* FoundEntry = BossSchedule.FindByPredicate(
        [this](const FBossSpawnEntry& E){ return E.SpawnDay == CurrentDay; });

#if HELLUNA_DEBUG_DEFENSE
    UE_LOG(LogTemp, Warning, TEXT("[EnterNight] BossSchedule 체크 — CurrentDay=%d | Schedule 항목수=%d | FoundEntry=%s"),
        CurrentDay, BossSchedule.Num(), FoundEntry ? TEXT("찾음 ✅") : TEXT("없음"));
#endif
    if (FoundEntry)
    {
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Warning, TEXT("[EnterNight] FoundEntry — SpawnDay=%d | BossClass=%s"),
            FoundEntry->SpawnDay,
            FoundEntry->BossClass ? *FoundEntry->BossClass->GetName() : TEXT("null ⚠️"));
#endif
    }

    if (FoundEntry)
    {
        Debug::Print(FString::Printf(
            TEXT("[EnterNight] %d일차 — %s 소환 대상, 일반 몬스터 스폰 생략"),
            CurrentDay,
            FoundEntry->BossClass ? *FoundEntry->BossClass->GetName() : TEXT("null")),
            FColor::Red);

        // 보스 1마리 소환 → UI용 몬스터 수 설정
        if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
        {
            GS->SetIsBossNight(true);
            GS->SetTotalMonstersThisNight(1);
            GS->SetAliveMonsterCount(1);
        }

        SetBossReady(true);
        TrySummonBoss(*FoundEntry);
        return;
    }

    int32 Current = 0, Need = 0;
    if (IsSpaceShipFullyRepaired(Current, Need))
    {
        // 우주선 수리 완료 시 BossSchedule과 무관하게 bBossReady만 세팅.
        // BossClass가 없는 폴백 상태이므로 별도 소환은 하지 않는다.
        SetBossReady(true);
        return;
    }

    TriggerMassSpawning();
}

// ============================================================
// TriggerMassSpawning — 밤 시작 시 근거리/원거리 몬스터 ECS 소환
//
// [소환 수 결정 우선순위]
//   1. NightSpawnTable에 CurrentDay에 맞는 FNightSpawnConfig가 있으면 해당 수 사용
//   2. 없으면 레거시 MassSpawnCountPerNight를 근거리에 적용 (원거리 0)
//
// [Spawner 생성 규칙]
//   - MeleeMassSpawnerClass가 설정되어 있으면 MeleeSpawnTag TargetPoint마다 근거리 Spawner 생성
//   - RangeMassSpawnerClass가 설정되어 있으면 RangeSpawnTag TargetPoint마다 원거리 Spawner 생성
//   - MeleeMassSpawnerClass가 없고 레거시 MassSpawnerClass가 있으면 MonsterSpawnTag로 폴백
//
// [소환 수 = Spawner 수 × 설정 값]
//   TargetPoint 2개 + MeleeCount=3 → 근거리 6마리 소환
// ============================================================
void AHellunaDefenseGameMode::TriggerMassSpawning()
{
    Debug::Print(TEXT("[TriggerMassSpawning] 진입"), FColor::Cyan);
    if (!HasAuthority()) return;

    // ── 이번 밤 소환 수 결정 ─────────────────────────────────────────
    int32 MeleeCount = MassSpawnCountPerNight; // NightSpawnTable 미설정 시 기본값
    int32 RangeCount = 0;

    if (const FNightSpawnConfig* Config = GetCurrentNightConfig())
    {
        MeleeCount = Config->MeleeCount;
        RangeCount = Config->RangeCount;
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] Day%d 설정 적용 (FromDay=%d) — 근거리:%d 원거리:%d"),
            CurrentDay, Config->FromDay, MeleeCount, RangeCount), FColor::Cyan);
    }
    else
    {
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] NightSpawnTable 미설정 — 기본값 근거리:%d 원거리:0"), MeleeCount), FColor::Yellow);
    }

    // ── 근거리 Spawner 초기화 (첫 번째 밤) ──────────────────────────
    if (MeleeMassSpawnerClass && CachedMeleeSpawners.IsEmpty())
    {
        if (MeleeSpawnPoints.IsEmpty())
            Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 근거리 스폰 포인트 없음 — TargetPoint에 태그 '%s' 추가하세요"), *MeleeSpawnTag.ToString()), FColor::Red);

        for (ATargetPoint* TP : MeleeSpawnPoints)
        {
            if (!IsValid(TP)) continue;
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            // SpawnActorDeferred: BeginPlay 전에 CDO 값이 올바르게 복사되도록 보장
            AHellunaEnemyMassSpawner* Spawner = GetWorld()->SpawnActorDeferred<AHellunaEnemyMassSpawner>(
                MeleeMassSpawnerClass, FTransform(TP->GetActorRotation(), TP->GetActorLocation()), nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (IsValid(Spawner))
            {
                UGameplayStatics::FinishSpawningActor(Spawner, FTransform(TP->GetActorRotation(), TP->GetActorLocation()));
                CachedMeleeSpawners.Add(Spawner);
                Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 근거리 Spawner 생성: %s | EntityTypes:%d"),
                    *Spawner->GetName(), Spawner->GetEntityTypesNum()), FColor::Green);
            }
        }
    }
    else if (!MeleeMassSpawnerClass)
        Debug::Print(TEXT("[TriggerMassSpawning] MeleeMassSpawnerClass 미설정 — BP에서 설정하세요"), FColor::Red);

    // ── 원거리 Spawner 초기화 (첫 번째 밤) ──────────────────────────
    if (RangeMassSpawnerClass && CachedRangeSpawners.IsEmpty())
    {
        if (RangeSpawnPoints.IsEmpty())
            Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 원거리 스폰 포인트 없음 — TargetPoint에 태그 '%s' 추가하세요"), *RangeSpawnTag.ToString()), FColor::Red);

        for (ATargetPoint* TP : RangeSpawnPoints)
        {
            if (!IsValid(TP)) continue;
            // SpawnActorDeferred: BeginPlay 전에 CDO 값이 올바르게 복사되도록 보장
            AHellunaEnemyMassSpawner* Spawner = GetWorld()->SpawnActorDeferred<AHellunaEnemyMassSpawner>(
                RangeMassSpawnerClass, FTransform(TP->GetActorRotation(), TP->GetActorLocation()), nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (IsValid(Spawner))
            {
                UGameplayStatics::FinishSpawningActor(Spawner, FTransform(TP->GetActorRotation(), TP->GetActorLocation()));
                CachedRangeSpawners.Add(Spawner);
                Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 원거리 Spawner 생성: %s | EntityTypes:%d"),
                    *Spawner->GetName(), Spawner->GetEntityTypesNum()), FColor::Green);
            }
        }
    }

    // ── 매 밤: RequestSpawn 호출 + 카운터 확정 ───────────────────────
    for (AHellunaEnemyMassSpawner* Spawner : CachedMeleeSpawners)
    {
        if (!IsValid(Spawner)) continue;
        Spawner->RequestSpawn(MeleeCount);
        RemainingMonstersThisNight += Spawner->GetRequestedSpawnCount();
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] 근거리 RequestSpawn(%d): %s | 누적: %d"),
            MeleeCount, *Spawner->GetName(), RemainingMonstersThisNight), FColor::Green);
    }

    for (AHellunaEnemyMassSpawner* Spawner : CachedRangeSpawners)
    {
        if (!IsValid(Spawner)) continue;
        if (RangeCount <= 0)    
        {
            Debug::Print(FString::Printf(TEXT("[TriggerMassSpawning] 원거리 0마리 — %s 스킵"), *Spawner->GetName()), FColor::Yellow);
            continue;
        }
        Spawner->RequestSpawn(RangeCount);
        RemainingMonstersThisNight += Spawner->GetRequestedSpawnCount();
        Debug::Print(FString::Printf(
            TEXT("[TriggerMassSpawning] 원거리 RequestSpawn(%d): %s | 누적: %d"),
            RangeCount, *Spawner->GetName(), RemainingMonstersThisNight), FColor::Green);
    }

    // 총 소환 수 확정 후 GameState에 반영 — Total과 Alive를 동시에 설정
    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetTotalMonstersThisNight(RemainingMonstersThisNight);
        GS->SetAliveMonsterCount(RemainingMonstersThisNight);
    }
}

// ============================================================
// RegisterAliveMonster — 하위 호환용 빈 함수
//
// 이전에는 EnemyCharacter::BeginPlay에서 이 함수를 호출해 AliveMonsters TSet에 등록했으나
// ECS 타이밍 이슈(Actor BeginPlay가 낮으로 넘어간 뒤 호출되어 등록 거부 → 미등록 몬스터 누적)로
// 카운터 기반 시스템으로 전환하면서 더 이상 사용하지 않음.
//
// 기존 BP나 EnemyCharacter에서 호출하는 코드가 있어도 문제없도록 빈 함수로 유지.
// 카운터는 TriggerMassSpawning의 RequestSpawn 호출 시점에 RemainingMonstersThisNight로 확정됨.
// ============================================================
void AHellunaDefenseGameMode::RegisterAliveMonster(AActor* Monster)
{
    // 의도적으로 비워둠 — 위 주석 참고
}

// ============================================================
// NotifyMonsterDied — 몬스터 사망 통보 및 낮 전환 판정
//
// GA_Death::HandleDeathFinished에서 호출됨.
// EnemyGrade에 따라 처리 경로 분기:
//   Normal   → RemainingMonstersThisNight 차감 → 0이 되면 낮 전환 타이머 시작
//   SemiBoss → NotifyBossDied 호출
//   Boss     → NotifyBossDied 호출
//
// [주의] 보스 BP에서 EnemyGrade를 Boss/SemiBoss로 설정하지 않으면
//        Normal 경로로 빠져 카운터만 차감되고 낮 전환이 되지 않음.
// ============================================================

void AHellunaDefenseGameMode::NotifyMonsterDied(AActor* DeadMonster)
{
    if (!HasAuthority() || !DeadMonster || !bGameInitialized) return;

    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return;

    // ── 보스/세미보스 분기 ────────────────────────────────────────────
    if (const AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(DeadMonster))
    {
        if (EnemyChar->EnemyGrade != EEnemyGrade::Normal)
        {
            NotifyBossDied(DeadMonster);
            return;
        }
    }

    // ── 일반 몬스터: 카운터 차감 ──────────────────────────────────────
    RemainingMonstersThisNight = FMath::Max(0, RemainingMonstersThisNight - 1);
    GS->SetAliveMonsterCount(RemainingMonstersThisNight); // UI 갱신

    if (RemainingMonstersThisNight <= 0)
    {
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// 보스몬스터 사망 로직 — NotifyMonsterDied에서 EnemyGrade != Normal 시 호출
void AHellunaDefenseGameMode::NotifyBossDied(AActor* DeadBoss)
{
    if (!HasAuthority() || !DeadBoss) return;

    AliveBoss.Reset();

    // 캐릭터 자체의 EnemyGrade로 등급 판별 (스케줄 조회 불필요)
    EEnemyGrade Grade = EEnemyGrade::Boss;
    if (const AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(DeadBoss))
    {
        Grade = EnemyChar->EnemyGrade;
    }

    FString TypeLabel;
    switch (Grade)
    {
    case EEnemyGrade::SemiBoss: TypeLabel = TEXT("세미보스"); break;
    case EEnemyGrade::Boss:     TypeLabel = TEXT("보스");     break;
    default:                    TypeLabel = TEXT("알 수 없음"); break;
    }

    Debug::Print(FString::Printf(
        TEXT("[%s 사망] %s 처치됨 — Day %d"),
        *TypeLabel, *DeadBoss->GetName(), CurrentDay),
        FColor::Red);

    // TODO: 보스/세미보스 사망 후속 처리 (보상, 연출, 클리어 조건 등) 이후 구현

    // 보스 사망 -> AliveMonsterCount 0으로 설정
    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetAliveMonsterCount(0);
    }

    // 최종 보스 처치 → 승리
    if (Grade == EEnemyGrade::Boss)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Victory] 최종 보스 처치! EndGame(Escaped) 호출"));
        EndGame(EHellunaGameEndReason::Escaped);
    }
    else
    {
        // 세미보스 처치 → 낮 전환
        UE_LOG(LogHelluna, Log, TEXT("[NotifyBossDied] 세미보스 처치 — 낮 전환 타이머 시작"));
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// ============================================================
// NotifyPlayerDied — 플레이어 사망 → 전원 사망 체크
// ============================================================
void AHellunaDefenseGameMode::NotifyPlayerDied(APlayerController* DeadPC)
{
    if (!HasAuthority() || !bGameInitialized || bGameEnded) return;

    UE_LOG(LogHelluna, Log, TEXT("[NotifyPlayerDied] %s 사망"), *GetNameSafe(DeadPC));

    // 생존자가 한 명이라도 있는지 확인
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        APawn* Pawn = PC->GetPawn();
        if (!IsValid(Pawn)) continue;

        UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>();
        if (HealthComp && !HealthComp->IsDead())
        {
            // 생존자 있음 → 게임 계속
            UE_LOG(LogHelluna, Log, TEXT("[NotifyPlayerDied] 생존자 있음: %s"), *GetNameSafe(PC));
            return;
        }
    }

    // 전원 사망 → 패배
    UE_LOG(LogHelluna, Warning, TEXT("[Defeat] 전원 사망! EndGame(AllDead) 호출"));
    EndGame(EHellunaGameEndReason::AllDead);
}

// ============================================================
// 보스/세미보스 스폰
// ============================================================
void AHellunaDefenseGameMode::TrySummonBoss(const FBossSpawnEntry& Entry)
{
    if (!HasAuthority() || !bGameInitialized) return;

    if (!Entry.BossClass)
    {
        Debug::Print(FString::Printf(
            TEXT("[TrySummonBoss] Day %d — BossClass null. BossSchedule 항목을 확인하세요."), CurrentDay),
            FColor::Red);
        return;
    }

    if (BossSpawnPoints.IsEmpty())
    {
        Debug::Print(TEXT("[TrySummonBoss] BossSpawnPoints 없음 — TargetPoint에 'BossSpawn' 태그를 추가하세요."), FColor::Red);
#if HELLUNA_DEBUG_DEFENSE
        UE_LOG(LogTemp, Error, TEXT("[TrySummonBoss] BossSpawnPoints 없음! BossSpawnPointTag=%s"), *BossSpawnPointTag.ToString());
#endif
        return;
    }

    ATargetPoint* TP = BossSpawnPoints[FMath::RandRange(0, BossSpawnPoints.Num() - 1)];
    if (!IsValid(TP))
    {
        Debug::Print(TEXT("[TrySummonBoss] 선택된 BossSpawnPoint가 유효하지 않습니다."), FColor::Red);
        return;
    }

    FActorSpawnParameters Param;
    Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    APawn* SpawnedBoss = GetWorld()->SpawnActor<APawn>(
        Entry.BossClass,
        TP->GetActorLocation() + FVector(0.f, 0.f, SpawnZOffset),
        TP->GetActorRotation(),
        Param);

    if (!IsValid(SpawnedBoss))
    {
        Debug::Print(FString::Printf(
            TEXT("[TrySummonBoss] SpawnActor 실패 — Class: %s"), *Entry.BossClass->GetName()),
            FColor::Red);
        return;
    }

    // ── 보스 스폰 직후 상태 진단 ──────────────────────────────────────
    Debug::Print(FString::Printf(TEXT("[TrySummonBoss] 소환 직후 진단 — %s"), *SpawnedBoss->GetName()), FColor::Cyan);
    Debug::Print(FString::Printf(TEXT("  Controller    : %s"),
        SpawnedBoss->GetController() ? *SpawnedBoss->GetController()->GetName() : TEXT("❌ 없음")), FColor::Cyan);
    Debug::Print(FString::Printf(TEXT("  AutoPossessAI : %d"), (int32)SpawnedBoss->AutoPossessAI), FColor::Cyan);

    if (AAIController* AIC = Cast<AAIController>(SpawnedBoss->GetController()))
    {
        UStateTreeComponent* STComp = AIC->FindComponentByClass<UStateTreeComponent>();
        Debug::Print(FString::Printf(TEXT("  StateTree     : %s"),
            STComp ? TEXT("✅ 있음") : TEXT("❌ 없음 — AIController BP에 StateTreeComponent 추가 필요")), FColor::Cyan);
    }

    if (AHellunaEnemyCharacter* BossChar = Cast<AHellunaEnemyCharacter>(SpawnedBoss))
    {
        Debug::Print(FString::Printf(TEXT("  EnemyGrade  : %s"),
            BossChar->EnemyGrade == EEnemyGrade::Boss     ? TEXT("Boss ✅") :
            BossChar->EnemyGrade == EEnemyGrade::SemiBoss ? TEXT("SemiBoss") :
                                                             TEXT("Normal ❌ — BP에서 EnemyGrade=Boss 로 설정 필요")), FColor::Cyan);
        BossChar->DebugPrintBossStatus();
    }

    AliveBoss = SpawnedBoss;
    bBossReady = false;

    // 소환된 액터의 EnemyGrade로 등급 표시
    FString BossTypeLabel = TEXT("보스 계열");
    if (const AHellunaEnemyCharacter* EnemyChar = Cast<AHellunaEnemyCharacter>(SpawnedBoss))
    {
        switch (EnemyChar->EnemyGrade)
        {
        case EEnemyGrade::SemiBoss: BossTypeLabel = TEXT("세미보스"); break;
        case EEnemyGrade::Boss:     BossTypeLabel = TEXT("보스");     break;
        default:                    BossTypeLabel = TEXT("Normal(경고: EnemyGrade 확인 필요)"); break;
        }
    }

    Debug::Print(FString::Printf(
        TEXT("[TrySummonBoss] %s 소환 완료: %s (Day %d)"),
        *BossTypeLabel, *SpawnedBoss->GetName(), CurrentDay),
        FColor::Red);
}

void AHellunaDefenseGameMode::SetBossReady(bool bReady)
{
    if (!HasAuthority() || bBossReady == bReady) return;
    bBossReady = bReady;
    // 소환은 EnterNight에서 TrySummonBoss(Entry) 직접 호출로 처리.
    // SetBossReady(true)는 우주선 수리 완료 폴백 경로에서만 사용.
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


void AHellunaDefenseGameMode::TickDayCountdown()
{
    AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>();
    if (!GS) return;

    float Remaining = FMath::Max(0.f, GS->DayTimeRemaining - 1.f);
    GS->SetDayTimeRemaining(Remaining);
}
// ============================================================
// Phase 10: PostLogin — 접속 채팅 + Phase 12b 레지스트리
// ============================================================
void AHellunaDefenseGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    // Phase 12b: 레지스트리 갱신
    CurrentPlayerCount++;
    WriteRegistryFile(TEXT("playing"), CurrentPlayerCount);
    UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] PostLogin 레지스트리 갱신 | Players=%d"), CurrentPlayerCount);

    // [Phase 16] 유휴 종료 타이머 해제 (첫 접속 시)
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(IdleShutdownTimer);
    }

    // [Phase 19] 커맨드 폴링 중지 (플레이어 접속)
    StopCommandPollTimer();

    // Phase 10: 접속 메시지
    if (bGameInitialized && IsValid(NewPlayer))
    {
        FString PlayerName;
        if (AHellunaPlayerState* HellunaPS = NewPlayer->GetPlayerState<AHellunaPlayerState>())
        {
            PlayerName = HellunaPS->GetPlayerUniqueId();
        }
        if (PlayerName.IsEmpty())
        {
            PlayerName = GetNameSafe(NewPlayer);
        }

        if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
        {
            GS->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 접속했습니다"), *PlayerName), EChatMessageType::System);
        }
    }
}

// ============================================================
// Phase 7: 게임 종료
// ============================================================

// 콘솔 커맨드 핸들러 (디버그용)
void AHellunaDefenseGameMode::CmdEndGame(const TArray<FString>& Args, UWorld* World)
{
    if (!World) return;

    AHellunaDefenseGameMode* GM = Cast<AHellunaDefenseGameMode>(World->GetAuthGameMode());
    if (!GM)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndGame 커맨드: DefenseGameMode가 아닌 맵에서 호출됨"));
        return;
    }

    EHellunaGameEndReason Reason = EHellunaGameEndReason::Escaped;
    if (Args.Num() > 0)
    {
        if (Args[0].Equals(TEXT("AllDead"), ESearchCase::IgnoreCase))
        {
            Reason = EHellunaGameEndReason::AllDead;
        }
        else if (Args[0].Equals(TEXT("ServerShutdown"), ESearchCase::IgnoreCase))
        {
            Reason = EHellunaGameEndReason::ServerShutdown;
        }
    }

    GM->EndGame(Reason);
}

static FAutoConsoleCommandWithWorldAndArgs GCmdEndGame(
    TEXT("EndGame"),
    TEXT("Phase 7: 게임 종료. 사용법: EndGame [Escaped|AllDead|ServerShutdown]"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AHellunaDefenseGameMode::CmdEndGame)
);

// EndGame 메인 함수
void AHellunaDefenseGameMode::EndGame(EHellunaGameEndReason Reason)
{
    if (!HasAuthority()) return;

    if (bGameEnded)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndGame: 이미 게임이 종료됨, 스킵"));
        return;
    }

    bGameEnded = true;

    UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndGame — Reason: %s | LobbyServerURL: '%s'"),
        Reason == EHellunaGameEndReason::Escaped ? TEXT("탈출 성공") :
        Reason == EHellunaGameEndReason::AllDead ? TEXT("전원 사망") :
        Reason == EHellunaGameEndReason::ServerShutdown ? TEXT("서버 셧다운") : TEXT("None"),
        *LobbyServerURL);

    // 낮/밤 타이머 정지
    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);

    FString ReasonString;
    switch (Reason)
    {
    case EHellunaGameEndReason::Escaped:       ReasonString = TEXT("탈출 성공"); break;
    case EHellunaGameEndReason::AllDead:        ReasonString = TEXT("전원 사망"); break;
    case EHellunaGameEndReason::ServerShutdown: ReasonString = TEXT("서버 셧다운"); break;
    default:                                    ReasonString = TEXT("알 수 없음"); break;
    }

    // 각 플레이어 처리
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC)) continue;

        bool bSurvived = false;
        if (Reason != EHellunaGameEndReason::AllDead)
        {
            APawn* Pawn = PC->GetPawn();
            if (IsValid(Pawn))
            {
                UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>();
                if (HealthComp && !HealthComp->IsDead())
                {
                    bSurvived = true;
                }
            }
        }

        UE_LOG(LogHelluna, Log, TEXT("[Phase7] EndGame: Player=%s Survived=%s"),
            *GetNameSafe(PC), bSurvived ? TEXT("Yes") : TEXT("No"));

        ProcessPlayerGameResult(PC, bSurvived);

        AHellunaHeroController* HeroPC = Cast<AHellunaHeroController>(PC);
        if (HeroPC)
        {
            TArray<FInv_SavedItemData> ResultItems;
            if (bSurvived)
            {
                AInv_PlayerController* InvPC = Cast<AInv_PlayerController>(PC);
                if (InvPC)
                {
                    UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
                    if (InvComp)
                    {
                        ResultItems = InvComp->CollectInventoryDataForSave();
                    }
                }
            }

            // [Fix15] 네트워크 최적화: SerializedManifest 제거
            TArray<FInv_SavedItemData> LightweightItems;
            LightweightItems.Reserve(ResultItems.Num());
            for (const FInv_SavedItemData& Item : ResultItems)
            {
                FInv_SavedItemData LightItem;
                LightItem.ItemType = Item.ItemType;
                LightItem.StackCount = Item.StackCount;
                LightItem.GridPosition = Item.GridPosition;
                LightItem.GridCategory = Item.GridCategory;
                LightItem.bEquipped = Item.bEquipped;
                LightItem.WeaponSlotIndex = Item.WeaponSlotIndex;
                LightweightItems.Add(MoveTemp(LightItem));
            }

            HeroPC->Client_ShowGameResult(LightweightItems, bSurvived, ReasonString, LobbyServerURL);
        }
    }

    // [Phase 14b] DisconnectedPlayers 처리 — 끊겨있으므로 사망 취급
    {
        UGameInstance* GI = GetGameInstance();
        UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;

        for (auto& Pair : DisconnectedPlayers)
        {
            FDisconnectedPlayerData& Data = Pair.Value;
            GetWorldTimerManager().ClearTimer(Data.GraceTimerHandle);

            if (DB)
            {
                TArray<FInv_SavedItemData> EmptyItems;
                DB->ExportGameResultToFile(Data.PlayerId, EmptyItems, false);
                UE_LOG(LogHelluna, Log, TEXT("[Phase14] EndGame: 끊긴 플레이어 사망 처리 | PlayerId=%s"), *Data.PlayerId);
            }

            if (Data.PreservedPawn.IsValid())
            {
                Data.PreservedPawn->Destroy();
            }
        }
        DisconnectedPlayers.Empty();
    }

    UE_LOG(LogHelluna, Log, TEXT("[Phase7] EndGame 완료 — 모든 플레이어 결과 처리됨"));

    // [Phase 16] EndGame 후 서버 자동 종료 (레지스트리 삭제 + RequestExit)
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimer(ShutdownTimer, [this]()
        {
            UE_LOG(LogHelluna, Log, TEXT("[Phase16] 서버 자동 종료 실행"));
            DeleteRegistryFile();
            FGenericPlatformMisc::RequestExit(false);
        }, ShutdownDelaySeconds, false);

        UE_LOG(LogHelluna, Log, TEXT("[Phase16] 서버 자동 종료 타이머 시작 (%.0f초 후)"), ShutdownDelaySeconds);
    }
}

// ============================================================
// ProcessPlayerGameResult
// ============================================================
void AHellunaDefenseGameMode::ProcessPlayerGameResult(APlayerController* PC, bool bSurvived)
{
    if (!IsValid(PC)) return;

    FString PlayerId = GetPlayerSaveId(PC);
    if (PlayerId.IsEmpty())
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] ProcessPlayerGameResult: PlayerId가 비어있음 | PC=%s"),
            *GetNameSafe(PC));
        return;
    }

    UGameInstance* GI = GetGameInstance();
    UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
    if (!DB)
    {
        UE_LOG(LogHelluna, Error, TEXT("[Phase7] ProcessPlayerGameResult: SQLite 서브시스템 없음 | PlayerId=%s"), *PlayerId);
        return;
    }

    TArray<FInv_SavedItemData> ResultItems;
    if (bSurvived)
    {
        UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>();
        if (InvComp)
        {
            ResultItems = InvComp->CollectInventoryDataForSave();
            UE_LOG(LogHelluna, Log, TEXT("[Phase7] 생존자 아이템 수집: %d개 | PlayerId=%s"),
                ResultItems.Num(), *PlayerId);
        }
    }
    else
    {
        UE_LOG(LogHelluna, Log, TEXT("[Phase7] 사망자: 아이템 전부 손실 | PlayerId=%s"), *PlayerId);
    }

    if (DB->ExportGameResultToFile(PlayerId, ResultItems, bSurvived))
    {
        UE_LOG(LogHelluna, Log, TEXT("[Phase7] ExportGameResultToFile 성공 | PlayerId=%s | Items=%d | Survived=%s"),
            *PlayerId, ResultItems.Num(), bSurvived ? TEXT("Y") : TEXT("N"));
    }
    else
    {
        UE_LOG(LogHelluna, Error, TEXT("[Phase7] ExportGameResultToFile 실패! | PlayerId=%s"), *PlayerId);
    }
}

// ============================================================
// Logout — Phase 10 채팅 + Phase 7 사망 처리 + Phase 12b 레지스트리
// ============================================================
void AHellunaDefenseGameMode::Logout(AController* Exiting)
{
    // [Fix50] LoginController → HeroController 스왑 시 Logout 스킵
    // SwapPlayerControllers가 LoginController를 파괴하면 Logout이 호출되지만,
    // 이것은 실제 플레이어 이탈이 아닌 컨트롤러 교체.
    if (AHellunaLoginController* ExitingLC = Cast<AHellunaLoginController>(Exiting))
    {
        AHellunaPlayerState* PS = ExitingLC->GetPlayerState<AHellunaPlayerState>();
        bool bIsControllerSwap = (!PS || PS->GetPlayerUniqueId().IsEmpty());
        if (bIsControllerSwap)
        {
            UE_LOG(LogHelluna, Log, TEXT("[Fix50] LoginController 스왑 감지 — Logout 처리 스킵 | Controller=%s"),
                *GetNameSafe(Exiting));
            Super::Logout(Exiting);
            return;
        }
    }

    // Phase 10: 퇴장 메시지
    if (bGameInitialized)
    {
        APlayerController* ExitPC = Cast<APlayerController>(Exiting);
        if (IsValid(ExitPC))
        {
            FString PlayerName;
            if (AHellunaPlayerState* HellunaPS = ExitPC->GetPlayerState<AHellunaPlayerState>())
            {
                PlayerName = HellunaPS->GetPlayerUniqueId();
            }
            if (PlayerName.IsEmpty())
            {
                PlayerName = GetNameSafe(ExitPC);
            }

            if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
            {
                GS->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 퇴장했습니다"), *PlayerName), EChatMessageType::System);
            }
        }
    }

    // [Phase 14b] 게임 진행 중이면 Grace Period 시작 (즉시 사망 대신 상태 보존)
    if (bGameInitialized && !bGameEnded)
    {
        APlayerController* PC = Cast<APlayerController>(Exiting);
        if (IsValid(PC))
        {
            const FString PlayerId = GetPlayerSaveId(PC);
            APawn* Pawn = PC->GetPawn();

            if (!PlayerId.IsEmpty() && IsValid(Pawn))
            {
                UE_LOG(LogHelluna, Warning, TEXT("[Phase14] Logout 중 게임 진행 중 — Grace Period 시작 (%0.f초) | Player=%s"),
                    DisconnectGracePeriodSeconds, *PlayerId);

                FDisconnectedPlayerData Data;
                Data.PlayerId = PlayerId;

                // 영웅타입 추출
                if (AHellunaPlayerState* HellunaPS2 = PC->GetPlayerState<AHellunaPlayerState>())
                {
                    Data.HeroType = HellunaPS2->GetSelectedHeroType();
                }

                // 인벤토리 저장
                if (UInv_InventoryComponent* InvComp = PC->FindComponentByClass<UInv_InventoryComponent>())
                {
                    Data.SavedInventory = InvComp->CollectInventoryDataForSave();
                }

                // 위치/회전 저장
                Data.LastLocation = Pawn->GetActorLocation();
                Data.LastRotation = Pawn->GetActorRotation();

                // 체력 저장
                if (UHellunaHealthComponent* HealthComp = Pawn->FindComponentByClass<UHellunaHealthComponent>())
                {
                    Data.Health = HealthComp->GetHealth();
                    Data.MaxHealth = HealthComp->GetMaxHealth();
                }

                // Pawn Unpossess + 숨김 (월드에 유지)
                PC->UnPossess();
                Pawn->SetActorHiddenInGame(true);
                Pawn->SetActorEnableCollision(false);
                Data.PreservedPawn = Pawn;

                // Grace 타이머 시작
                FTimerDelegate TimerDelegate;
                TimerDelegate.BindUObject(this, &AHellunaDefenseGameMode::OnGracePeriodExpired, PlayerId);
                GetWorldTimerManager().SetTimer(Data.GraceTimerHandle, TimerDelegate, DisconnectGracePeriodSeconds, false);

                DisconnectedPlayers.Add(PlayerId, MoveTemp(Data));

                // 채팅으로 알림
                if (AHellunaDefenseGameState* GS2 = GetGameState<AHellunaDefenseGameState>())
                {
                    GS2->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 연결이 끊겼습니다 (%.0f초 대기)"),
                        *Data.PlayerId, DisconnectGracePeriodSeconds), EChatMessageType::System);
                }
            }
            else
            {
                // PlayerId 없거나 Pawn 없으면 기존 사망 처리
                UE_LOG(LogHelluna, Warning, TEXT("[Phase14] Logout: PlayerId/Pawn 없음 → 즉시 사망 처리 | Player=%s"),
                    *GetNameSafe(PC));
                ProcessPlayerGameResult(PC, false);
            }
        }
    }

    // Phase 12b: 레지스트리 갱신
    CurrentPlayerCount = FMath::Max(0, CurrentPlayerCount - 1);
    WriteRegistryFile(CurrentPlayerCount > 0 ? TEXT("playing") : TEXT("empty"), CurrentPlayerCount);
    UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] Logout 레지스트리 갱신 | Players=%d"), CurrentPlayerCount);

    // [Phase 16] 전원 이탈 시 유휴 종료 타이머 재시작
    if (CurrentPlayerCount == 0 && !bGameEnded && IdleShutdownSeconds > 0.f)
    {
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().SetTimer(IdleShutdownTimer, this,
                &AHellunaDefenseGameMode::CheckIdleShutdown, IdleShutdownSeconds, false);
            UE_LOG(LogHelluna, Log, TEXT("[Phase16] 전원 이탈 — 유휴 종료 타이머 재시작 (%.0f초)"), IdleShutdownSeconds);
        }
    }

    // [Phase 19] 전원 이탈 → 커맨드 폴링 재시작
    if (CurrentPlayerCount == 0 && !bGameEnded)
    {
        StartCommandPollTimer();
    }

    Super::Logout(Exiting);
}

// ============================================================
// EndPlay — Phase 7 셧다운 + Phase 12b 레지스트리 정리
// ============================================================
void AHellunaDefenseGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority() && bGameInitialized && !bGameEnded)
    {
        UE_LOG(LogHelluna, Warning, TEXT("[Phase7] EndPlay: 서버 셧다운 — EndGame(ServerShutdown) 호출"));
        EndGame(EHellunaGameEndReason::ServerShutdown);
    }

    // [Phase 14b] Grace 타이머 정리 (EndGame에서 이미 처리됐지만 안전장치)
    for (auto& Pair : DisconnectedPlayers)
    {
        GetWorldTimerManager().ClearTimer(Pair.Value.GraceTimerHandle);
    }
    DisconnectedPlayers.Empty();

    // Phase 12b: 하트비트 타이머 정리 + 레지스트리 파일 삭제
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(RegistryHeartbeatTimer);
        // [Phase 16] 종료/유휴 타이머 정리
        W->GetTimerManager().ClearTimer(ShutdownTimer);
        W->GetTimerManager().ClearTimer(IdleShutdownTimer);
    }
    DeleteRegistryFile();
    UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] EndPlay: 레지스트리 파일 삭제"));

    Super::EndPlay(EndPlayReason);
}

// ============================================================
// [Phase 14b] 재참가 시스템
// ============================================================

void AHellunaDefenseGameMode::OnGracePeriodExpired(FString PlayerId)
{
    UE_LOG(LogHelluna, Warning, TEXT("[Phase14] Grace Period 만료 → 사망 처리 | PlayerId=%s"), *PlayerId);

    FDisconnectedPlayerData* Data = DisconnectedPlayers.Find(PlayerId);
    if (!Data) return;

    // GameResult 파일 내보내기 (빈 배열 = 사망)
    UGameInstance* GI = GetGameInstance();
    UHellunaSQLiteSubsystem* DB = GI ? GI->GetSubsystem<UHellunaSQLiteSubsystem>() : nullptr;
    if (DB)
    {
        TArray<FInv_SavedItemData> EmptyItems;
        DB->ExportGameResultToFile(PlayerId, EmptyItems, false);
        UE_LOG(LogHelluna, Log, TEXT("[Phase14] Grace 만료: ExportGameResultToFile (사망) | PlayerId=%s"), *PlayerId);
    }

    // 보존된 Pawn 파괴
    if (Data->PreservedPawn.IsValid())
    {
        Data->PreservedPawn->Destroy();
    }

    // 레지스트리에서 연결 끊김 플레이어 제거
    DisconnectedPlayers.Remove(PlayerId);
    WriteRegistryFile(TEXT("playing"), CurrentPlayerCount);
}

bool AHellunaDefenseGameMode::HasDisconnectedPlayer(const FString& PlayerId) const
{
    return DisconnectedPlayers.Contains(PlayerId);
}

void AHellunaDefenseGameMode::RestoreReconnectedPlayer(APlayerController* PC, const FString& PlayerId)
{
    FDisconnectedPlayerData* Data = DisconnectedPlayers.Find(PlayerId);
    if (!Data)
    {
        UE_LOG(LogHelluna, Error, TEXT("[Phase14] RestoreReconnectedPlayer: 데이터 없음 | PlayerId=%s"), *PlayerId);
        return;
    }

    UE_LOG(LogHelluna, Log, TEXT("[Phase14] ▶ RestoreReconnectedPlayer 시작 | PlayerId=%s | HeroType=%d | Items=%d"),
        *PlayerId, static_cast<int32>(Data->HeroType), Data->SavedInventory.Num());

    // 1. Grace 타이머 취소
    GetWorldTimerManager().ClearTimer(Data->GraceTimerHandle);

    // 2. 보존된 Pawn 파괴 (새로 스폰할 것이므로)
    if (Data->PreservedPawn.IsValid())
    {
        Data->PreservedPawn->Destroy();
    }

    // 3. 저장된 위치에 새 캐릭터 스폰
    // HeroType 설정
    if (AHellunaPlayerState* PS = PC->GetPlayerState<AHellunaPlayerState>())
    {
        PS->SetSelectedHeroType(Data->HeroType);
    }

    // SpawnHeroCharacter 사용 (HellunaBaseGameMode 가상 함수)
    // 위치 오버라이드를 위해 약간의 지연 후 스폰
    const FVector SpawnLoc = Data->LastLocation;
    const FRotator SpawnRot = Data->LastRotation;
    const float SavedHealth = Data->Health;
    const float SavedMaxHealth = Data->MaxHealth;
    const TArray<FInv_SavedItemData> SavedItems = Data->SavedInventory;

    // DisconnectedPlayers에서 제거 (데이터 복사 완료)
    DisconnectedPlayers.Remove(PlayerId);

    // 스폰 딜레이 (Controller 초기화 대기)
    FTimerHandle& SpawnTimer = LambdaTimerHandles.AddDefaulted_GetRef();
    GetWorldTimerManager().SetTimer(SpawnTimer, [this, PC, PlayerId, SpawnLoc, SpawnRot, SavedHealth, SavedMaxHealth, SavedItems]()
    {
        if (!IsValid(PC)) return;

        // 캐릭터 스폰 (기본 스폰 흐름 사용)
        SpawnHeroCharacter(PC);

        // 0.3초 추가 딜레이: Pawn 스폰 + Possess 완료 대기
        FTimerHandle& RestoreTimer = LambdaTimerHandles.AddDefaulted_GetRef();
        GetWorldTimerManager().SetTimer(RestoreTimer, [this, PC, PlayerId, SpawnLoc, SpawnRot, SavedHealth, SavedMaxHealth, SavedItems]()
        {
            if (!IsValid(PC)) return;

            APawn* Pawn = PC->GetPawn();
            if (!IsValid(Pawn)) return;

            // 위치 복원
            Pawn->SetActorLocationAndRotation(SpawnLoc, SpawnRot);

            // 체력 복원
            if (UHellunaHealthComponent* HC = Pawn->FindComponentByClass<UHellunaHealthComponent>())
            {
                HC->SetHealth(SavedHealth);
            }

            // 인벤토리 복원 (PreCachedInventoryMap 활용)
            FInv_PlayerSaveData CachedData;
            CachedData.Items = SavedItems;
            PreCachedInventoryMap.Add(PlayerId, MoveTemp(CachedData));
            LoadAndSendInventoryToClient(PC);

            UE_LOG(LogHelluna, Log, TEXT("[Phase14] ✓ RestoreReconnectedPlayer 완료 | PlayerId=%s | Loc=%s"),
                *PlayerId, *SpawnLoc.ToString());

            // 채팅 알림
            if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
            {
                GS->BroadcastChatMessage(TEXT(""), FString::Printf(TEXT("%s 님이 재접속했습니다"), *PlayerId), EChatMessageType::System);
            }
        }, 0.3f, false);
    }, 0.5f, false);
}

// ============================================================
// Phase 12b: 서버 레지스트리 헬퍼 함수
// ============================================================

FString AHellunaDefenseGameMode::GetRegistryDirectoryPath() const
{
    return FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ServerRegistry")));
}

int32 AHellunaDefenseGameMode::GetServerPort() const
{
    UWorld* World = GetWorld();
    if (World)
    {
        return World->URL.Port;
    }
    return 7777;
}

FString AHellunaDefenseGameMode::GetRegistryFilePath() const
{
    const int32 Port = GetServerPort();
    return FPaths::Combine(GetRegistryDirectoryPath(), FString::Printf(TEXT("channel_%d.json"), Port));
}

void AHellunaDefenseGameMode::WriteRegistryFile(const FString& Status, int32 PlayerCount)
{
    const int32 Port = GetServerPort();
    const FString ChannelId = FString::Printf(TEXT("channel_%d"), Port);
    const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("Unknown");
    const FString LastUpdate = FDateTime::UtcNow().ToIso8601();

    // [Phase 14g] disconnectedPlayers 배열 생성
    FString DisconnectedArray = TEXT("[");
    {
        bool bFirst = true;
        for (const auto& Pair : DisconnectedPlayers)
        {
            if (!bFirst) DisconnectedArray += TEXT(", ");
            DisconnectedArray += FString::Printf(TEXT("\"%s\""), *Pair.Key);
            bFirst = false;
        }
    }
    DisconnectedArray += TEXT("]");

    const FString JsonContent = FString::Printf(
        TEXT("{\n")
        TEXT("  \"channelId\": \"%s\",\n")
        TEXT("  \"port\": %d,\n")
        TEXT("  \"status\": \"%s\",\n")
        TEXT("  \"currentPlayers\": %d,\n")
        TEXT("  \"maxPlayers\": 3,\n")
        TEXT("  \"mapName\": \"%s\",\n")
        TEXT("  \"lastUpdate\": \"%s\",\n")
        TEXT("  \"disconnectedPlayers\": %s\n")
        TEXT("}"),
        *ChannelId, Port, *Status, PlayerCount, *MapName, *LastUpdate, *DisconnectedArray
    );

    const FString FilePath = GetRegistryFilePath();
    if (!FFileHelper::SaveStringToFile(JsonContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogHelluna, Error, TEXT("[DefenseGameMode] 레지스트리 파일 쓰기 실패: %s"), *FilePath);
    }
}

void AHellunaDefenseGameMode::DeleteRegistryFile()
{
    const FString FilePath = GetRegistryFilePath();
    if (IFileManager::Get().FileExists(*FilePath))
    {
        IFileManager::Get().Delete(*FilePath);
        UE_LOG(LogHelluna, Log, TEXT("[DefenseGameMode] 레지스트리 파일 삭제: %s"), *FilePath);
    }
}

// ============================================================
// [Phase 16] CheckIdleShutdown
// ============================================================

void AHellunaDefenseGameMode::CheckIdleShutdown()
{
    if (CurrentPlayerCount == 0 && !bGameEnded)
    {
        UE_LOG(LogHelluna, Log, TEXT("[Phase16] 유휴 종료 — 접속자 0, 서버 종료"));
        DeleteRegistryFile();
        FGenericPlatformMisc::RequestExit(false);
    }
}

// ============================================================
// [Phase 19] 커맨드 파일 폴링 — 빈 서버 맵 전환
// ============================================================

void AHellunaDefenseGameMode::PollForCommand()
{
    if (CurrentPlayerCount > 0 || bGameEnded)
    {
        return;
    }

    const FString CmdPath = FPaths::Combine(
        GetRegistryDirectoryPath(),
        FString::Printf(TEXT("command_%d.json"), GetServerPort()));

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *CmdPath))
    {
        return;
    }

    TSharedPtr<FJsonObject> JsonObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
    {
        return;
    }

    const FString Command = JsonObj->GetStringField(TEXT("command"));
    if (Command != TEXT("servertravel"))
    {
        return;
    }

    const FString MapPath = JsonObj->GetStringField(TEXT("mapPath"));
    if (MapPath.IsEmpty())
    {
        return;
    }

    // 커맨드 파일 삭제 (먼저 삭제 — 중복 실행 방지)
    IFileManager::Get().Delete(*CmdPath);

    // 타이머 정리
    StopCommandPollTimer();
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(IdleShutdownTimer);
        W->GetTimerManager().ClearTimer(RegistryHeartbeatTimer);
    }

    // [Phase 19 수정] ServerTravel은 UE 5.7 World Partition 크래시 유발 → RequestExit로 프로세스 종료
    UE_LOG(LogHelluna, Log, TEXT("[Phase19] 커맨드 파일 감지 → RequestExit (ServerTravel 대신) | MapPath=%s"), *MapPath);
    FGenericPlatformMisc::RequestExit(false);
}

void AHellunaDefenseGameMode::StartCommandPollTimer()
{
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimer(CommandPollTimer, this,
            &AHellunaDefenseGameMode::PollForCommand, 2.0f, true);
    }
}

void AHellunaDefenseGameMode::StopCommandPollTimer()
{
    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().ClearTimer(CommandPollTimer);
    }
}
