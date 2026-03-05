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
    CacheMeleeSpawnPoints();
    CacheRangeSpawnPoints();

    UE_LOG(LogTemp, Warning, TEXT("[DefenseGameMode] BeginPlay 완료 — BossSpawn:%d / MeleeSpawn:%d / RangeSpawn:%d"),
        BossSpawnPoints.Num(), MeleeSpawnPoints.Num(), RangeSpawnPoints.Num());
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

    UE_LOG(LogTemp, Warning, TEXT("[CacheBossSpawnPoints] BossSpawnPoints: %d개 (태그: %s)"),
        BossSpawnPoints.Num(), *BossSpawnPointTag.ToString());
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
        GS->MulticastPrintDay();
        GS->NetMulticast_OnDawnPassed(TestDayDuration);
    }

    GetWorldTimerManager().ClearTimer(TimerHandle_ToNight);
    GetWorldTimerManager().SetTimer(TimerHandle_ToNight, this, &ThisClass::EnterNight, TestDayDuration, false);
}

void AHellunaDefenseGameMode::EnterNight()
{
    if (!HasAuthority() || !bGameInitialized) return;

    Debug::Print(FString::Printf(TEXT("[EnterNight] %d일차 밤 시작"), CurrentDay), FColor::Purple);

    RemainingMonstersThisNight = 0;

    if (AHellunaDefenseGameState* GS = GetGameState<AHellunaDefenseGameState>())
    {
        GS->SetPhase(EDefensePhase::Night);
        GS->SetAliveMonsterCount(0);
    }

    // ── 보스 소환 일 체크 ──────────────────────────────────────────────
    // BossSchedule 배열에서 CurrentDay와 일치하는 항목을 찾는다.
    // 동일 Day 중복 시 첫 번째 항목만 사용.
    const FBossSpawnEntry* FoundEntry = BossSchedule.FindByPredicate(
        [this](const FBossSpawnEntry& E){ return E.SpawnDay == CurrentDay; });

    UE_LOG(LogTemp, Warning, TEXT("[EnterNight] BossSchedule 체크 — CurrentDay=%d | Schedule 항목수=%d | FoundEntry=%s"),
        CurrentDay, BossSchedule.Num(), FoundEntry ? TEXT("찾음 ✅") : TEXT("없음"));
    if (FoundEntry)
    {
        UE_LOG(LogTemp, Warning, TEXT("[EnterNight] FoundEntry — SpawnDay=%d | BossClass=%s"),
            FoundEntry->SpawnDay,
            FoundEntry->BossClass ? *FoundEntry->BossClass->GetName() : TEXT("null ⚠️"));
    }

    if (FoundEntry)
    {
        Debug::Print(FString::Printf(
            TEXT("[EnterNight] %d일차 — %s 소환 대상, 일반 몬스터 스폰 생략"),
            CurrentDay,
            FoundEntry->BossClass ? *FoundEntry->BossClass->GetName() : TEXT("null")),
            FColor::Red);

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

    Debug::Print(FString::Printf(
        TEXT("[NotifyMonsterDied] %s 사망 | 남은: %d"),
        *DeadMonster->GetName(), RemainingMonstersThisNight),
        FColor::Orange);

    if (RemainingMonstersThisNight <= 0)
    {
        Debug::Print(TEXT("[NotifyMonsterDied] ✅ 전멸 → 낮 전환 타이머 시작"), FColor::Yellow);
        GetWorldTimerManager().ClearTimer(TimerHandle_ToDay);
        GetWorldTimerManager().SetTimer(TimerHandle_ToDay, this, &ThisClass::EnterDay, TestNightFailToDayDelay, false);
    }
}

// 보스몬스터 사망 로직 — NotifyMonsterDied에서 EnemyGrade != Normal 시 호출
//기현님이 수정해주시면 될 거 같습니다!
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
        UE_LOG(LogTemp, Error, TEXT("[TrySummonBoss] BossSpawnPoints 없음! BossSpawnPointTag=%s"), *BossSpawnPointTag.ToString());
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
